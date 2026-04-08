// IslandState.h
#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum class IslandDisplayMode
{
    Idle,
    MusicCompact,
    MusicExpanded,
    Alert,
    Volume,
    FileDrop
};

struct LyricData {
    std::wstring text;        // 当前歌词
    int64_t currentMs;         // 当前行开始时间戳(ms)
    int64_t nextMs;           // 下一行开始时间戳(ms)
    int64_t positionMs;       // 当前播放位置(ms)
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
    LyricData lyric;

    // 音量条
    bool isVolumeActive;
    float volumeLevel;

    // 文件拖拽
    bool isDragHovering;
    size_t storedFileCount;
    std::vector<std::wstring> storedFiles;    // [新增] 存储的文件列表
    int hoveredFileIndex;                      // [新增] 悬停的文件索引
    bool isFileDeleteHovered;                  // [新增] 是否悬停在删除按钮上

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
