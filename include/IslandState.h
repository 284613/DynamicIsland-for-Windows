// IslandState.h
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

enum class WeatherViewMode { Hourly, Daily };

enum class IslandDisplayMode
{
    Idle,
    MusicCompact,
    MusicExpanded,
    WeatherExpanded,
    Alert,
    Volume,
    FileDrop
};

// Display mode priority entry for configurable scheduling
struct DisplayModeEntry {
    IslandDisplayMode mode;
    int priority;                           // Higher number = higher priority
    std::function<bool()> condition;       // Runtime condition check
};

using DisplayModePriorityTable = std::vector<DisplayModeEntry>;

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

    // 副岛动画状态 [新增]
    float secondaryHeight;
    float secondaryAlpha;

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

    // 天气
    std::wstring weatherDesc;
    float weatherTemp;
    std::wstring weatherIconId;
    struct HourlyForecast {
        std::wstring time;
        std::wstring icon;
        std::wstring text;
        float temp;
    };
    std::vector<HourlyForecast> hourlyForecasts;
    struct DailyForecast {
        std::wstring date;
        std::wstring iconDay;
        std::wstring textDay;
        float tempMax;
        float tempMin;
    };
    std::vector<DailyForecast> dailyForecasts;
    WeatherViewMode weatherViewMode = WeatherViewMode::Hourly;

    // 当前显示模式
    IslandDisplayMode mode;
};
