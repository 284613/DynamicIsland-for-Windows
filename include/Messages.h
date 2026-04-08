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
#define WM_APP_INVALIDATE (WM_APP + 50)  // wParam = DirtyFlags

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
    // P0: 低电量（alertType==5）
    if (alertType == 5) {
        return PRIORITY_P0_CRITICAL;
    }
    // P1: App通知（alertType==3）— 即时通讯或其他App均为P1
    if (alertType == 3) {
        return PRIORITY_P1_IMMEDIATE;
    }
    // P3: 后台状态变化
    if (alertType == 1 ||  // WiFi
        alertType == 2 ||  // 蓝牙
        alertType == 4) {  // 充电
        return PRIORITY_P3_BACKGROUND;
    }
    // 默认: P2 媒体控制
    return PRIORITY_P2_MEDIA;
}

struct AlertInfo {
    int type;                // 1 = WiFi, 2 = Bluetooth, 3 = App通知
    std::wstring name;       // WiFi名 / 蓝牙名 / App名(如 "微信")
    std::wstring deviceType; // 设备类型 / 通知标题
    std::wstring iconPath;   // 【新增】应用图标临时文件路径
    std::vector<uint8_t> iconData; // 【新增】内存图像数据（改为值类型）
    AlertPriority priority = PRIORITY_P2_MEDIA; // 【OPT-03】优先级（0最高，末尾以保持向后兼容）
};


