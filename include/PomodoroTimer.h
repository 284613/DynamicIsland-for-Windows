#pragma once
#include <windows.h>
#include <string>
#include <chrono>
#include <functional>

// 番茄钟组件 - 实用小工具
class PomodoroTimer {
public:
    PomodoroTimer();
    
    // 启动/暂停/重置
    void Start();
    void Pause();
    void Reset();
    
    // 设置时长（分钟）
    void SetDuration(int minutes);
    void SetRemainingSeconds(int seconds);
    
    // 更新（每帧调用）
    void Update(float deltaTime);
    
    // 获取状态
    bool IsRunning() const { return m_isRunning; }
    bool IsPaused() const { return m_isPaused; }
    int GetRemainingSeconds() const;
    float GetProgress() const;  // 0.0 - 1.0
    
    // 获取显示文本
    std::wstring GetDisplayText() const;
    
    // 检查是否结束
    bool IsFinished() const { return m_remainingSeconds <= 0 && !m_isRunning; }
    
    // 回调：结束时调用
    void SetOnFinishCallback(std::function<void()> callback) { m_onFinish = callback; }

private:
    int m_durationSeconds = 25 * 60;  // 默认25分钟
    int m_remainingSeconds = 25 * 60;
    bool m_isRunning = false;
    bool m_isPaused = false;
    std::function<void()> m_onFinish;
    
    // 上次更新时间
    std::chrono::steady_clock::time_point m_lastUpdate;
    std::chrono::milliseconds m_accumulatedTime{ 0 };
};

inline PomodoroTimer::PomodoroTimer() {
    m_lastUpdate = std::chrono::steady_clock::now();
}

inline void PomodoroTimer::SetDuration(int minutes) {
    m_durationSeconds = minutes * 60;
    m_remainingSeconds = m_durationSeconds;
    m_accumulatedTime = std::chrono::milliseconds(0);
}

inline void PomodoroTimer::SetRemainingSeconds(int seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    if (seconds > m_durationSeconds) {
        seconds = m_durationSeconds;
    }
    m_remainingSeconds = seconds;
    m_accumulatedTime = std::chrono::milliseconds(0);
}

inline void PomodoroTimer::Start() {
    if (m_remainingSeconds > 0) {
        m_isRunning = true;
        m_isPaused = false;
        m_lastUpdate = std::chrono::steady_clock::now();
    }
}

inline void PomodoroTimer::Pause() {
    m_isPaused = true;
    m_isRunning = false;
}

inline void PomodoroTimer::Reset() {
    m_remainingSeconds = m_durationSeconds;
    m_isRunning = false;
    m_isPaused = false;
    m_accumulatedTime = std::chrono::milliseconds(0);
}

inline void PomodoroTimer::Update(float deltaTime) {
    if (!m_isRunning || m_isPaused) return;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastUpdate);
    m_lastUpdate = now;

    m_accumulatedTime += elapsed;
    while (m_accumulatedTime >= std::chrono::seconds(1) && m_remainingSeconds > 0) {
        m_accumulatedTime -= std::chrono::seconds(1);
        --m_remainingSeconds;
    }
    
    if (m_remainingSeconds <= 0) {
        m_remainingSeconds = 0;
        m_isRunning = false;
        m_isPaused = false;
        m_accumulatedTime = std::chrono::milliseconds(0);
        if (m_onFinish) {
            m_onFinish();
        }
    }
}

inline int PomodoroTimer::GetRemainingSeconds() const {
    return m_remainingSeconds;
}

inline float PomodoroTimer::GetProgress() const {
    if (m_durationSeconds == 0) return 0.0f;
    return 1.0f - (float)m_remainingSeconds / (float)m_durationSeconds;
}

inline std::wstring PomodoroTimer::GetDisplayText() const {
    int minutes = m_remainingSeconds / 60;
    int seconds = m_remainingSeconds % 60;
    
    wchar_t buffer[16];
    swprintf_s(buffer, L"%02d:%02d", minutes, seconds);
    return std::wstring(buffer);
}


