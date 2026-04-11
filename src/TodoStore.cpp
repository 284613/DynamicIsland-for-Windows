#include "TodoStore.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Data::Json;

namespace {
std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), len, nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::wstring EscapeJson(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 8);

    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\': escaped += L"\\\\"; break;
        case L'"': escaped += L"\\\""; break;
        case L'\n': escaped += L"\\n"; break;
        case L'\r': escaped += L"\\r"; break;
        case L'\t': escaped += L"\\t"; break;
        default: escaped += ch; break;
        }
    }

    return escaped;
}

uint64_t CurrentTimestamp() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

std::filesystem::path GetTodoDirectory() {
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::filesystem::path dir = std::filesystem::path(localAppData) / L"DynamicIsland";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::filesystem::path fallback = std::filesystem::temp_directory_path() / L"DynamicIsland";
    std::error_code ec;
    std::filesystem::create_directories(fallback, ec);
    return fallback;
}
}

int TodoPriorityRank(TodoPriority priority) {
    switch (priority) {
    case TodoPriority::High: return 3;
    case TodoPriority::Medium: return 2;
    case TodoPriority::Low:
    default:
        return 1;
    }
}

std::wstring TodoPriorityLabel(TodoPriority priority) {
    switch (priority) {
    case TodoPriority::High: return L"High";
    case TodoPriority::Medium: return L"Medium";
    case TodoPriority::Low:
    default:
        return L"Low";
    }
}

std::filesystem::path TodoStore::GetStoragePath() const {
    return GetTodoDirectory() / L"todos.json";
}

bool TodoStore::Load() {
    m_items.clear();
    m_nextId = 1;

    const std::filesystem::path path = GetStoragePath();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return true;
    }

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return true;
    }

    try {
        JsonObject root = JsonObject::Parse(winrt::hstring(Utf8ToWide(bytes)));
        if (root.HasKey(L"nextId")) {
            m_nextId = static_cast<uint64_t>(root.GetNamedValue(L"nextId").GetNumber());
        }

        if (!root.HasKey(L"items")) {
            return true;
        }

        JsonArray items = root.GetNamedArray(L"items");
        for (auto const& value : items) {
            JsonObject obj = value.as<JsonObject>();
            TodoItem item;

            if (obj.HasKey(L"id")) item.id = static_cast<uint64_t>(obj.GetNamedValue(L"id").GetNumber());
            if (obj.HasKey(L"title")) item.title = obj.GetNamedString(L"title").c_str();
            if (obj.HasKey(L"note")) item.note = obj.GetNamedString(L"note").c_str();
            if (obj.HasKey(L"priority")) {
                const std::wstring priority = obj.GetNamedString(L"priority").c_str();
                if (priority == L"High") item.priority = TodoPriority::High;
                else if (priority == L"Low") item.priority = TodoPriority::Low;
                else item.priority = TodoPriority::Medium;
            }
            if (obj.HasKey(L"completed")) item.completed = obj.GetNamedBoolean(L"completed");
            if (obj.HasKey(L"createdAt")) item.createdAt = static_cast<uint64_t>(obj.GetNamedValue(L"createdAt").GetNumber());
            if (obj.HasKey(L"updatedAt")) item.updatedAt = static_cast<uint64_t>(obj.GetNamedValue(L"updatedAt").GetNumber());

            if (item.id == 0 || item.title.empty()) {
                continue;
            }

            m_nextId = (std::max)(m_nextId, item.id + 1);
            m_items.push_back(std::move(item));
        }
    } catch (...) {
        m_items.clear();
        m_nextId = 1;
        return false;
    }

    return true;
}

bool TodoStore::Save() const {
    std::wostringstream out;
    out << L"{\n";
    out << L"  \"version\": 1,\n";
    out << L"  \"nextId\": " << m_nextId << L",\n";
    out << L"  \"items\": [\n";

    for (size_t i = 0; i < m_items.size(); ++i) {
        const TodoItem& item = m_items[i];
        out << L"    {\n";
        out << L"      \"id\": " << item.id << L",\n";
        out << L"      \"title\": \"" << EscapeJson(item.title) << L"\",\n";
        out << L"      \"note\": \"" << EscapeJson(item.note) << L"\",\n";
        out << L"      \"priority\": \"" << TodoPriorityLabel(item.priority) << L"\",\n";
        out << L"      \"completed\": " << (item.completed ? L"true" : L"false") << L",\n";
        out << L"      \"createdAt\": " << item.createdAt << L",\n";
        out << L"      \"updatedAt\": " << item.updatedAt << L"\n";
        out << L"    }";
        if (i + 1 < m_items.size()) {
            out << L",";
        }
        out << L"\n";
    }

    out << L"  ]\n";
    out << L"}\n";

    const std::filesystem::path path = GetStoragePath();
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path tempPath = path.wstring() + L".tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        const std::string utf8 = WideToUtf8(out.str());
        file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        if (!file.good()) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            return false;
        }
    }

    return true;
}

size_t TodoStore::CountIncomplete() const {
    return static_cast<size_t>(std::count_if(m_items.begin(), m_items.end(), [](const TodoItem& item) {
        return !item.completed;
        }));
}

const TodoItem* TodoStore::GetTopIncomplete() const {
    auto sorted = GetSortedItems();
    for (const TodoItem* item : sorted) {
        if (item && !item->completed) {
            return item;
        }
    }
    return nullptr;
}

std::vector<const TodoItem*> TodoStore::GetSortedItems() const {
    std::vector<const TodoItem*> items;
    items.reserve(m_items.size());
    for (const auto& item : m_items) {
        items.push_back(&item);
    }

    std::sort(items.begin(), items.end(), [](const TodoItem* lhs, const TodoItem* rhs) {
        if (lhs->completed != rhs->completed) {
            return !lhs->completed;
        }
        const int lhsPriority = TodoPriorityRank(lhs->priority);
        const int rhsPriority = TodoPriorityRank(rhs->priority);
        if (lhsPriority != rhsPriority) {
            return lhsPriority > rhsPriority;
        }
        if (lhs->updatedAt != rhs->updatedAt) {
            return lhs->updatedAt > rhs->updatedAt;
        }
        return lhs->id > rhs->id;
        });

    return items;
}

uint64_t TodoStore::AddItem(const std::wstring& title, const std::wstring& note, TodoPriority priority) {
    if (title.empty()) {
        return 0;
    }

    TodoItem item;
    item.id = m_nextId++;
    item.title = title;
    item.note = note;
    item.priority = priority;
    item.completed = false;
    item.createdAt = CurrentTimestamp();
    item.updatedAt = item.createdAt;
    m_items.push_back(item);

    if (!Save()) {
        m_items.pop_back();
        --m_nextId;
        return 0;
    }

    return item.id;
}

bool TodoStore::UpdateItem(uint64_t id, const std::wstring& title, const std::wstring& note, TodoPriority priority) {
    TodoItem* item = FindItem(id);
    if (!item || title.empty()) {
        return false;
    }

    TodoItem before = *item;
    item->title = title;
    item->note = note;
    item->priority = priority;
    item->updatedAt = CurrentTimestamp();

    if (!Save()) {
        *item = before;
        return false;
    }

    return true;
}

bool TodoStore::SetCompleted(uint64_t id, bool completed) {
    TodoItem* item = FindItem(id);
    if (!item) {
        return false;
    }

    const bool before = item->completed;
    const uint64_t beforeUpdated = item->updatedAt;
    item->completed = completed;
    item->updatedAt = CurrentTimestamp();

    if (!Save()) {
        item->completed = before;
        item->updatedAt = beforeUpdated;
        return false;
    }

    return true;
}

bool TodoStore::RemoveItem(uint64_t id) {
    auto it = std::find_if(m_items.begin(), m_items.end(), [id](const TodoItem& item) {
        return item.id == id;
        });
    if (it == m_items.end()) {
        return false;
    }

    const TodoItem removed = *it;
    m_items.erase(it);
    if (!Save()) {
        m_items.push_back(removed);
        return false;
    }
    return true;
}

TodoItem* TodoStore::FindItem(uint64_t id) {
    auto it = std::find_if(m_items.begin(), m_items.end(), [id](const TodoItem& item) {
        return item.id == id;
        });
    return it == m_items.end() ? nullptr : &(*it);
}

const TodoItem* TodoStore::FindItem(uint64_t id) const {
    auto it = std::find_if(m_items.begin(), m_items.end(), [id](const TodoItem& item) {
        return item.id == id;
        });
    return it == m_items.end() ? nullptr : &(*it);
}
