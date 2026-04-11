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
    constexpr int WEATHER_CITY     = 40;
    constexpr int WEATHER_KEY      = 41;
    constexpr int WEATHER_LOC      = 42;
    constexpr int NOTIFY_APPS      = 50;
    constexpr int ADV_STIFFNESS    = 60;
    constexpr int ADV_DAMPING      = 61;
    constexpr int ADV_LOW_BAT      = 62;
    constexpr int ADV_FILE_MAX     = 63;
    constexpr int ADV_MEDIA_POLL   = 64;
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
