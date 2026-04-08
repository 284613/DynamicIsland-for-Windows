#pragma once
#include "Spring.h"
#include <windows.h>
#include <vector>
#include <string>

class LayoutController {
public:
    LayoutController();

    // 尺寸状态
    float GetCurrentWidth() const { return m_currentWidth; }
    float GetCurrentHeight() const { return m_currentHeight; }
    float GetTargetWidth() const { return m_targetWidth; }
    float GetTargetHeight() const { return m_targetHeight; }
    float GetCurrentAlpha() const { return m_currentAlpha; }
    float GetTargetAlpha() const { return m_targetAlpha; }
    bool IsAnimating() const { return m_isAnimating; }

    void SetTargetSize(float w, float h);
    void SetTargetAlpha(float a);
    void StartAnimation();
    void UpdatePhysics();

    // 碰撞检测（供 DynamicIsland 调用）
    int HitTestPlaybackButtons(POINT pt, bool isExpanded, bool hasSession, float canvasWidth,
        float currentWidth, float currentHeight, float dpiScale) const;
    int HitTestProgressBar(POINT pt, bool isExpanded, bool hasSession, float canvasWidth,
        float currentWidth, float currentHeight, float dpiScale) const;

    // 文件面板碰撞检测
    int HitTestFileItem(POINT pt, const std::vector<std::wstring>& files, float canvasWidth,
        float currentWidth, float currentHeight, float dpiScale) const;
    bool HitTestFileDelete(POINT pt, int fileIndex, float canvasWidth,
        float currentWidth, float currentHeight, float dpiScale) const;

    // 布局矩形（供 RenderEngine 调用）
    struct LayoutRects {
        float albumArtLeft, albumArtTop, albumArtSize;
        float buttonAreaLeft;
        float progressBarLeft, progressBarTop, progressBarWidth;
    };
    LayoutRects ComputeLayout(float currentWidth, float currentHeight, float dpiScale) const;

private:
    float m_currentWidth = 0.0f;
    float m_currentHeight = 0.0f;
    float m_targetWidth = 0.0f;
    float m_targetHeight = 0.0f;
    float m_currentAlpha = 1.0f;
    float m_targetAlpha = 1.0f;
    bool m_isAnimating = false;

    Spring m_widthSpring;
    Spring m_heightSpring;
    Spring m_alphaSpring;
};
