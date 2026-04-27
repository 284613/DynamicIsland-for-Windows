#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "PluginManager.h"

enum class WeatherViewMode { Hourly, Daily };

enum class IslandDisplayMode
{
    Shrunk,
    Idle,
    TodoInputCompact,
    TodoListCompact,
    TodoExpanded,
    AgentCompact,
    AgentExpanded,
    PomodoroCompact,
    PomodoroExpanded,
    MusicCompact,
    MusicExpanded,
	WeatherExpanded,
    FaceUnlockFeedback,
    Alert,
    Volume,
    FileDrop
};

enum class ActiveExpandedMode
{
    None,
    Music,
    Todo,
    Pomodoro,
    Weather,
    Agent
};

enum class SecondaryContentKind
{
    None,
    Volume,
    FileCircle,
    FileSwirlDrop,
    FileMini,
    FileExpanded,
    FileDropTarget
};

enum class MusicArtworkStyle
{
    Square,
    Vinyl
};

enum class LyricTranslationMode
{
    Off,
    CurrentOnly,
    AllLines,
    TranslationOnly
};

struct DisplayModeEntry {
    IslandDisplayMode mode;
    int priority;
    std::function<bool()> condition;
};

using DisplayModePriorityTable = std::vector<DisplayModeEntry>;

struct LyricWord {
    std::wstring text;
    int64_t startMs = -1;
    int64_t durationMs = 0;
    int flag = 0;
    bool isCjk = false;
    bool endsWithSpace = false;
    bool trailing = false;
};

struct LyricData {
    std::wstring text;
    std::wstring previousText;
    std::wstring nextText;
    int64_t currentMs = -1;
    int64_t nextMs = -1;
    int64_t positionMs = 0;
    std::vector<LyricWord> words;
    bool hasExplicitWordTiming = false;
    std::wstring translation;  // 行级中文翻译（网易云 tlyric），缺失时为空
    std::wstring previousTranslation;
    std::wstring nextTranslation;
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
    bool shrinkAnimating = false;
    float shrinkProgress = 0.0f;
    IslandDisplayMode shrinkSourceMode = IslandDisplayMode::Idle;
    bool manualShrunk = false;
};
