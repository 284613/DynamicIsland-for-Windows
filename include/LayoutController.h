#pragma once
#include "Spring.h"
#include <windows.h>

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
    bool IsSettled() const {
        return m_widthSpring.IsSettled() && m_heightSpring.IsSettled() 
            && m_alphaSpring.IsSettled() && m_secHeightSpring.IsSettled() 
            && m_secAlphaSpring.IsSettled();
    }

    void SetTargetSize(float w, float h);
    void SetTargetAlpha(float a);
    void SetSecondaryTarget(float h, float a); // [新增] 设置副岛目标状态
    void SetSpringParams(float stiffness, float damping);
    void StartAnimation();
    void UpdatePhysics();

    // 副岛状态 [新增]
    float GetSecondaryHeight() const { return m_currentSecHeight; }
    float GetSecondaryAlpha() const { return m_currentSecAlpha; }

    // 碰撞检测（供 DynamicIsland 调用）
    int HitTestPlaybackButtons(POINT pt, bool isExpanded, bool hasSession, float canvasWidth,
        float currentWidth, float currentHeight, float dpiScale) const;
    int HitTestProgressBar(POINT pt, bool isExpanded, bool hasSession, float canvasWidth,
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

    // 副岛物理状态 [新增]
    float m_currentSecHeight = 0.0f;
    float m_targetSecHeight = 0.0f;
    float m_currentSecAlpha = 0.0f;
    float m_targetSecAlpha = 0.0f;

    Spring m_widthSpring;
    Spring m_heightSpring;
    Spring m_alphaSpring;
    Spring m_secHeightSpring; // [新增]
    Spring m_secAlphaSpring;  // [新增]
};
