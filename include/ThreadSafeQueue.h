#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

// 线程安全队列模板类
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    // 禁止拷贝
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // 入队
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(item);
        m_condition.notify_one();
    }

    // 入队（移动语义）
    void push(T&& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(item));
        m_condition.notify_one();
    }

    // 出队（阻塞等待）
    T pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return !m_queue.empty(); });
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }

    // 尝试出队（非阻塞）
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        
        T item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }

    // 查看队首（非阻塞）
    std::optional<T> peek() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        return std::make_optional(m_queue.front());
    }

    // 判断是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    // 获取队列大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    // 清空队列
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
};


