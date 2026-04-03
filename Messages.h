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

struct AlertInfo {
    int type;                // 1 = WiFi, 2 = Bluetooth, 3 = App通知
    std::wstring name;       // WiFi名 / 蓝牙名 / App名(如 "微信")
    std::wstring deviceType; // 设备类型 / 通知标题
    std::wstring iconPath;   // 【新增】应用图标临时文件路径
    std::vector<uint8_t> iconData; // 【新增】内存图像数据（改为值类型）
};


