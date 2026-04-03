#pragma once
#include <windows.h>
#include <vector>
#include <memory>

// ============================================
// 分离态小岛 - 支持多窗口并存
// ============================================

// 小岛类型
enum class IslandType {
    Main,       // 主岛（音乐/通知）
    Mini,       // Mini岛（仅显示时间）
    Pomodoro,   // 番茄钟
    Stats,      // 系统状态（CPU/内存）
};

// 小岛配置
struct IslandConfig {
    IslandType type;
    float width;
    float height;
    int x;  // 屏幕坐标
    int y;
    bool visible;
    HWND hwnd;  // 窗口句柄
};

// 分离态管理器
class IslandManager {
public:
    IslandManager();
    ~IslandManager();
    
    // 创建一个小岛
    bool CreateIsland(IslandType type, int x, int y);
    
    // 移除一个小岛
    void RemoveIsland(int index);
    
    // 更新位置
    void UpdatePosition(int index, int x, int y);
    
    // 显示/隐藏
    void ShowIsland(int index);
    void HideIsland(int index);
    
    // 获取小岛数量
    int GetCount() const { return (int)m_islands.size(); }
    
    // 获取小岛配置
    IslandConfig* GetIsland(int index);
    
    // 主窗口关联（主岛）
    void SetMainIsland(HWND hwnd) { m_mainIsland = hwnd; }
    HWND GetMainIsland() const { return m_mainIsland; }

private:
    std::vector<IslandConfig> m_islands;
    HWND m_mainIsland = nullptr;
};

inline IslandManager::IslandManager() {}
inline IslandManager::~IslandManager() {}

inline bool IslandManager::CreateIsland(IslandType type, int x, int y) {
    IslandConfig config;
    config.type = type;
    config.visible = true;
    config.hwnd = nullptr;
    
    // 根据类型设置默认尺寸
    switch (type) {
        case IslandType::Main:
            config.width = 340.0f;
            config.height = 160.0f;
            break;
        case IslandType::Mini:
            config.width = 80.0f;
            config.height = 28.0f;
            break;
        case IslandType::Pomodoro:
            config.width = 120.0f;
            config.height = 40.0f;
            break;
        case IslandType::Stats:
            config.width = 200.0f;
            config.height = 60.0f;
            break;
    }
    
    config.x = x;
    config.y = y;
    
    m_islands.push_back(config);
    return true;
}

inline void IslandManager::RemoveIsland(int index) {
    if (index >= 0 && index < (int)m_islands.size()) {
        m_islands.erase(m_islands.begin() + index);
    }
}

inline void IslandManager::UpdatePosition(int index, int x, int y) {
    if (index >= 0 && index < (int)m_islands.size()) {
        m_islands[index].x = x;
        m_islands[index].y = y;
    }
}

inline void IslandManager::ShowIsland(int index) {
    if (index >= 0 && index < (int)m_islands.size()) {
        m_islands[index].visible = true;
    }
}

inline void IslandManager::HideIsland(int index) {
    if (index >= 0 && index < (int)m_islands.size()) {
        m_islands[index].visible = false;
    }
}

inline IslandConfig* IslandManager::GetIsland(int index) {
    if (index >= 0 && index < (int)m_islands.size()) {
        return &m_islands[index];
    }
    return nullptr;
}


