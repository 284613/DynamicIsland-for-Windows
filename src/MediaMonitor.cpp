//MediaMonitor.cpp
#include "MediaMonitor.h"
#include "EventBus.h"
#include <winrt/base.h>    
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vector>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "windowsapp.lib") // 链接 WinRT 库
#pragma comment(lib, "shlwapi.lib")

// 加上 winrt:: 前缀，防止和 Win32 的 Windows 宏发生冲突
using namespace winrt;
using namespace winrt::Windows::Media::Control;

namespace {
float Clamp01(float value) {
	if (value < 0.0f) return 0.0f;
	if (value > 1.0f) return 1.0f;
	return value;
}

float ByteToSample(const BYTE* frame, WORD bitsPerSample) {
	switch (bitsPerSample) {
	case 8:
		return (static_cast<float>(*frame) - 128.0f) / 128.0f;
	case 16:
		return static_cast<float>(*reinterpret_cast<const int16_t*>(frame)) / 32768.0f;
	case 24: {
		int32_t sample = (frame[0] | (frame[1] << 8) | (frame[2] << 16));
		if (sample & 0x800000) sample |= ~0xFFFFFF;
		return static_cast<float>(sample) / 8388608.0f;
	}
	case 32:
		return static_cast<float>(*reinterpret_cast<const int32_t*>(frame)) / 2147483648.0f;
	default:
		return 0.0f;
	}
}
}

// 辅助函数：获取 EXE 所在目录
std::wstring GetAppDirectory() {
	wchar_t buffer[MAX_PATH];
	GetModuleFileNameW(NULL, buffer, MAX_PATH);
	PathRemoveFileSpecW(buffer);
	return std::wstring(buffer);
}

MediaMonitor::MediaMonitor() : m_running(false) {}

MediaMonitor::~MediaMonitor()
{
	m_running = false;
	m_waitCv.notify_all();

	if (m_workerThread.joinable())
		m_workerThread.join();
	if (m_audioThread.joinable())
		m_audioThread.join();

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
	m_audioThread = std::thread(&MediaMonitor::AudioCaptureWorker, this);

	return true;
}

float MediaMonitor::GetAudioLevel() {
	if (!m_audioMeter) return 0.0f;
	float peak = 0.0f;
	m_audioMeter->GetPeakValue(&peak); // 获取 0.0 到 1.0 之间的音量峰值
	return peak;
}

std::array<float, 3> MediaMonitor::GetWaveformBands() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_waveformBands;
}

std::wstring MediaMonitor::GetTitle() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_title;
}

std::wstring MediaMonitor::GetArtist() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_artist;
}

void MediaMonitor::RequestImmediateRefresh(bool refreshAlbumArt) {
	if (refreshAlbumArt) {
		m_needAlbumArtUpdate = true;
	}
	m_eventWakeRequested = true;
	m_waitCv.notify_one();
}

void MediaMonitor::DetachSessionHandlers() {
	if (m_currentSession) {
		if (m_playbackInfoChangedToken.value) {
			m_currentSession.PlaybackInfoChanged(m_playbackInfoChangedToken);
			m_playbackInfoChangedToken = {};
		}
		if (m_mediaPropertiesChangedToken.value) {
			m_currentSession.MediaPropertiesChanged(m_mediaPropertiesChangedToken);
			m_mediaPropertiesChangedToken = {};
		}
		if (m_timelinePropertiesChangedToken.value) {
			m_currentSession.TimelinePropertiesChanged(m_timelinePropertiesChangedToken);
			m_timelinePropertiesChangedToken = {};
		}
	}
	m_currentSession = nullptr;
}

void MediaMonitor::AttachSessionHandlers(const GlobalSystemMediaTransportControlsSession& session) {
	DetachSessionHandlers();
	m_currentSession = session;
	if (!m_currentSession) return;

	m_playbackInfoChangedToken = m_currentSession.PlaybackInfoChanged([this](auto const&, auto const&) {
		RequestImmediateRefresh(false);
	});
	m_mediaPropertiesChangedToken = m_currentSession.MediaPropertiesChanged([this](auto const&, auto const&) {
		RequestImmediateRefresh(true);
	});
	m_timelinePropertiesChangedToken = m_currentSession.TimelinePropertiesChanged([this](auto const&, auto const&) {
		RequestImmediateRefresh(false);
	});
}

int MediaMonitor::ComputePollIntervalMs() const {
    if (m_isExpanded && m_hasSession) {
        return (std::max)(250, m_basePollIntervalMs / (m_isPlaying ? 2 : 1));
    }

    if (m_isPlaying) {
        return (std::max)(250, m_basePollIntervalMs / 2);
    }

    if (m_hasSession) {
        return (std::max)(500, m_basePollIntervalMs);
    }

    return (std::max)(1500, m_basePollIntervalMs * 4);
}

void MediaMonitor::AudioCaptureWorker() {
	HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
		return;
	}

	Microsoft::WRL::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
	Microsoft::WRL::ComPtr<IMMDevice> audioDevice;
	Microsoft::WRL::ComPtr<IAudioClient> audioClient;
	Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
	WAVEFORMATEX* mixFormat = nullptr;

	auto resetBands = [this]() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_waveformBands = { 0.0f, 0.0f, 0.0f };
	};

	do {
		if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(deviceEnumerator.GetAddressOf())))) {
			break;
		}

		if (FAILED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audioDevice))) {
			break;
		}

		if (FAILED(audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audioClient.GetAddressOf())))) {
			break;
		}

		if (FAILED(audioClient->GetMixFormat(&mixFormat)) || !mixFormat) {
			break;
		}

		const REFERENCE_TIME requestedDuration = 1000000; // 100 ms
		if (FAILED(audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, requestedDuration, 0, mixFormat, nullptr))) {
			break;
		}

		if (FAILED(audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(captureClient.GetAddressOf())))) {
			break;
		}

		if (FAILED(audioClient->Start())) {
			break;
		}

		const bool isFloat =
			(mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
			(mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
			 reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
		const float sampleRate = static_cast<float>(mixFormat->nSamplesPerSec);
		const WORD channelCount = (std::max)(static_cast<WORD>(1), mixFormat->nChannels);
		const WORD sampleBytes = (std::max)(static_cast<WORD>(1), static_cast<WORD>(mixFormat->nBlockAlign / channelCount));
		const WORD frameBytes = mixFormat->nBlockAlign;

		const float lowAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * 220.0f / sampleRate);
		const float midAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * 1800.0f / sampleRate);
		float lowState = 0.0f;
		float midState = 0.0f;

		while (m_running) {
			UINT32 packetLength = 0;
			if (FAILED(captureClient->GetNextPacketSize(&packetLength))) {
				break;
			}

			std::array<float, 3> energy = { 0.0f, 0.0f, 0.0f };
			uint64_t sampleCount = 0;

			while (packetLength != 0) {
				BYTE* data = nullptr;
				UINT32 numFrames = 0;
				DWORD flags = 0;
				if (FAILED(captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr))) {
					packetLength = 0;
					break;
				}

				if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
					for (UINT32 frameIndex = 0; frameIndex < numFrames; ++frameIndex) {
						const BYTE* frameBase = data + frameIndex * frameBytes;
						float monoSample = 0.0f;
						for (WORD channel = 0; channel < channelCount; ++channel) {
							const BYTE* sampleBase = frameBase + channel * sampleBytes;
							if (isFloat) {
								monoSample += *reinterpret_cast<const float*>(sampleBase);
							} else {
								monoSample += ByteToSample(sampleBase, mixFormat->wBitsPerSample);
							}
						}
						monoSample /= static_cast<float>(channelCount);

						lowState += (monoSample - lowState) * lowAlpha;
						midState += (monoSample - midState) * midAlpha;
						const float lowBand = lowState;
						const float midBand = midState - lowState;
						const float highBand = monoSample - midState;

						energy[0] += lowBand * lowBand;
						energy[1] += midBand * midBand;
						energy[2] += highBand * highBand;
					}
					sampleCount += numFrames;
				}

				captureClient->ReleaseBuffer(numFrames);
				if (FAILED(captureClient->GetNextPacketSize(&packetLength))) {
					packetLength = 0;
					break;
				}
			}

			std::array<float, 3> nextBands = { 0.0f, 0.0f, 0.0f };
			if (sampleCount > 0) {
				nextBands[0] = Clamp01(std::sqrt(energy[0] / static_cast<float>(sampleCount)) * 7.5f);
				nextBands[1] = Clamp01(std::sqrt(energy[1] / static_cast<float>(sampleCount)) * 11.0f);
				nextBands[2] = Clamp01(std::sqrt(energy[2] / static_cast<float>(sampleCount)) * 15.0f);
			}

			{
				std::lock_guard<std::mutex> lock(m_mutex);
				for (size_t i = 0; i < m_waveformBands.size(); ++i) {
					const float blend = nextBands[i] > m_waveformBands[i] ? 0.65f : 0.22f;
					m_waveformBands[i] += (nextBands[i] - m_waveformBands[i]) * blend;
					if (sampleCount == 0) {
						m_waveformBands[i] *= 0.88f;
					}
				}
			}

			Sleep(10);
		}

		audioClient->Stop();
	} while (false);

	resetBands();
	if (mixFormat) {
		CoTaskMemFree(mixFormat);
	}
	CoUninitialize();
}

void MediaMonitor::SyncCurrentSession() {
	bool wasPlaying = m_isPlaying.load();
	bool hadSession = m_hasSession.load();

	try {
		if (!m_manager) {
			m_manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			if (m_manager && !m_sessionChangedToken.value) {
				m_sessionChangedToken = m_manager.CurrentSessionChanged([this](auto const&, auto const&) {
					RequestImmediateRefresh(false);
				});
			}
		}

		auto session = m_manager ? m_manager.GetCurrentSession() : GlobalSystemMediaTransportControlsSession(nullptr);
		if ((session && !m_currentSession) || (!session && m_currentSession) || (session && m_currentSession && session.SourceAppUserModelId() != m_currentSession.SourceAppUserModelId())) {
			AttachSessionHandlers(session);
		}

		m_hasSession = (session != nullptr);
		if (session) {
			auto playbackInfo = session.GetPlaybackInfo();
			auto status = playbackInfo.PlaybackStatus();
			m_isPlaying = (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

			auto timelineProperties = session.GetTimelineProperties();
			if (timelineProperties) {
				auto position = timelineProperties.Position();
				auto duration = timelineProperties.EndTime();
				m_positionMs = std::chrono::duration_cast<std::chrono::milliseconds>(position).count();
				m_durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
			}

			auto props = session.TryGetMediaPropertiesAsync().get();
			if (props) {
				std::wstring currentTitle = props.Title().c_str();
				std::wstring currentArtist = props.Artist().c_str();

				bool songChanged = (currentTitle != m_lastPolledTitle || currentArtist != m_lastPolledArtist);
				if (songChanged || m_needAlbumArtUpdate) {
					if (songChanged) {
						m_lastPolledTitle = currentTitle;
						m_lastPolledArtist = currentArtist;
						m_needAlbumArtUpdate = true;

						std::lock_guard<std::mutex> lock(m_mutex);
						m_title = currentTitle;
						m_artist = currentArtist;
					}

					std::wstring cachePath = GetAlbumArtCachePath(currentTitle, currentArtist);
					std::vector<uint8_t>* cachedData = LoadFromCache(cachePath);

					if (cachedData) {
						{
							std::lock_guard<std::mutex> lock(m_mutex);
							if (m_albumArtData) delete m_albumArtData;
							m_albumArtData = cachedData;
						}
						ImageData* imageData = new ImageData{ *cachedData };
						Event e(EventType::MediaMetadataChanged, 0, 0);
						e.userData = imageData;
						EventBus::GetInstance().Publish(e);
						m_needAlbumArtUpdate = false;
					} else {
						auto thumbnail = props.Thumbnail();
						if (thumbnail) {
							std::vector<uint8_t>* newData = ReadThumbnailToMemory(thumbnail);
							if (newData) {
								{
									std::lock_guard<std::mutex> lock(m_mutex);
									if (m_albumArtData) delete m_albumArtData;
									m_albumArtData = newData;
								}
								ImageData* imageData = new ImageData{ *newData };
								Event e(EventType::MediaMetadataChanged, 0, 0);
								e.userData = imageData;
								EventBus::GetInstance().Publish(e);
								SaveToCache(cachePath, *newData);
								m_needAlbumArtUpdate = false;
							}
						}
					}
				}
			}
		} else {
			DetachSessionHandlers();
			m_isPlaying = false;
			if (m_lastPolledTitle != L"") {
				m_lastPolledTitle.clear();
				m_lastPolledArtist.clear();
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_title = L"无音乐播放";
					m_artist = L"";
				}
				ClearAlbumArt();
			}
		}
	} catch (winrt::hresult_error const&) {
	} catch (...) {
	}

	if (m_isPlaying.load() != wasPlaying) {
		EventBus::GetInstance().PublishMediaPlaybackStateChanged(m_isPlaying.load());
	}
	if (m_hasSession.load() != hadSession) {
		EventBus::GetInstance().PublishMediaSessionChanged(m_hasSession.load());
	}

	m_pollIntervalMs = ComputePollIntervalMs();
}

// 运行在独立的后台线程中，使用事件唤醒 + 轮询兜底
void MediaMonitor::BackgroundMediaWorker() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		RequestImmediateRefresh(true);

		while (m_running) {
			SyncCurrentSession();
			std::unique_lock<std::mutex> lock(m_waitMutex);
			m_waitCv.wait_for(lock, std::chrono::milliseconds(m_pollIntervalMs), [this]() {
				return !m_running || m_eventWakeRequested.load();
			});
			m_eventWakeRequested = false;
		}

		if (m_manager && m_sessionChangedToken.value) {
			m_manager.CurrentSessionChanged(m_sessionChangedToken);
			m_sessionChangedToken = {};
		}
		DetachSessionHandlers();
	}
	catch (...) {
		m_running = false;
	}
}

// --- 本地缓存辅助函数实现 ---

std::wstring MediaMonitor::CleanFileName(std::wstring name) {
	std::wstring forbidden = L"\\/:*?\"<>|";
	for (auto& c : name) {
		if (forbidden.find(c) != std::wstring::npos) c = L'_';
	}
	return name;
}

std::wstring MediaMonitor::GetAlbumArtCachePath(const std::wstring& title, const std::wstring& artist) {
	std::wstring baseDir = GetAppDirectory() + L"\\albumart_cache";
	CreateDirectoryW(baseDir.c_str(), nullptr);
	
	std::wstring fileName = CleanFileName(artist) + L" - " + CleanFileName(title) + L".jpg";
	return baseDir + L"\\" + fileName;
}

void MediaMonitor::SaveToCache(const std::wstring& path, const std::vector<uint8_t>& data) {
	std::ofstream file(path, std::ios::binary);
	if (file.is_open()) {
		file.write(reinterpret_cast<const char*>(data.data()), data.size());
		file.close();
	}
}

std::vector<uint8_t>* MediaMonitor::LoadFromCache(const std::wstring& path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open()) return nullptr;

	std::streamsize size = file.tellg();
	if (size <= 0) return nullptr;

	file.seekg(0, std::ios::beg);
	std::vector<uint8_t>* buffer = new std::vector<uint8_t>(size);
	if (file.read(reinterpret_cast<char*>(buffer->data()), size)) {
		return buffer;
	}
	
	delete buffer;
	return nullptr;
}
bool MediaMonitor::IsPlaying() const {
	return m_isPlaying.load();  // 原子变量无需锁
}

void MediaMonitor::SetExpandedState(bool expanded) {
	m_isExpanded = expanded;
	m_pollIntervalMs = ComputePollIntervalMs();
	RequestImmediateRefresh(false);
}

void MediaMonitor::RequestAlbumArtRefresh() {
	m_needAlbumArtUpdate = true;
}

void MediaMonitor::SetPollIntervalMs(int intervalMs) {
	m_basePollIntervalMs = (std::max)(250, intervalMs);
	m_pollIntervalMs = ComputePollIntervalMs();
	RequestImmediateRefresh(false);
}

std::wstring MediaMonitor::GetAlbumArt()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return L""; // 不再返回文件路径
}

std::vector<uint8_t> MediaMonitor::GetAlbumArtDataCopy()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_albumArtData) {
		return {};
	}
	return *m_albumArtData;
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
	return std::chrono::duration_cast<std::chrono::seconds>(GetPositionMs());
}

std::chrono::seconds MediaMonitor::GetDuration() const {
	return std::chrono::duration_cast<std::chrono::seconds>(GetDurationMs());
}

std::chrono::milliseconds MediaMonitor::GetPositionMs() const {
	return std::chrono::milliseconds(m_positionMs.load());
}

std::chrono::milliseconds MediaMonitor::GetDurationMs() const {
	return std::chrono::milliseconds(m_durationMs.load());
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

