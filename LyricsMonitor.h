//LyricsMonitor.h
#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

struct LyricLine {
    int64_t timestamp;  // 毫秒
    std::wstring text;
};

class LyricsMonitor {
public:
    LyricsMonitor();
    ~LyricsMonitor();

    bool Initialize(HWND hwnd);
    void Shutdown();

    // 更新当前歌曲，触发异步获取歌词
    void UpdateSong(const std::wstring& title, const std::wstring& artist);

    // 根据播放位置（毫秒）获取当前歌词
    std::wstring GetCurrentLyric(int64_t positionMs);

    std::vector<LyricLine> GetAllLyrics() const;
    bool HasLyrics() const;
    void ClearLyrics();

private:
    // 异步获取线程
    void FetchLyricsThread(std::wstring title, std::wstring artist);

    // 网络请求函数（私有，但可在类内访问）
    std::wstring SearchSong(const std::wstring& keyword, const std::wstring& searchArtist);
    std::wstring FetchLyricsFromNetEase(const std::wstring& songId);

    // 解析LRC（静态）
    static std::vector<LyricLine> ParseLrc(const std::string& lrcContent);

    // 本地文件辅助（静态）
    static std::wstring GetExeDirectory();
    static bool LoadLrcFromFile(const std::wstring& filePath, std::vector<LyricLine>& outLyrics);

private:
    // Pimpl 隐藏 WinRT 实现
    struct Impl;
    std::unique_ptr<Impl> pImpl;

    // 常规成员
    mutable std::mutex m_lyricsMutex;
    std::vector<LyricLine> m_lyrics;
    std::wstring m_currentSongTitle;
    std::wstring m_currentSongArtist;
    HWND m_hwnd = nullptr;
    std::atomic<bool> m_isFetching = false;
    std::thread m_fetchThread;
};


