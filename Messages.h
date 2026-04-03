// Messages.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#define WM_UPDATE_ALBUM_ART (WM_USER + 100)
#define WM_UPDATE_ALBUM_ART_MEMORY (WM_USER + 102) // 【新增】内存流专辑封面
#define WM_SHOW_ALERT       (WM_USER + 103) // 【新增】弹出提示消息
#define WM_SHOW_ALERT_MEMORY (WM_USER + 106) // 【新增】内存流通知
#define WM_UPDATE_LYRICS    (WM_USER + 104) // 【新增】歌词更新消息
#define WM_DRAG_ENTER (WM_USER + 201)
#define WM_DRAG_LEAVE (WM_USER + 202)
#define WM_DROP_FILE  (WM_USER + 203)
#define WM_FILE_REMOVED (WM_USER + 204)

// 内存图像数据结构
struct ImageData {
    std::vector<uint8_t> data;  // 图像字节数据（改为值类型）
};

// 【OPT-03】优先级调度系统
enum AlertPriority {
    PRIORITY_P0_CRITICAL = 0, // 系统严重警告（低电量、断网）- 可打断动画
    PRIORITY_P1_IMMEDIATE = 1, // 即时通讯或App弹窗
    PRIORITY_P2_MEDIA = 2, // 多媒体控制（切歌、暂停）
    PRIORITY_P3_BACKGROUND = 3, // 后台状态变化（WiFi连接成功）
};

// 获取通知来源对应的优先级
inline AlertPriority GetAlertPriority(int alertType, const std::wstring& name) {
    // P0: 系统严重警告
    if (name.find(L"低电量") != std::wstring::npos ||
        name.find(L"电量") != std::wstring::npos && alertType == 3) {
        return PRIORITY_P0_CRITICAL;
    }
    // P1: 即时通讯App通知
    if (alertType == 3) { // App通知通常是type=3
        if (name.find(L"微信") != std::wstring::npos ||
            name.find(L"QQ") != std::wstring::npos ||
            name.find(L"钉钉") != std::wstring::npos ||
            name.find(L"飞书") != std::wstring::npos ||
            name.find(L"Slack") != std::wstring::npos) {
            return PRIORITY_P1_IMMEDIATE;
        }
        return PRIORITY_P1_IMMEDIATE; // 其他App通知都是P1
    }
    // P2: 媒体控制
    if (alertType == 0 || alertType == 1) { // 假设0/1是媒体相关
        return PRIORITY_P2_MEDIA;
    }
    // P3: 后台状态（WiFi连接等）
    if (alertType == 1) {
        return PRIORITY_P3_BACKGROUND;
    }
    return PRIORITY_P2_MEDIA; // 默认
}

struct AlertInfo {
    int type;                // 1 = WiFi, 2 = Bluetooth, 3 = App通知
    std::wstring name;       // WiFi名 / 蓝牙名 / App名(如 "微信")
    std::wstring deviceType; // 设备类型 / 通知标题
    std::wstring iconPath;   // 【新增】应用图标临时文件路径
    std::vector<uint8_t> iconData; // 【新增】内存图像数据（改为值类型）
    uint8_t priority = PRIORITY_P2_MEDIA; // 【OPT-03】优先级（0最高，末尾以保持向后兼容）
};


