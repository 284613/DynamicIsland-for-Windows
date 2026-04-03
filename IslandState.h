#pragma once

#include <windows.h>
#include <string>

enum class IslandDisplayMode
{
    Idle,
    MusicCompact,
    MusicExpanded,
    Alert,
    Volume,
    FileDrop
};

struct RenderContext
{
    // 尺寸
    float islandWidth;
    float islandHeight;
    float canvasWidth;
    float contentAlpha;

    // 音乐数据
    float audioLevel;
    std::wstring title;
    std::wstring artist;
    bool isPlaying;
    bool hasSession;

    // 时间显示
    bool showTime;
    std::wstring timeText;

    // 进度条
    float progress;
    int hoveredProgress;
    int pressedProgress;

    // 歌词
    std::wstring lyric;

    // 音量条
    bool isVolumeActive;
    float volumeLevel;

    // 文件拖拽
    bool isDragHovering;
    size_t storedFileCount;

    // 天气
    std::wstring weatherDesc;
    float weatherTemp;

    // 警报数据
    bool isAlertActive;
    int alertType;              // 1=WiFi, 2=Bluetooth, 3=App, 4=Charging, 5=LowBattery, 6=File
    std::wstring alertName;
    std::wstring alertDeviceType;

    // 当前显示模式
    IslandDisplayMode mode;
};
