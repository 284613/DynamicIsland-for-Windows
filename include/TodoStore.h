#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class TodoPriority {
    High,
    Medium,
    Low
};

struct TodoItem {
    uint64_t id = 0;
    std::wstring title;
    std::wstring note;
    TodoPriority priority = TodoPriority::Medium;
    bool completed = false;
    uint64_t createdAt = 0;
    uint64_t updatedAt = 0;
};

class TodoStore {
public:
    bool Load();
    bool Save() const;

    const std::vector<TodoItem>& Items() const { return m_items; }
    bool HasItems() const { return !m_items.empty(); }
    size_t CountIncomplete() const;
    const TodoItem* GetTopIncomplete() const;
    std::vector<const TodoItem*> GetSortedItems() const;

    uint64_t AddItem(const std::wstring& title,
        const std::wstring& note = L"",
        TodoPriority priority = TodoPriority::Medium);
    bool UpdateItem(uint64_t id, const std::wstring& title, const std::wstring& note, TodoPriority priority);
    bool SetCompleted(uint64_t id, bool completed);
    bool RemoveItem(uint64_t id);

    TodoItem* FindItem(uint64_t id);
    const TodoItem* FindItem(uint64_t id) const;

    std::filesystem::path GetStoragePath() const;

private:
    std::vector<TodoItem> m_items;
    uint64_t m_nextId = 1;
};

int TodoPriorityRank(TodoPriority priority);
std::wstring TodoPriorityLabel(TodoPriority priority);
