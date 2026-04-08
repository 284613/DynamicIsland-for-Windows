#pragma once
#include <windows.h>
#include <functional>

// ============================================
// 音量OSD控制器 - 拦截并自定义音量显示
// ============================================
class VolumeOSDController {
public:
    VolumeOSDController();
    ~VolumeOSDController();
    
    // 初始化（注册音量钩子）
    bool Initialize(HWND hwnd);
    
    // 清理
    void Shutdown();
    
    // 显示自定义音量OSD
    void ShowVolumeOSD(float volume);
    
    // 隐藏音量OSD
    void HideVolumeOSD();
    
    // 音量是否正在显示
    bool IsVisible() const { return m_isVisible; }
    
    // 获取当前音量
    float GetCurrentVolume() const { return m_currentVolume; }
    
    // 回调：音量变化时调用
    void SetOnVolumeChangedCallback(std::function<void(float)> callback) {
        m_onVolumeChanged = callback;
    }

private:
    // 处理系统音量通知（静态回调）
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);
    
    HWND m_hwnd = nullptr;
    HWND m_osdWindow = nullptr;
    bool m_isVisible = false;
    float m_currentVolume = 0.5f;
    std::function<void(float)> m_onVolumeChanged;
    
    // 钩子句柄
    HHOOK m_hook = nullptr;
};

// Windows消息常量
#ifndef WM_VOLUME_CHANGED
#define WM_VOLUME_CHANGED 0x8001
#endif


