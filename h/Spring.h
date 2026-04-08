#pragma once
#include <cmath>
#include <algorithm>

// ============================================
// 弹簧物理引擎 - 用于更自然的动画效果
// ============================================
class Spring {
public:
    Spring() : m_stiffness(100.0f), m_damping(10.0f), m_mass(1.0f) {}
    
    void SetStiffness(float s) { m_stiffness = s; }  // 刚度 (stiffness)
    void SetDamping(float d) { m_damping = d; }       // 阻尼 (damping)
    void SetMass(float m) { m_mass = m; }             // 质量
    
    // 设置目标值
    void SetTarget(float target) { m_target = target; }
    
    // 获取当前值
    [[nodiscard]] float GetValue() const { return m_value; }
    
    // 更新物理（使用半隐式欧拉积分）
    // deltaTime: 时间步长（秒）
    void Update(float deltaTime) {
        deltaTime = std::min(deltaTime, 0.05f);  // 防止睡眠唤醒后时间步长过大
        // 弹簧力 F = -k * (x - target)
        float displacement = m_value - m_target;
        float springForce = -m_stiffness * displacement;
        
        // 阻尼力 F = -c * v
        float dampingForce = -m_damping * m_velocity;
        
        // 总力 F = spring + damping
        float totalForce = springForce + dampingForce;
        
        // 加速度 a = F / m
        float acceleration = totalForce / m_mass;
        
        // 更新速度 v = v + a * dt
        m_velocity += acceleration * deltaTime;
        
        // 更新位置 x = x + v * dt
        m_value += m_velocity * deltaTime;
    }
    
    // 判断是否已稳定（接近目标且速度很小）
    [[nodiscard]] bool IsSettled(float positionThreshold = 0.5f, float velocityThreshold = 0.1f) const {
        return std::abs(m_value - m_target) < positionThreshold && 
               std::abs(m_velocity) < velocityThreshold;
    }
    
    // 立即设置到目标值（用于重置）
    void SnapToTarget() {
        m_value = m_target;
        m_velocity = 0.0f;
    }

private:
    float m_value = 0.0f;        // 当前值
    float m_velocity = 0.0f;    // 当前速度
    float m_target = 0.0f;      // 目标值
    
    // 弹簧参数
    float m_stiffness;  // 刚度 (k) - 越大回复越快
    float m_damping;    // 阻尼 (c) - 越大振荡越小
    float m_mass;       // 质量 (m) - 越大惯性越大
};

// ============================================
// 便捷构造函数 - 创建常用弹簧
// ============================================
namespace SpringFactory {
    // 创建弹性弹簧（快速响应，少量振荡）
    inline Spring CreateBouncy() {
        Spring s;
        s.SetStiffness(200.0f);
        s.SetDamping(8.0f);
        s.SetMass(1.0f);
        return s;
    }
    
    // 创建平滑弹簧（缓慢响应，无振荡）
    inline Spring CreateSmooth() {
        Spring s;
        s.SetStiffness(50.0f);
        s.SetDamping(20.0f);
        s.SetMass(1.0f);
        return s;
    }
    
    // 创建弹性适中的弹簧（默认）
    inline Spring CreateDefault() {
        Spring s;
        s.SetStiffness(100.0f);
        s.SetDamping(12.0f);
        s.SetMass(1.0f);
        return s;
    }
}


