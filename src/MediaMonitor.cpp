//MediaMonitor.cpp
#include "MediaMonitor.h"
#include "EventBus.h"
#include <winrt/base.h>    
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <shlobj.h>
#include <vector>
#include <cstdint>
#include <chrono>

#pragma comment(lib, "windowsapp.lib") // 链接 WinRT 库

// 加上 winrt:: 前缀，防止和 Win32 的 Windows 宏发生冲突
using namespace winrt;
using namespace winrt::Windows::Media::Control;

MediaMonitor::MediaMonitor() : m_running(false) {}

MediaMonitor::~MediaMonitor()
{
	m_running = false;

	if (m_workerThread.joinable())
		m_workerThread.join();

	// 清理内存中的专辑封面数据
	if (m_albumArtData)
	{
		delete m_albumArtData;
		m_albumArtData = nullptr;
	}
}

bool MediaMonitor::Initialize() {

	// 1. 初始化 WASAPI (用于抓取实时跳动的音量波形)
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_deviceEnumerator);
	if (SUCCEEDED(hr)) {
		hr = m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_audioDevice);
		if (SUCCEEDED(hr)) {
			m_audioDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, NULL, (void**)&m_audioMeter);
			hr = m_audioDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&m_endpointVolume);
		}
	}

	// 2. 启动后台线程抓取歌名 (防止 WinRT 阻塞主 UI 线程的物理动画)
	m_running = true;
	m_workerThread = std::thread(&MediaMonitor::BackgroundMediaWorker, this);

	return true;
}

float MediaMonitor::GetAudioLevel() {
	if (!m_audioMeter) return 0.0f;
	float peak = 0.0f;
	m_audioMeter->GetPeakValue(&peak); // 获取 0.0 到 1.0 之间的音量峰值
	return peak;
}

std::wstring MediaMonitor::GetTitle() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_title;
}

std::wstring MediaMonitor::GetArtist() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_artist;
}

// 运行在独立的后台线程中，每隔1秒抓取一次正在播放的音乐信息
void MediaMonitor::BackgroundMediaWorker() {
	// 不要调用 winrt::init_apartment()，因为后台线程不需要初始化 WinRT
	// 或者使用 MTA (多线程公寓)，不需要显式初始化
	// winrt::init_apartment();  // 删除这行

	try {
		auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

		while (m_running) {
			try {
				if (manager) {
					auto session = manager.GetCurrentSession();
					m_hasSession = (session != nullptr);
					if (session) {
						auto playbackInfo = session.GetPlaybackInfo();
						auto status = playbackInfo.PlaybackStatus();

						// 获取播放进度
						auto timelineProperties = session.GetTimelineProperties();
						if (timelineProperties) {
							auto position = timelineProperties.Position();
							auto duration = timelineProperties.EndTime();
							m_position = std::chrono::duration_cast<std::chrono::seconds>(position).count();
							m_duration = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
						}

						if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
							m_isPlaying = true;
							auto props = session.TryGetMediaPropertiesAsync().get();
							if (props) {
								std::wstring currentTitle = props.Title().c_str();
								std::wstring currentArtist = props.Artist().c_str();

								// 【核心优化】只有在"切歌"时，才去更新UI和拉取封面
								if (currentTitle != m_lastPolledTitle || currentArtist != m_lastPolledArtist) {
									m_lastPolledTitle = currentTitle;
									m_lastPolledArtist = currentArtist;

									{
										std::lock_guard<std::mutex> lock(m_mutex);
										m_title = currentTitle;
										m_artist = currentArtist;
									}

									// 仅在切歌的这一瞬间，去向系统请求并保存专辑封面到内存
									auto thumbnail = props.Thumbnail();
									if (thumbnail) {
										Sleep(500);
										std::vector<uint8_t>* newData = ReadThumbnailToMemory(thumbnail);
										if (newData) {
											// 清理旧数据
											if (m_albumArtData) {
												delete m_albumArtData;
											}
											m_albumArtData = newData;

											// 发送内存数据给主线程（通过EventBus）
											ImageData* imageData = new ImageData{ *newData };
											Event e(EventType::MediaMetadataChanged, 0, 0);
											e.userData = imageData;
											EventBus::GetInstance().Publish(e);
										}
										else {
											ClearAlbumArt();
										}
									}
									else {
										ClearAlbumArt();
									}
								}
							}
						}
						else {
							m_isPlaying = false;
							// 避免每秒都在上锁和清空UI
							if (m_lastPolledTitle != L"") {
								m_lastPolledTitle = L"";
								m_lastPolledArtist = L"";
								std::lock_guard<std::mutex> lock(m_mutex);
								m_title = L"已暂停";
								m_artist = L"";
								ClearAlbumArt();
							}
						}
					}
					else {
						m_isPlaying = false;
						if (m_lastPolledTitle != L"") {
							m_lastPolledTitle = L"";
							m_lastPolledArtist = L"";
							std::lock_guard<std::mutex> lock(m_mutex);
							m_title = L"无音乐播放";
							m_artist = L"";
							ClearAlbumArt();
						}
					}
				}
			}
			// 遇到 COM 代理错误（比如 Chrome 浏览器抛出的无封面异常）时，静默忽略
			catch (winrt::hresult_error const&) {}
			catch (...) {}

			// 【OPT-02】智能休眠：根据播放状态和岛屿状态调整轮询间隔
			if (m_isPlaying) {
				m_pollIntervalMs = 1000; // 正在播放：1秒轮询
			} else if (m_hasSession) {
				m_pollIntervalMs = 5000; // 有会话但暂停：5秒轮询
			} else {
				m_pollIntervalMs = 10000; // 无会话：10秒轮询
			}
			Sleep(m_pollIntervalMs);
		}
	}
	catch (...) {
		m_running = false;
	}
}
bool MediaMonitor::IsPlaying() const {
	return m_isPlaying.load();  // 原子变量无需锁
}

void MediaMonitor::SetExpandedState(bool expanded) {
	// 【OPT-02】岛屿展开时缩短轮询间隔，确保进度条流畅更新
	if (expanded && m_isPlaying) {
		m_pollIntervalMs = 1000; // 展开+播放：1秒轮询
	} else if (expanded && !m_isPlaying && m_hasSession) {
		m_pollIntervalMs = 2000; // 展开+暂停：2秒轮询
	}
	// 收起状态由智能休眠逻辑自动调整
}

std::wstring MediaMonitor::GetAlbumArt()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return L""; // 不再返回文件路径
}
// MediaMonitor.cpp 中 ReadThumbnailToMemory 函数的实现
std::vector<uint8_t>* MediaMonitor::ReadThumbnailToMemory(const IRandomAccessStreamReference& thumbnail)
{
	if (!thumbnail)
		return nullptr;

	try
	{
		auto stream = thumbnail.OpenReadAsync().get();
		DataReader reader{ stream };
		reader.LoadAsync(static_cast<uint32_t>(stream.Size())).get();

		std::vector<uint8_t>* buffer = new std::vector<uint8_t>();
		buffer->resize(stream.Size());
		reader.ReadBytes(*buffer);

		return buffer;
	}
	catch (...)
	{
		// 捕获异常，避免因封面加载失败导致整个媒体监控逻辑崩溃
		return nullptr;
	}

	return nullptr;
}
void MediaMonitor::ClearAlbumArt() {
	// 清理内存数据
	if (m_albumArtData) {
		delete m_albumArtData;
		m_albumArtData = nullptr;
	}

	// 通知UI线程清除封面（通过EventBus发送nullptr）
	Event e(EventType::MediaMetadataChanged, 0, 0);
	e.userData = (ImageData*)nullptr;
	EventBus::GetInstance().Publish(e);
}
void MediaMonitor::PlayPause() {
	std::thread([]() {
		winrt::init_apartment();
		try {
			auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			if (!manager) return;
			auto session = manager.GetCurrentSession();
			if (!session) return;

			auto status = session.GetPlaybackInfo().PlaybackStatus();
			if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
				session.TryPauseAsync().get();    // 正在播放则暂停
			}
			else {
				session.TryPlayAsync().get();     // 否则尝试播放
			}
		}
		catch (...) {}
		}).detach();
}

void MediaMonitor::Next() {
	std::thread([]() {
		winrt::init_apartment();
		try {
			auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			if (!manager) return;
			auto session = manager.GetCurrentSession();
			if (!session) return;
			session.TrySkipNextAsync().get();
		}
		catch (...) {}
		}).detach();
}

void MediaMonitor::Previous() {
	std::thread([]() {
		winrt::init_apartment();
		try {
			auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			if (!manager) return;
			auto session = manager.GetCurrentSession();
			if (!session) return;
			session.TrySkipPreviousAsync().get();
		}
		catch (...) {}
		}).detach();
}

std::chrono::seconds MediaMonitor::GetPosition() const {
	return std::chrono::seconds(m_position.load());
}

std::chrono::seconds MediaMonitor::GetDuration() const {
	return std::chrono::seconds(m_duration.load());
}

void MediaMonitor::SetPosition(std::chrono::seconds position) {
	std::thread([position]() {
		winrt::init_apartment();
		try {
			auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			if (!manager) return;
			auto session = manager.GetCurrentSession();
			if (!session) return;

			int64_t positionTicks = std::chrono::duration_cast<std::chrono::microseconds>(position).count() * 10;
			session.TryChangePlaybackPositionAsync(positionTicks).get();
		}
		catch (...) {}
		}).detach();
}
float MediaMonitor::GetVolume() {
	if (!m_endpointVolume) return 0.0f;
	float vol = 0.0f;
	m_endpointVolume->GetMasterVolumeLevelScalar(&vol);
	return vol;
}

void MediaMonitor::SetVolume(float volume) {
	if (!m_endpointVolume) return;
	m_endpointVolume->SetMasterVolumeLevelScalar(volume, NULL);
}

