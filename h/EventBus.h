#pragma once
#include <windows.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <any>

// ============================================
// 统一事件驱动架构 - EventBus 系统
// ============================================

// 事件类型枚举
enum class EventType {
    // 电源事件
    PowerChange,
    
    // 媒体事件
    MediaSessionChanged,
    MediaPlaybackStateChanged,
    MediaMetadataChanged,
    MediaProgressChanged,
    
    // 系统事件
    SystemTimeChanged,
    VolumeChanged,
    
    // UI事件
    WindowStateChanged,
    HoverStateChanged,
    DragStateChanged,
    
    // 通知事件
    NotificationArrived,
    NotificationRemoved,
    
    // 连接事件
    NetworkStatusChanged,
    BluetoothStatusChanged,
    
    // 自定义事件
    Custom
};

// 事件数据结构（可扩展）
struct Event {
    EventType type;
    WPARAM wParam;
    LPARAM lParam;
    std::any userData;  // 类型安全的附加数据，替代 void*

    Event(EventType t = EventType::Custom, WPARAM w = 0, LPARAM l = 0)
        : type(t), wParam(w), lParam(l) {}
};

// 事件回调函数类型
using EventCallback = std::function<void(const Event&)>;

// 订阅者结构
struct Subscriber {
    int id;
    EventCallback callback;
};

// ============================================
// EventBus 类 - 事件总线核心
// ============================================
class EventBus {
public:
    // 获取单例实例
    static EventBus& GetInstance() {
        static EventBus instance;
        return instance;
    }
    
    // 订阅事件
    // 返回订阅ID，可用于取消订阅
    int Subscribe(EventType type, EventCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        int subscriberId = ++m_nextId;
        Subscriber sub;
        sub.id = subscriberId;
        sub.callback = std::move(callback);
        
        m_subscribers[type].push_back(sub);
        
        return subscriberId;
    }
    
    // 取消订阅
    bool Unsubscribe(EventType type, int subscriberId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_subscribers.find(type);
        if (it == m_subscribers.end()) return false;
        
        auto& vec = it->second;
        for (auto iter = vec.begin(); iter != vec.end(); ++iter) {
            if (iter->id == subscriberId) {
                vec.erase(iter);
                return true;
            }
        }
        return false;
    }
    
    // 发布事件（同步调用所有订阅者）
    void Publish(const Event& event) {
        std::vector<Subscriber> subscribersCopy;
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            auto it = m_subscribers.find(event.type);
            if (it == m_subscribers.end()) return;
            
            // 复制一份订阅者列表，避免在回调中修改导致问题
            subscribersCopy = it->second;
        }
        
        // 在锁外调用回调
        for (const auto& sub : subscribersCopy) {
            try {
                sub.callback(event);
            } catch (...) {
                // 忽略回调中的异常
            }
        }
    }
    
    // 便捷方法：发布特定类型事件
    void PublishPowerChange(BYTE acStatus, BYTE batteryPct) {
        Event e(EventType::PowerChange);
        e.wParam = acStatus;
        e.lParam = batteryPct;
        Publish(e);
    }
    
    void PublishMediaSessionChanged(bool hasSession) {
        Event e(EventType::MediaSessionChanged);
        e.wParam = hasSession ? 1 : 0;
        Publish(e);
    }
    
    void PublishMediaPlaybackStateChanged(bool isPlaying) {
        Event e(EventType::MediaPlaybackStateChanged);
        e.wParam = isPlaying ? 1 : 0;
        Publish(e);
    }
    
    void PublishVolumeChanged(float volume) {
        Event e(EventType::VolumeChanged);
        // 将float volume打包到wParam
        e.wParam = (WPARAM)(volume * 100);  // 使用0-100表示
        Publish(e);
    }
    
    void PublishNotificationArrived(const AlertInfo& info) {
        Event e(EventType::NotificationArrived);
        e.userData = info;  // 直接值拷贝，无需 raw new，无泄漏风险
        Publish(e);
        // 订阅者通过 std::any_cast<AlertInfo>(event.userData) 读取
    }

    void PublishNetworkStatusChanged(bool connected, const std::wstring& ssid = L"") {
        Event e(EventType::NetworkStatusChanged);
        e.wParam = connected ? 1 : 0;
        if (!ssid.empty()) {
            e.userData = ssid;  // ssid 直接存入 userData
        }
        Publish(e);
    }
    
    // 清空所有订阅
    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscribers.clear();
    }

private:
    EventBus() : m_nextId(0) {}
    ~EventBus() = default;
    
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    
    std::unordered_map<EventType, std::vector<Subscriber>> m_subscribers;
    std::mutex m_mutex;
    int m_nextId;
};

// ============================================
// 便捷宏定义
// ============================================
#define EVENT_SUBSCRIBE(type, callback) EventBus::GetInstance().Subscribe(EventType::type, callback)
#define EVENT_UNSUBSCRIBE(type, id) EventBus::GetInstance().Unsubscribe(EventType::type, id)
#define EVENT_PUBLISH(event) EventBus::GetInstance().Publish(event)


