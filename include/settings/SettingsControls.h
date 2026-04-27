#pragma once

#include <d2d1_1.h>
#include <string>
#include "Spring.h"

enum class ControlKind {
    Label,
    SubLabel,
    Toggle,
    Slider,
    Button,
    Card,
    Separator,
    TextInput
};

namespace SettingID {
    constexpr int DARK_MODE        = 1;
    constexpr int FOLLOW_SYSTEM    = 2;
    constexpr int AUTOSTART        = 3;
    constexpr int MAIN_WIDTH       = 20;
    constexpr int MAIN_HEIGHT      = 21;
    constexpr int MAIN_ALPHA       = 22;
    constexpr int FILE_WIDTH       = 30;
    constexpr int FILE_HEIGHT      = 31;
    constexpr int FILE_ALPHA       = 32;
    constexpr int COMPACT_MUSIC_TOGGLE = 33;
    constexpr int COMPACT_MUSIC_UP     = 34;
    constexpr int COMPACT_MUSIC_DOWN   = 35;
    constexpr int COMPACT_POMO_TOGGLE  = 36;
    constexpr int COMPACT_POMO_UP      = 37;
    constexpr int COMPACT_POMO_DOWN    = 38;
    constexpr int COMPACT_TODO_TOGGLE  = 39;
    constexpr int WEATHER_CITY     = 40;
    constexpr int WEATHER_KEY      = 41;
    constexpr int WEATHER_LOC      = 42;
    constexpr int WEATHER_AUTO_LOC = 43;
    constexpr int WEATHER_REGION   = 44;
    constexpr int COMPACT_TODO_UP      = 45;
    constexpr int COMPACT_TODO_DOWN    = 46;
    constexpr int COMPACT_AGENT_TOGGLE = 47;
    constexpr int COMPACT_AGENT_UP     = 48;
    constexpr int COMPACT_AGENT_DOWN   = 49;
    constexpr int NOTIFY_APPS      = 50;
    constexpr int ADV_STIFFNESS    = 60;
    constexpr int ADV_DAMPING      = 61;
    constexpr int ADV_LOW_BAT      = 62;
    constexpr int ADV_FILE_MAX     = 63;
    constexpr int ADV_MEDIA_POLL   = 64;
    constexpr int CLAUDE_INSTALL   = 70;
    constexpr int CLAUDE_REINSTALL = 71;
    constexpr int CLAUDE_UNINSTALL = 72;
    constexpr int CODEX_INSTALL    = 73;
    constexpr int CODEX_REINSTALL  = 74;
    constexpr int CODEX_UNINSTALL  = 75;
    constexpr int MUSIC_COMPACT_ART_SQUARE   = 76;
    constexpr int MUSIC_COMPACT_ART_VINYL    = 77;
    constexpr int MUSIC_EXPANDED_ART_SQUARE  = 78;
    constexpr int MUSIC_EXPANDED_ART_VINYL   = 79;
    constexpr int MUSIC_TRANSLATION_OFF      = 86;
    constexpr int MUSIC_TRANSLATION_CURRENT  = 87;
    constexpr int MUSIC_TRANSLATION_ALL      = 88;
    constexpr int MUSIC_TRANSLATION_ONLY     = 89;
    constexpr int MUSIC_VINYL_RING_PULSE     = 90;
    constexpr int FACE_ENABLE      = 80;
    constexpr int FACE_ENROLL      = 81;
    constexpr int FACE_DELETE      = 82;
    constexpr int FACE_AUTOSTART   = 83;
    constexpr int FACE_PERF_MODE   = 84;
    constexpr int FACE_UPDATE_PASSWORD = 85;
    constexpr int BTN_RESET        = 99;
    constexpr int BTN_SAVE         = 100;
    constexpr int BTN_APPLY        = 101;
}

struct SettingsControl {
    ControlKind kind = ControlKind::Label;
    D2D1_RECT_F bounds = D2D1::RectF();
    std::wstring text;
    std::wstring subtitle;
    std::wstring placeholder;
    std::wstring valueText;

    float value = 0.0f;
    float animatedValue = 0.0f;
    Spring spring = SpringFactory::CreateDefault();
    Spring hoverSpring = SpringFactory::CreateSmooth();

    bool enabled = true;
    bool hovered = false;
    bool isPrimary = false;
    bool editing = false;
    int id = 0;

    float minVal = 0.0f;
    float maxVal = 1.0f;
    std::wstring suffix;

    std::wstring inputText;
    int cursorPos = 0;

    float GetRawValue() const {
        return minVal + value * (maxVal - minVal);
    }

    void SetRawValue(float raw) {
        if (maxVal > minVal) {
            value = (raw - minVal) / (maxVal - minVal);
        }
    }
};
