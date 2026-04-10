#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "PluginManager.h"

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

enum class SecondaryContentKind
{
    None,
    Volume,
    FileMini,
    FileExpanded,
    FileDropTarget
};

struct DisplayModeEntry {
    IslandDisplayMode mode;
    int priority;
    std::function<bool()> condition;
};

using DisplayModePriorityTable = std::vector<DisplayModeEntry>;

struct LyricData {
    std::wstring text;
    int64_t currentMs;
    int64_t nextMs;
    int64_t positionMs;
};

struct RenderContext
{
    float islandWidth = 0.0f;
    float islandHeight = 0.0f;
    float canvasWidth = 0.0f;
    float contentAlpha = 0.0f;
    float secondaryHeight = 0.0f;
    float secondaryAlpha = 0.0f;
    SecondaryContentKind secondaryContent = SecondaryContentKind::None;
    IslandDisplayMode mode = IslandDisplayMode::Idle;
    ULONGLONG currentTimeMs = 0;
};
