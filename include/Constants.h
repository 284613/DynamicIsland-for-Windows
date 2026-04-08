#pragma once
#include <windows.h>

// ============================================
// 统一常量命名空间 - 消除硬编码
// ============================================
namespace Constants {

// ============================================
// 窗口尺寸常量
// ============================================
namespace Size {
    // Mini 尺寸（无任务时显示时间）
    constexpr float MINI_WIDTH = 80.0f;
    constexpr float MINI_HEIGHT = 28.0f;
    
    // Collapsed 尺寸
    constexpr float COLLAPSED_WIDTH = MINI_WIDTH;
    constexpr float COLLAPSED_HEIGHT = MINI_HEIGHT;
    
    // 展开尺寸
    constexpr float EXPANDED_WIDTH = 340.0f;
    constexpr float EXPANDED_HEIGHT = 120.0f;
    
    // 音乐展开高度
    constexpr float MUSIC_EXPANDED_HEIGHT = 160.0f;
    
    // Compact 紧凑态
    constexpr float COMPACT_WIDTH = 200.0f;
    constexpr float COMPACT_HEIGHT = 40.0f;
    
    // 提示弹窗尺寸
    constexpr float ALERT_WIDTH = 220.0f;
    constexpr float ALERT_HEIGHT = 40.0f;
    
    // 副岛（音量/静音等）尺寸
    constexpr float SECONDARY_WIDTH = 220.0f;
    constexpr float SECONDARY_HEIGHT = 36.0f;
    
    // 画布尺寸
    constexpr float CANVAS_WIDTH = 400.0f;
    constexpr float CANVAS_HEIGHT = 300.0f;
    
    // 紧凑模式阈值
    constexpr float COMPACT_THRESHOLD = 60.0f;
    constexpr float COMPACT_MIN_HEIGHT = 35.0f;
}

// ============================================
// UI 尺寸常量
// ============================================
namespace UI {
    constexpr float TOP_MARGIN = 10.0f;           // 顶部边距
    constexpr float LEFT_MARGIN = 15.0f;          // 左边距
    constexpr float RIGHT_MARGIN = 20.0f;         // 右边距
    constexpr float BOTTOM_MARGIN = 10.0f;        // 底部边距
    
    constexpr float ALBUM_ART_SIZE = 60.0f;       // 专辑封面尺寸
    constexpr float ALBUM_ART_MARGIN = 20.0f;     // 专辑封面左边距
    constexpr float ALBUM_ART_RADIUS = 12.0f;     // 专辑封面圆角
    
    constexpr float ICON_SIZE = 24.0f;            // 通知图标尺寸
    constexpr float ICON_MARGIN = 8.0f;           // 图标与文本间距
    
    constexpr float BUTTON_SIZE = 30.0f;          // 播放控制按钮尺寸
    constexpr float BUTTON_SPACING = 5.0f;       // 按钮间距
    
    constexpr float PROGRESS_BAR_HEIGHT = 4.0f;   // 进度条高度
    constexpr float VOLUME_BAR_WIDTH = 120.0f;     // 音量条宽度
    constexpr float VOLUME_BAR_HEIGHT = 6.0f;     // 音量条高度
}

// ============================================
// 动画常量
// ============================================
namespace Animation {
    constexpr float TENSION = 0.15f;              // 弹簧张力
    constexpr float FRICTION = 0.7f;             // 摩擦力
    constexpr float ALPHA_INTERPOLATION = 0.15f;  // 透明度插值速度
    constexpr float AUDIO_SMOOTHING = 0.2f;       // 音频平滑系数
    
    constexpr float SCROLL_SPEED = 30.0f;          // 滚动速度（像素/秒）
    
    // 物理停止阈值
    constexpr float VELOCITY_THRESHOLD = 0.01f;
    constexpr float POSITION_THRESHOLD = 0.5f;
    
    // 动画帧率
    constexpr int FRAME_RATE = 60;
    constexpr int FRAME_TIME_MS = 16;  // ~60fps

    constexpr float WAVE_SPEED = 5.0f;              // 波浪速度
}

// ============================================
// 颜色常量 (RGBA)
// ============================================
namespace Color {
    // 背景色
    constexpr float BG_R = 0.08f;
    constexpr float BG_G = 0.08f;
    constexpr float BG_B = 0.10f;
    constexpr float BG_A = 0.8f;
    
    // 主题色（红色）
    constexpr float THEME_R = 1.0f;
    constexpr float THEME_G = 0.2f;
    constexpr float THEME_B = 0.4f;
    
    // 白色
    constexpr float WHITE_R = 1.0f;
    constexpr float WHITE_G = 1.0f;
    constexpr float WHITE_B = 1.0f;
    
    // 灰色
    constexpr float GRAY_R = 0.6f;
    constexpr float GRAY_G = 0.6f;
    constexpr float GRAY_B = 0.6f;
    
    // WiFi 绿色
    constexpr float WIFI_R = 0.1f;
    constexpr float WIFI_G = 0.8f;
    constexpr float WIFI_B = 0.3f;
    
    // 蓝牙 蓝色
    constexpr float BLUETOOTH_R = 0.2f;
    constexpr float BLUETOOTH_G = 0.5f;
    constexpr float BLUETOOTH_B = 1.0f;
    
    // 充电 亮绿色
    constexpr float CHARGING_R = 0.2f;
    constexpr float CHARGING_G = 0.85f;
    constexpr float CHARGING_B = 0.4f;
    
    // 低电量 红色
    constexpr float LOW_BATTERY_R = 1.0f;
    constexpr float LOW_BATTERY_G = 0.2f;
    constexpr float LOW_BATTERY_B = 0.2f;
    
    // 文件暂存 天蓝色
    constexpr float FILE_R = 0.1f;
    constexpr float FILE_G = 0.6f;
    constexpr float FILE_B = 1.0f;
    
    // 通知 橙红色
    constexpr float NOTIFICATION_R = 0.9f;
    constexpr float NOTIFICATION_G = 0.3f;
    constexpr float NOTIFICATION_B = 0.3f;
    
    // 深灰色背景
    constexpr float DARK_GRAY_R = 0.2f;
    constexpr float DARK_GRAY_G = 0.2f;
    constexpr float DARK_GRAY_B = 0.2f;
    
    // 进度条背景
    constexpr float PROGRESS_BG_R = 0.8f;
    constexpr float PROGRESS_BG_G = 0.8f;
    constexpr float PROGRESS_BG_B = 0.8f;
    constexpr float PROGRESS_BG_A = 0.5f;
    
    // 进度条前景
    constexpr float PROGRESS_FG_R = 1.0f;
    constexpr float PROGRESS_FG_G = 1.0f;
    constexpr float PROGRESS_FG_B = 1.0f;
}

// ============================================
// 字体常量
// ============================================
namespace Font {
    constexpr float TITLE_SIZE = 16.0f;           // 标题字体大小
    constexpr float SUBTITLE_SIZE = 13.0f;        // 副标题字体大小
    constexpr float ICON_SIZE = 18.0f;           // 图标字体大小
    
    constexpr const wchar_t* TITLE_FONT = L"Microsoft YaHei";
    constexpr const wchar_t* ICON_FONT = L"Segoe MDL2 Assets";
}

// ============================================
// 通知类型常量
// ============================================
namespace Alert {
    constexpr int TYPE_NONE = 0;
    constexpr int TYPE_WIFI = 1;
    constexpr int TYPE_BLUETOOTH = 2;
    constexpr int TYPE_NOTIFICATION = 3;
    constexpr int TYPE_CHARGING = 4;
    constexpr int TYPE_LOW_BATTERY = 5;
    constexpr int TYPE_FILE = 6;
    
    // 弹窗显示时长（毫秒）
    constexpr int DURATION_MS = 3000;
}

// ============================================
// 系统常量
// ============================================
namespace System {
    // 默认 DPI
    constexpr float DEFAULT_DPI = 96.0f;
    
    // 低电量阈值
    constexpr int LOW_BATTERY_THRESHOLD = 20;
    
    // 音量阈值（用于选择不同图标）
    constexpr float VOLUME_MUTE = 0.0f;
    constexpr float VOLUME_LOW = 0.35f;
    constexpr float VOLUME_MEDIUM = 0.65f;
}

} // namespace Constants


