//LyricsMonitor.cpp
#include "LyricsMonitor.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <shlwapi.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <winrt/Windows.Web.Http.Headers.h>      // 必须

// ... 其他头文件
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;
using namespace Windows::Data::Json;
using namespace Windows::Storage::Streams;

// 辅助函数：宽字符与 UTF-8 互转
static std::string WideToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

static std::wstring UTF8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &result[0], size);
    return result;
}

// ---------- Pimpl 实现结构 ----------
struct LyricsMonitor::Impl {

};

// ---------- 类成员函数实现 ----------
LyricsMonitor::LyricsMonitor() : pImpl(std::make_unique<Impl>()) {

}

LyricsMonitor::~LyricsMonitor() {
    if (m_fetchThread.joinable())
        m_fetchThread.join();
}

bool LyricsMonitor::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    return true;
}

void LyricsMonitor::Shutdown() {
    if (m_fetchThread.joinable())
        m_fetchThread.join();
}

void LyricsMonitor::UpdateSong(const std::wstring& title, const std::wstring& artist) {
    std::lock_guard<std::mutex> lock(m_lyricsMutex);

    // 只做轻度清理，保留更多原始信息用于搜索
    std::wstring cleanTitle = title;
    std::wstring cleanArtist = artist;

    // 只移除首尾空格
    size_t start = cleanTitle.find_first_not_of(L" \t\r\n");
    size_t end = cleanTitle.find_last_not_of(L" \t\r\n");
    if (start != std::wstring::npos && end != std::wstring::npos) {
        cleanTitle = cleanTitle.substr(start, end - start + 1);
    }

    // 同样清理艺术家名称
    start = cleanArtist.find_first_not_of(L" \t\r\n");
    end = cleanArtist.find_last_not_of(L" \t\r\n");
    if (start != std::wstring::npos && end != std::wstring::npos) {
        cleanArtist = cleanArtist.substr(start, end - start + 1);
    }

    // 比较清理后的歌曲信息
    if (cleanTitle == m_currentSongTitle && cleanArtist == m_currentSongArtist)
        return;

    m_currentSongTitle = cleanTitle;
    m_currentSongArtist = cleanArtist;
    m_lyrics.clear();

    if (m_fetchThread.joinable())
        m_fetchThread.join();

    m_isFetching = true;
    m_fetchThread = std::thread(&LyricsMonitor::FetchLyricsThread, this, cleanTitle, cleanArtist);
}

std::wstring LyricsMonitor::GetCurrentLyric(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    if (m_lyrics.empty())
        return L"";

    // 添加时间偏移量：提前500毫秒显示歌词，解决慢半拍问题
    // 你可以根据实际情况调整这个值（比如300ms, 500ms, 800ms）
    const int64_t offsetMs = 1000;
    int64_t adjustedPosition = positionMs + offsetMs;

    // 确保调整后的位置不会小于0
    if (adjustedPosition < 0) adjustedPosition = 0;

    // 二分查找
    auto it = std::upper_bound(m_lyrics.begin(), m_lyrics.end(), adjustedPosition,
        [](int64_t ts, const LyricLine& line) { return ts < line.timestamp; });

    static int64_t lastPosition = -1;
    static std::wstring lastLyric = L"";

    // 只在位置变化较大时输出调试信息（避免刷屏）
    if (abs(positionMs - lastPosition) > 1000) {
        lastPosition = positionMs;

        if (it != m_lyrics.begin()) {
            --it;
            std::wstring currentLyric = it->text;
            if (currentLyric != lastLyric) {
                lastLyric = currentLyric;
                // OutputDebugStringW((L"Position: " + std::to_wstring(positionMs) + 
                //                    L"ms (adjusted: +" + std::to_wstring(offsetMs) + 
                //                    L"ms), Lyric: [" + std::to_wstring(it->timestamp) + 
                //                    L"ms] " + currentLyric + L"\n").c_str());
            }
            return currentLyric;
        }
    }
    else {
        if (it != m_lyrics.begin()) {
            --it;
            return it->text;
        }
    }
    return L"";
}

std::vector<LyricLine> LyricsMonitor::GetAllLyrics() const {
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    return m_lyrics;
}

bool LyricsMonitor::HasLyrics() const {
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    return !m_lyrics.empty();
}

void LyricsMonitor::ClearLyrics() {
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    m_lyrics.clear();
    m_currentSongTitle.clear();
    m_currentSongArtist.clear();
}

// ---------- 网络获取 ----------
// 辅助函数：检查歌手是否匹配（简单模糊匹配）
static bool IsArtistMatch(const std::wstring& searchArtist, const std::wstring& songArtist) {
    if (searchArtist.empty() || searchArtist == L"系统") {
        return true; // 如果没有指定搜索歌手，就认为匹配
    }

    // 简单的包含匹配
    return songArtist.find(searchArtist) != std::wstring::npos ||
        searchArtist.find(songArtist) != std::wstring::npos;
}


LyricsMonitor::LyricData LyricsMonitor::GetLyricData(int64_t positionMs) {
    LyricData data = {L"", -1, -1, positionMs};
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    if (m_lyrics.empty())
        return data;

    const int64_t offsetMs = 1000;
    int64_t adjustedPosition = positionMs + offsetMs;
    if (adjustedPosition < 0) adjustedPosition = 0;

    auto it = std::upper_bound(m_lyrics.begin(), m_lyrics.end(), adjustedPosition,
        [](int64_t ts, const LyricLine& line) { return ts < line.timestamp; });

    if (it != m_lyrics.begin()) {
        --it;
        data.text = it->text;
        data.currentMs = it->timestamp;
        // 下一行
        auto nextIt = std::next(it);
        if (nextIt != m_lyrics.end())
            data.nextMs = nextIt->timestamp;
    }
    return data;
}

std::wstring LyricsMonitor::SearchSong(const std::wstring& keyword, const std::wstring& searchArtist) {
    try {
        HttpClient httpClient;
        // 使用更新的API端点
        Uri uri(L"https://music.163.com/api/cloudsearch/pc");
        HttpRequestMessage request(HttpMethod::Post(), uri);

        // 添加必要的请求头
        request.Headers().Append(L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        request.Headers().Append(L"Referer", L"https://music.163.com/");
        request.Headers().Append(L"Origin", L"https://music.163.com");

        // 构建搜索参数
        std::wstring formBody = L"s=" + keyword + L"&type=1&limit=10"; // 增加搜索结果数量
        HttpStringContent content(formBody, UnicodeEncoding::Utf8, L"application/x-www-form-urlencoded");
        content.Headers().ContentType(HttpMediaTypeHeaderValue(L"application/x-www-form-urlencoded; charset=utf-8"));
        request.Content(content);

        auto response = httpClient.SendRequestAsync(request).get();
        auto responseBody = response.Content().ReadAsStringAsync().get();

        JsonObject json = JsonObject::Parse(responseBody);
        auto result = json.GetNamedObject(L"result");
        auto songs = result.GetNamedArray(L"songs");

        // 遍历搜索结果，找到歌手匹配的歌曲
        for (uint32_t i = 0; i < songs.Size(); i++) {
            auto song = songs.GetObjectAt(i);

            // 获取歌曲的歌手信息
            std::wstring songArtist = L"";
            if (song.HasKey(L"ar")) {
                auto artists = song.GetNamedArray(L"ar");
                if (artists.Size() > 0) {
                    auto firstArtist = artists.GetObjectAt(0);
                    if (firstArtist.HasKey(L"name")) {
                        songArtist = firstArtist.GetNamedString(L"name").c_str();
                    }
                }
            }

            // OutputDebugStringW((L"Result " + std::to_wstring(i) + 
             //                   L": ID=" + std::to_wstring((int64_t)song.GetNamedNumber(L"id")) + 
              //                  L", Artist=" + songArtist + L"\n").c_str());

             // 检查歌手是否匹配
            if (IsArtistMatch(searchArtist, songArtist)) {
                double id = song.GetNamedNumber(L"id");
                // OutputDebugStringW((L"Artist matched! Using song ID: " + std::to_wstring((int64_t)id) + L"\n").c_str());
                return std::to_wstring((int64_t)id);
            }
        }

        // 如果没有找到歌手匹配的，就返回第一个结果
        if (songs.Size() > 0) {
            auto firstSong = songs.GetObjectAt(0);
            double id = firstSong.GetNamedNumber(L"id");
            // OutputDebugStringW((L"No artist match, using first result: " + std::to_wstring((int64_t)id) + L"\n").c_str());
            return std::to_wstring((int64_t)id);
        }
    }
    catch (winrt::hresult_error const& ) {
        // OutputDebugStringW((L"SearchSong error: " + std::wstring(e.message().c_str()) + L"\n").c_str());
    }
    return L"";
}

std::wstring LyricsMonitor::FetchLyricsFromNetEase(const std::wstring& songId) {
    try {
        HttpClient httpClient;
        std::wstring url = L"https://music.163.com/api/song/lyric?id=" + songId + L"&lv=1&kv=1&tv=-1";
        Uri uri(url);
        HttpRequestMessage request(HttpMethod::Get(), uri);

        // 添加必要的请求头
        request.Headers().Append(L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        request.Headers().Append(L"Referer", L"https://music.163.com/");

        auto response = httpClient.SendRequestAsync(request).get();
        auto responseBody = response.Content().ReadAsStringAsync().get();

        JsonObject json = JsonObject::Parse(responseBody);

        // 先检查是否有lrc字段
        if (json.HasKey(L"lrc")) {
            auto lrc = json.GetNamedObject(L"lrc");
            if (lrc.HasKey(L"lyric")) {
                auto lyric = lrc.GetNamedString(L"lyric");
                return lyric.c_str();
            }
        }

        // 如果没有lrc，检查是否有klyric（逐字歌词）
        if (json.HasKey(L"klyric")) {
            auto klyric = json.GetNamedObject(L"klyric");
            if (klyric.HasKey(L"lyric")) {
                auto lyric = klyric.GetNamedString(L"lyric");
                return lyric.c_str();
            }
        }
    }
    catch (winrt::hresult_error const& e) {
        OutputDebugStringW((L"FetchLyrics error: " + std::wstring(e.message().c_str()) + L"\n").c_str());
    }
    return L"";
}

// ---------- 解析LRC ----------
std::vector<LyricLine> LyricsMonitor::ParseLrc(const std::string& lrcContent) {
    std::vector<LyricLine> result;
    std::istringstream iss(lrcContent);
    std::string line;
    std::regex timeRegex(R"(\[(\d+):(\d+\.?\d*)\])");

    int lineCount = 0;
    while (std::getline(iss, line)) {
        lineCount++;
        std::smatch match;
        std::string::const_iterator searchStart(line.cbegin());
        std::vector<int64_t> timestamps;

        while (std::regex_search(searchStart, line.cend(), match, timeRegex)) {
            int minutes = std::stoi(match[1]);
            double seconds = std::stod(match[2]);
            int64_t ms = (int64_t)((minutes * 60 + seconds) * 1000);
            timestamps.push_back(ms);
            searchStart = match.suffix().first;
        }

        if (timestamps.empty()) continue;

        std::string lyricText = std::string(searchStart, line.cend());
        if (!lyricText.empty() && lyricText[0] == ' ')
            lyricText.erase(0, 1);

        // 跳过空歌词行
        if (lyricText.empty()) continue;

        std::wstring wtext = UTF8ToWide(lyricText);

        for (int64_t ts : timestamps) {
            LyricLine ll;
            ll.timestamp = ts;
            ll.text = wtext;
            result.push_back(ll);
        }
    }

    // 输出调试信息（已禁用）
    // OutputDebugStringW((L"Parsed " + std::to_wstring(result.size()) + L" lyric lines\n").c_str());
    // if (!result.empty()) {
    //     OutputDebugStringW((L"First lyric: [" + std::to_wstring(result[0].timestamp) + L"ms] " + result[0].text + L"\n").c_str());
    //     OutputDebugStringW((L"Last lyric: [" + std::to_wstring(result.back().timestamp) + L"ms] " + result.back().text + L"\n").c_str());
    // }

    std::sort(result.begin(), result.end(),
        [](const LyricLine& a, const LyricLine& b) { return a.timestamp < b.timestamp; });
    return result;
}

// ---------- 本地文件 ----------
std::wstring LyricsMonitor::GetExeDirectory() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath);
}

bool LyricsMonitor::LoadLrcFromFile(const std::wstring& filePath, std::vector<LyricLine>& outLyrics) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    outLyrics = ParseLrc(buffer.str());
    return !outLyrics.empty();
}

// 辅助函数：清理文件名中的非法字符
static std::wstring CleanFileName(const std::wstring& name) {
    std::wstring result = name;
    // 移除Windows文件名中非法的字符
    const std::wstring illegalChars = L"\\/:*?\"<>|";
    for (wchar_t c : illegalChars) {
        size_t pos = 0;
        while ((pos = result.find(c, pos)) != std::wstring::npos) {
            result.replace(pos, 1, L"_");
        }
    }
    return result;
}

// ---------- 获取线程 ----------
void LyricsMonitor::FetchLyricsThread(std::wstring title, std::wstring artist) {
    std::vector<LyricLine> newLyrics;

    // 生成缓存文件名
    std::wstring baseDir = GetExeDirectory();
    std::wstring cleanTitle = CleanFileName(title);
    std::wstring cleanArtist = CleanFileName(artist);
    std::wstring cacheFile = baseDir + L"\\lyrics_cache\\" + cleanTitle;
    if (!cleanArtist.empty() && cleanArtist != L"系统") {
        cacheFile += L" - " + cleanArtist;
    }
    cacheFile += L".lrc";

    // 1. 优先尝试从本地缓存加载
    // OutputDebugStringW((L"Trying to load from cache: " + cacheFile + L"\n").c_str());
    if (LoadLrcFromFile(cacheFile, newLyrics)) {
        // std::wstring msg = L"Lyrics loaded  from cache successfully!\n";
        // OutputDebugStringW(msg.c_str());
    }

    // 2. 如果缓存没有，再从网络获取
    if (newLyrics.empty()) {
        // 简化搜索策略：先尝试完整歌曲名，再尝试清理后的版本
        std::vector<std::wstring> keywords;

        // 1. 优先只搜索歌曲名（最简单）
        keywords.push_back(title);

        // 2. 如果有艺术家，也尝试歌曲名+艺术家
        if (!artist.empty() && artist != L"系统" && artist != L"") {
            keywords.push_back(title + L" " + artist);
        }

        // 3. 清理歌曲名中的特殊字符后再试
        std::wstring simpleTitle = title;
        size_t pos;
        // 只移除常见的干扰字符，但保留核心歌曲名
        while ((pos = simpleTitle.find(L" (")) != std::wstring::npos) {
            simpleTitle = simpleTitle.substr(0, pos);
        }
        while ((pos = simpleTitle.find(L"（")) != std::wstring::npos) {
            simpleTitle = simpleTitle.substr(0, pos);
        }
        while ((pos = simpleTitle.find(L"[")) != std::wstring::npos) {
            simpleTitle = simpleTitle.substr(0, pos);
        }
        // 清理首尾空格
        size_t start = simpleTitle.find_first_not_of(L" \t\r\n");
        size_t end = simpleTitle.find_last_not_of(L" \t\r\n");
        if (start != std::wstring::npos && end != std::wstring::npos) {
            simpleTitle = simpleTitle.substr(start, end - start + 1);
        }
        if (!simpleTitle.empty() && simpleTitle != title) {
            keywords.push_back(simpleTitle);
            if (!artist.empty() && artist != L"系统" && artist != L"") {
                keywords.push_back(simpleTitle + L" " + artist);
            }
        }

        // 网络获取 - 尝试多个关键词
        std::wstring lrcTextToSave;
        for (const auto& keyword : keywords) {
            if (newLyrics.empty() && !keyword.empty()) {
                // 输出调试信息（已禁用）
                // OutputDebugStringW((L"Trying keyword: " + keyword + L"\n").c_str());

                std::wstring songId = SearchSong(keyword, artist);
                if (!songId.empty()) {
                    // OutputDebugStringW((L"Found song ID: " + songId + L"\n").c_str());

                    std::wstring lrcText = FetchLyricsFromNetEase(songId);
                    if (!lrcText.empty()) {
                        std::string utf8Lrc = WideToUTF8(lrcText);
                        newLyrics = ParseLrc(utf8Lrc);
                        if (!newLyrics.empty()) {
                            lrcTextToSave = lrcText;
                            // std::wstring msg = L"Lyrics loaded successfully!\n";
                            // OutputDebugStringW(msg.c_str());
                            break; // 找到歌词了，停止搜索
                        }
                    }
                }
            }
        }

        // 保存到缓存
        if (!newLyrics.empty() && !lrcTextToSave.empty()) {
            try {
                // 创建缓存目录
                std::wstring cacheDir = baseDir + L"\\lyrics_cache";
                CreateDirectoryW(cacheDir.c_str(), nullptr);

                // 保存歌词到缓存文件
                std::wofstream file(cacheFile);
                if (file.is_open()) {
                    file << lrcTextToSave;
                    file.close();
                    // std::wstring msg = (L"Saved lyrics to cache: " + cacheFile + L"\n");
                    // OutputDebugStringW(msg.c_str());
                }
            }
            catch (...) {
                // OutputDebugStringW(L"Failed to save lyrics to cache\n");
            }
        }
    }

    // 3. 本地文件（作为备选）
    if (newLyrics.empty()) {
        std::wstring file1 = baseDir + L"\\" + title + L" - " + artist + L".lrc";
        std::wstring file2 = baseDir + L"\\" + title + L".lrc";
        LoadLrcFromFile(file1, newLyrics) || LoadLrcFromFile(file2, newLyrics);
    }

    // 4. 更新
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    if (m_currentSongTitle == title && m_currentSongArtist == artist) {
        m_lyrics = std::move(newLyrics);
    }
    m_isFetching = false;
}

