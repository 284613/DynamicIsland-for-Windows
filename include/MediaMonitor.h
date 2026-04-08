//MediaMonitor.h
#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl.h>
#include <queue>
#include <functional>
#include <condition_variable>
// 【新增】引入 WinRT 核心头文件（必须放在前面）
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h> // 关键：IRandomAccessStreamReference 所在头文件
#include "Messages.h"   // 添加
// 【新增】WinRT 命名空间声明（避免重复写 winrt:: 前缀）
using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams; // 引入 IRandomAccessStreamReference 命名空间

class MediaMonitor {
public:
    MediaMonitor();
    ~MediaMonitor();

    bool Initialize();

    // 获取实时音频峰值 (0.0f 到 1.0f)
    float GetAudioLevel();

    // 获取当前歌名和歌手
    std::wstring GetTitle();
    std::wstring GetArtist();
    // 控制媒体播放`
    void PlayPause();
    void Next();
    void Previous();

    std::wstring GetAlbumArt();
    bool IsPlaying() const;  // 新增
    // MediaMonitor.h public 部分添加
    void SetTargetWindow(HWND hwnd) { m_targetHwnd = hwnd; }
    bool HasSession() const { return m_hasSession.load(); }

    // 音乐进度相关
    std::chrono::seconds GetPosition() const;  // 当前播放位置（秒）
    std::chrono::seconds GetDuration() const;  // 总时长（秒）
    void SetPosition(std::chrono::seconds position);  // 设置播放位置
    float GetVolume();               // 【新增】获取当前音量 (0.0 ~ 1.0)
    void SetVolume(float volume);    // 【新增】设置当前音量 (0.0 ~ 1.0)
    void SetExpandedState(bool expanded); // 【OPT-02】设置岛屿展开状态，动态调整轮询频率
private:
    void BackgroundMediaWorker(); // 后台拉取媒体信息的线程
    std::vector<uint8_t>* ReadThumbnailToMemory(const IRandomAccessStreamReference& thumbnail);
    void ClearAlbumArt();
    
    // 【新增】本地缓存辅助函数
    std::wstring GetAlbumArtCachePath(const std::wstring& title, const std::wstring& artist);
    void SaveToCache(const std::wstring& path, const std::vector<uint8_t>& data);
    std::vector<uint8_t>* LoadFromCache(const std::wstring& path);
    std::wstring CleanFileName(std::wstring name);
private:
    // WASAPI 核心组件 (获取音量)
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> m_deviceEnumerator;
    Microsoft::WRL::ComPtr<IMMDevice> m_audioDevice;
    Microsoft::WRL::ComPtr<IAudioMeterInformation> m_audioMeter;
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> m_endpointVolume;
    // 线程安全的数据
    std::mutex m_mutex;
    std::wstring m_title = L"暂无播放";
    std::wstring m_artist = L"系统";
    std::vector<uint8_t>* m_albumArtData = nullptr; // 内存中的专辑封面数据
    HWND m_targetHwnd = nullptr;
    // 后台线程控制
    std::thread m_workerThread;
    std::atomic<bool> m_isPlaying{ false };  // 新增原子变量，无需额外加锁
    std::atomic<bool> m_running;
    std::atomic<bool> m_hasSession{ false };
    // 【新增】用于记录上次拉取的歌曲，防止每秒重复拉取封面！
    std::wstring m_lastPolledTitle = L"";
    std::wstring m_lastPolledArtist = L"";
    std::atomic<bool> m_needAlbumArtUpdate{ false }; // 【新增】标记是否需要重试封面抓取

    // 音乐进度
    std::atomic<std::chrono::seconds::rep> m_position{ 0 };  // 当前位置（秒）
    std::atomic<std::chrono::seconds::rep> m_duration{ 0 };  // 总时长（秒）
    std::mutex m_controlMutex; // 控制操作的互斥锁，防止同时调用多个控制函数导致状态混乱
    event_token m_sessionChangedToken; // 【OPT-02】会话变更事件订阅
    int m_pollIntervalMs = 1000; // 【OPT-02】当前轮询间隔（毫秒）
   

    
};



