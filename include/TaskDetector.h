#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

enum class TaskType {
    None,
    Music,
    Notification,
    Lyrics
};

struct TaskInfo {
    TaskType type;
    std::wstring title;
    std::wstring subtitle;
    bool isActive;
};

class TaskDetector {
public:
    static TaskDetector& Instance();

    std::vector<TaskInfo> GetActiveTasks();
    int GetTaskCount();
    void SetUpdateCallback(std::function<void()> callback);

    void SetMediaMonitor(void* mediaMonitor);
    void SetLyricsMonitor(void* lyricsMonitor);
    void SetNotificationMonitor(void* notificationMonitor);

private:
    TaskDetector();
    ~TaskDetector();

    void CheckMusicTask();
    void CheckNotificationTask();
    void CheckLyricsTask();

    void* m_mediaMonitor = nullptr;
    void* m_lyricsMonitor = nullptr;
    void* m_notificationMonitor = nullptr;

    std::vector<TaskInfo> m_tasks;
    std::mutex m_mutex;
    std::function<void()> m_updateCallback;
};


