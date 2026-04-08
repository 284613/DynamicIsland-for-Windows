#include "TaskDetector.h"
#include "MediaMonitor.h"
#include "LyricsMonitor.h"
#include <algorithm>

TaskDetector& TaskDetector::Instance() {
    static TaskDetector instance;
    return instance;
}

TaskDetector::TaskDetector() {
}

TaskDetector::~TaskDetector() {
}

void TaskDetector::SetMediaMonitor(void* mediaMonitor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mediaMonitor = mediaMonitor;
}

void TaskDetector::SetLyricsMonitor(void* lyricsMonitor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lyricsMonitor = lyricsMonitor;
}

void TaskDetector::SetNotificationMonitor(void* notificationMonitor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notificationMonitor = notificationMonitor;
}

void TaskDetector::SetUpdateCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_updateCallback = callback;
}

void TaskDetector::CheckMusicTask() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_mediaMonitor) return;

    auto* mediaMonitor = static_cast<MediaMonitor*>(m_mediaMonitor);

    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
            [](const TaskInfo& task) { return task.type == TaskType::Music; }),
        m_tasks.end());

    if (mediaMonitor->IsPlaying() && mediaMonitor->HasSession()) {
        TaskInfo musicTask;
        musicTask.type = TaskType::Music;
        musicTask.title = mediaMonitor->GetTitle();
        musicTask.subtitle = mediaMonitor->GetArtist();
        musicTask.isActive = true;
        m_tasks.push_back(musicTask);
    }
}

void TaskDetector::CheckNotificationTask() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
            [](const TaskInfo& task) { return task.type == TaskType::Notification; }),
        m_tasks.end());
}

void TaskDetector::CheckLyricsTask() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_lyricsMonitor) return;

    auto* lyricsMonitor = static_cast<LyricsMonitor*>(m_lyricsMonitor);

    m_tasks.erase(
        std::remove_if(m_tasks.begin(), m_tasks.end(),
            [](const TaskInfo& task) { return task.type == TaskType::Lyrics; }),
        m_tasks.end());

    if (lyricsMonitor->HasLyrics()) {
        TaskInfo lyricsTask;
        lyricsTask.type = TaskType::Lyrics;
        lyricsTask.title = L"Lyrics";
        lyricsTask.subtitle = L"";
        lyricsTask.isActive = true;
        m_tasks.push_back(lyricsTask);
    }
}

std::vector<TaskInfo> TaskDetector::GetActiveTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);

    CheckMusicTask();
    CheckNotificationTask();
    CheckLyricsTask();

    return m_tasks;
}

int TaskDetector::GetTaskCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_tasks.size());
}


