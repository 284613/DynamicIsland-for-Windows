//LyricsMonitor.cpp
#include "LyricsMonitor.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cwctype>
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

static bool IsCjkChar(wchar_t ch) {
    return (ch >= 0x3400 && ch <= 0x9FFF) ||
        (ch >= 0xF900 && ch <= 0xFAFF) ||
        (ch >= 0x3040 && ch <= 0x30FF) ||
        (ch >= 0xAC00 && ch <= 0xD7AF);
}

static std::vector<std::wstring> SplitLyricTokens(const std::wstring& text) {
    std::vector<std::wstring> tokens;
    std::wstring current;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (wchar_t ch : text) {
        if (std::iswspace(ch)) {
            flush();
            if (!tokens.empty()) {
                tokens.back().push_back(ch);
            }
            continue;
        }

        if (IsCjkChar(ch)) {
            flush();
            tokens.emplace_back(1, ch);
            continue;
        }

        if (std::iswalnum(ch) || ch == L'\'' || ch == L'-') {
            current.push_back(ch);
        } else {
            flush();
            tokens.emplace_back(1, ch);
        }
    }

    flush();
    if (tokens.empty() && !text.empty()) {
        tokens.push_back(text);
    }
    return tokens;
}

static int64_t LyricTokenWeight(const std::wstring& token) {
    int64_t weight = 0;
    for (wchar_t ch : token) {
        if (std::iswspace(ch)) {
            weight += 1;
        } else if (IsCjkChar(ch)) {
            weight += 2;
        } else {
            weight += 1;
        }
    }
    return (std::max)(int64_t{ 1 }, weight);
}

struct RawDynamicWord {
    std::wstring text;
    int64_t rawStart = 0;
    int64_t durationMs = 0;
    int flag = 0;
};

static bool HasCjkChar(const std::wstring& text) {
    return std::any_of(text.begin(), text.end(), IsCjkChar);
}

static bool EndsWithSpace(const std::wstring& text) {
    return !text.empty() && std::iswspace(text.back()) != 0;
}

static bool IsPunctuationOrSpaceOnly(const std::wstring& text) {
    if (text.empty()) {
        return true;
    }
    for (wchar_t ch : text) {
        if (!std::iswspace(ch) && !std::iswpunct(ch)) {
            return false;
        }
    }
    return true;
}

static std::vector<std::wstring> SplitDynamicWordPreservingSpaces(const std::wstring& text) {
    std::vector<std::wstring> runs;
    std::wstring current;
    const bool leadingSpace = !text.empty() && std::iswspace(text.front()) != 0;
    const bool trailingSpace = !text.empty() && std::iswspace(text.back()) != 0;

    for (wchar_t ch : text) {
        if (std::iswspace(ch)) {
            if (!current.empty()) {
                runs.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        runs.push_back(current);
    }

    if (runs.empty()) {
        return {};
    }

    for (size_t i = 0; i < runs.size(); ++i) {
        if (i == 0 && leadingSpace) {
            runs[i].insert(runs[i].begin(), L' ');
        }
        if (i + 1 < runs.size() || (i + 1 == runs.size() && trailingSpace)) {
            runs[i].push_back(L' ');
        }
    }
    return runs;
}

static void MarkLyricWordMetadata(LyricWord& word) {
    word.isCjk = HasCjkChar(word.text);
    word.endsWithSpace = EndsWithSpace(word.text);
}

static void MarkTrailingWords(std::vector<LyricWord>& words) {
    if (words.empty()) {
        return;
    }

    std::vector<int> boundaries;
    boundaries.push_back(-1);
    for (size_t i = 0; i + 1 < words.size(); ++i) {
        if (words[i].endsWithSpace || IsPunctuationOrSpaceOnly(words[i].text)) {
            boundaries.push_back(static_cast<int>(i));
        }
    }
    boundaries.push_back(static_cast<int>(words.size() - 1));

    for (size_t segment = 1; segment < boundaries.size(); ++segment) {
        const int begin = boundaries[segment - 1];
        const int end = boundaries[segment];
        for (int i = end; i > begin; --i) {
            if (i >= 0 && i < static_cast<int>(words.size()) &&
                !IsPunctuationOrSpaceOnly(words[i].text)) {
                if (words[i].durationMs >= 1000) {
                    words[i].trailing = true;
                }
                break;
            }
        }
    }
}

static std::wstring GetLyricCacheFormatMarker(DynamicLyricFormat format) {
    switch (format) {
    case DynamicLyricFormat::Yrc:
        return L"#DynamicIslandLyricFormat=Yrc\n";
    case DynamicLyricFormat::Klyric:
        return L"#DynamicIslandLyricFormat=Klyric\n";
    case DynamicLyricFormat::None:
    default:
        return L"";
    }
}

static DynamicLyricFormat DetectLyricCacheFormat(const std::wstring& text) {
    if (text.rfind(L"#DynamicIslandLyricFormat=Yrc", 0) == 0) {
        return DynamicLyricFormat::Yrc;
    }
    if (text.rfind(L"#DynamicIslandLyricFormat=Klyric", 0) == 0) {
        return DynamicLyricFormat::Klyric;
    }
    return DynamicLyricFormat::None;
}

static bool ShouldUseRelativeDynamicTimes(
    int64_t lineStart,
    int64_t lineDuration,
    const std::vector<RawDynamicWord>& words) {
    if (words.empty() || lineStart <= 0) {
        return true;
    }

    const int64_t lineEnd = lineStart + lineDuration;
    int absoluteFit = 0;
    int relativeFit = 0;
    for (const auto& word : words) {
        if (word.rawStart >= lineStart - 500 && word.rawStart <= lineEnd + 1000) {
            ++absoluteFit;
        }
        if (word.rawStart >= -250 && word.rawStart <= lineDuration + 1000) {
            ++relativeFit;
        }
    }

    if (absoluteFit > relativeFit) {
        return false;
    }
    if (relativeFit > absoluteFit) {
        return true;
    }

    const int64_t firstRawStart = words.front().rawStart;
    if (firstRawStart >= lineStart - 500 && firstRawStart <= lineStart + 1000) {
        return false;
    }
    return firstRawStart + 1000 < lineStart;
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

    // 只让整行歌词轻微提前出现；逐字高亮必须使用真实播放位置。
    const int64_t offsetMs = 250;
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


LyricData LyricsMonitor::GetLyricData(int64_t positionMs) {
    LyricData data = {L"", -1, -1, positionMs};
    std::lock_guard<std::mutex> lock(m_lyricsMutex);
    if (m_lyrics.empty())
        return data;

    const int64_t offsetMs = 250;
    int64_t adjustedPosition = positionMs + offsetMs;
    if (adjustedPosition < 0) adjustedPosition = 0;
    data.positionMs = (std::max)(int64_t{ 0 }, positionMs);

    auto it = std::upper_bound(m_lyrics.begin(), m_lyrics.end(), adjustedPosition,
        [](int64_t ts, const LyricLine& line) { return ts < line.timestamp; });

    if (it != m_lyrics.begin()) {
        --it;
        data.text = it->text;
        data.currentMs = it->timestamp;
        data.words = it->words;
        // 下一行
        auto nextIt = std::next(it);
        if (nextIt != m_lyrics.end()) {
            data.nextMs = nextIt->timestamp;
        } else if (!it->words.empty()) {
            const auto& lastWord = it->words.back();
            data.nextMs = lastWord.startMs + (std::max)(int64_t{ 250 }, lastWord.durationMs);
        }
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

LyricsMonitor::FetchedLyrics LyricsMonitor::FetchLyricsFromNetEase(const std::wstring& songId) {
    FetchedLyrics result;
    try {
        HttpClient httpClient;
        std::wstring url = L"https://music.163.com/api/song/lyric?id=" + songId + L"&lv=1&kv=1&yv=1&tv=-1";
        Uri uri(url);
        HttpRequestMessage request(HttpMethod::Get(), uri);

        // 添加必要的请求头
        request.Headers().Append(L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        request.Headers().Append(L"Referer", L"https://music.163.com/");

        auto response = httpClient.SendRequestAsync(request).get();
        auto responseBody = response.Content().ReadAsStringAsync().get();

        JsonObject json = JsonObject::Parse(responseBody);

        if (json.HasKey(L"yrc")) {
            auto yrc = json.GetNamedObject(L"yrc");
            if (yrc.HasKey(L"lyric")) {
                result.yrcLyric = yrc.GetNamedString(L"lyric").c_str();
                if (!result.yrcLyric.empty()) {
                    result.dynamicFormat = DynamicLyricFormat::Yrc;
                }
            }
        }

        if (json.HasKey(L"klyric")) {
            auto klyric = json.GetNamedObject(L"klyric");
            if (klyric.HasKey(L"lyric")) {
                result.klyricLyric = klyric.GetNamedString(L"lyric").c_str();
                if (result.dynamicFormat == DynamicLyricFormat::None && !result.klyricLyric.empty()) {
                    result.dynamicFormat = DynamicLyricFormat::Klyric;
                }
            }
        }

        if (json.HasKey(L"lrc")) {
            auto lrc = json.GetNamedObject(L"lrc");
            if (lrc.HasKey(L"lyric")) {
                result.lineLyric = lrc.GetNamedString(L"lyric").c_str();
            }
        }
    }
    catch (winrt::hresult_error const& e) {
        OutputDebugStringW((L"FetchLyrics error: " + std::wstring(e.message().c_str()) + L"\n").c_str());
    }
    return result;
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
    AttachFallbackWordTiming(result);
    return result;
}

static std::vector<LyricLine> ParseTimedDynamicLyrics(const std::wstring& lyricContent, bool wordTimesAreAbsolute) {
    std::vector<LyricLine> result;
    std::wistringstream iss(lyricContent);
    std::wstring line;
    std::wregex headerRegex(LR"(^\[(\d+),(\d+)\])");
    std::wregex wordRegex(LR"(\((\d+),(\d+)(,(\d+))?\)([^\(]*))");

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }

        std::wsmatch headerMatch;
        if (!std::regex_search(line, headerMatch, headerRegex)) {
            continue;
        }

        int64_t lineStart = _wtoi64(headerMatch[1].str().c_str());
        int64_t lineDuration = (std::max)(int64_t{ 1 }, _wtoi64(headerMatch[2].str().c_str()));
        std::wstring wordPart = headerMatch.suffix().str();
        std::vector<RawDynamicWord> rawWords;
        std::wstring lineText;

        auto begin = std::wsregex_iterator(wordPart.begin(), wordPart.end(), wordRegex);
        auto end = std::wsregex_iterator();
        for (auto it = begin; it != end; ++it) {
            const std::wsmatch& match = *it;
            std::wstring wordText = match[5].str();
            if (wordText.empty()) {
                continue;
            }

            int64_t rawStart = _wtoi64(match[1].str().c_str());
            int64_t wordDuration = (std::max)(int64_t{ 1 }, _wtoi64(match[2].str().c_str()));
            int flag = 0;
            if (match[4].matched) {
                flag = _wtoi(match[4].str().c_str());
            }

            const auto tokens = SplitDynamicWordPreservingSpaces(wordText);
            if (tokens.empty()) {
                continue;
            }
            for (size_t tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex) {
                const int64_t tokenStartOffset = (wordDuration * static_cast<int64_t>(tokenIndex)) /
                    static_cast<int64_t>(tokens.size());
                const int64_t tokenEndOffset = (wordDuration * static_cast<int64_t>(tokenIndex + 1)) /
                    static_cast<int64_t>(tokens.size());

                RawDynamicWord word;
                word.text = tokens[tokenIndex];
                word.rawStart = rawStart + tokenStartOffset;
                word.durationMs = (std::max)(int64_t{ 1 }, tokenEndOffset - tokenStartOffset);
                word.flag = flag;
                rawWords.push_back(word);
                lineText += word.text;
            }
        }

        if (rawWords.empty() || lineText.empty()) {
            continue;
        }

        const bool useRelativeTimes = wordTimesAreAbsolute
            ? false
            : ShouldUseRelativeDynamicTimes(lineStart, lineDuration, rawWords);
        std::vector<LyricWord> words;
        words.reserve(rawWords.size());
        for (size_t i = 0; i < rawWords.size(); ++i) {
            LyricWord word;
            word.text = rawWords[i].text;
            word.startMs = useRelativeTimes ? lineStart + rawWords[i].rawStart : rawWords[i].rawStart;
            word.durationMs = rawWords[i].durationMs;
            word.flag = rawWords[i].flag;
            word.durationMs = (std::max)(int64_t{ 1 }, word.durationMs);
            MarkLyricWordMetadata(word);
            words.push_back(std::move(word));
        }
        MarkTrailingWords(words);

        LyricLine parsedLine;
        parsedLine.timestamp = lineStart;
        parsedLine.text = lineText;
        parsedLine.words = std::move(words);
        result.push_back(std::move(parsedLine));
    }

    std::sort(result.begin(), result.end(),
        [](const LyricLine& a, const LyricLine& b) { return a.timestamp < b.timestamp; });
    return result;
}

std::vector<LyricLine> LyricsMonitor::ParseYrcLyrics(const std::wstring& lyricContent) {
    return ParseTimedDynamicLyrics(lyricContent, true);
}

std::vector<LyricLine> LyricsMonitor::ParseKlyricLyrics(const std::wstring& lyricContent) {
    return ParseTimedDynamicLyrics(lyricContent, false);
}

void LyricsMonitor::AttachFallbackWordTiming(std::vector<LyricLine>& lyrics) {
    for (size_t i = 0; i < lyrics.size(); ++i) {
        if (!lyrics[i].words.empty() || lyrics[i].text.empty()) {
            continue;
        }

        const int64_t lineStart = lyrics[i].timestamp;
        int64_t lineEnd = lineStart + 4000;
        if (i + 1 < lyrics.size() && lyrics[i + 1].timestamp > lineStart) {
            lineEnd = lyrics[i + 1].timestamp;
        }

        const auto tokens = SplitLyricTokens(lyrics[i].text);
        if (tokens.empty()) {
            continue;
        }

        const int64_t lineDuration = (std::max)(int64_t{ 500 }, lineEnd - lineStart);
        int64_t totalWeight = 0;
        for (const auto& token : tokens) {
            totalWeight += LyricTokenWeight(token);
        }
        totalWeight = (std::max)(int64_t{ 1 }, totalWeight);
        int64_t consumedWeight = 0;

        lyrics[i].words.clear();
        lyrics[i].words.reserve(tokens.size());
        for (size_t tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex) {
            const int64_t tokenWeight = LyricTokenWeight(tokens[tokenIndex]);
            const int64_t startOffset = (lineDuration * consumedWeight) / totalWeight;
            consumedWeight += tokenWeight;
            const int64_t endOffset = (tokenIndex + 1 == tokens.size())
                ? lineDuration
                : (lineDuration * consumedWeight) / totalWeight;

            LyricWord word;
            word.text = tokens[tokenIndex];
            word.startMs = lineStart + startOffset;
            word.durationMs = (std::max)(int64_t{ 1 }, endOffset - startOffset);
            MarkLyricWordMetadata(word);
            lyrics[i].words.push_back(std::move(word));
        }
        MarkTrailingWords(lyrics[i].words);
    }
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

    const std::wstring wideLyrics = UTF8ToWide(buffer.str());
    const DynamicLyricFormat cacheFormat = DetectLyricCacheFormat(wideLyrics);
    if (cacheFormat == DynamicLyricFormat::Yrc) {
        outLyrics = ParseYrcLyrics(wideLyrics);
    } else if (cacheFormat == DynamicLyricFormat::Klyric) {
        outLyrics = ParseKlyricLyrics(wideLyrics);
    } else {
        outLyrics = ParseKlyricLyrics(wideLyrics);
    }
    if (outLyrics.empty()) {
        outLyrics = ParseLrc(buffer.str());
    }
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

                    FetchedLyrics fetchedLyrics = FetchLyricsFromNetEase(songId);
                    std::vector<LyricLine> lineLyrics;
                    if (!fetchedLyrics.lineLyric.empty()) {
                        lineLyrics = ParseLrc(WideToUTF8(fetchedLyrics.lineLyric));
                    }

                    if (!fetchedLyrics.yrcLyric.empty()) {
                        std::vector<LyricLine> yrcLyrics = ParseYrcLyrics(fetchedLyrics.yrcLyric);
                        const bool yrcLooksComplete = lineLyrics.empty() ||
                            yrcLyrics.size() * 10 >= lineLyrics.size() * 3;
                        if (!yrcLyrics.empty() && yrcLooksComplete) {
                            newLyrics = std::move(yrcLyrics);
                            lrcTextToSave = GetLyricCacheFormatMarker(DynamicLyricFormat::Yrc) + fetchedLyrics.yrcLyric;
                            break;
                        }
                    }

                    if (!fetchedLyrics.klyricLyric.empty()) {
                        newLyrics = ParseKlyricLyrics(fetchedLyrics.klyricLyric);
                        if (!newLyrics.empty()) {
                            lrcTextToSave = GetLyricCacheFormatMarker(DynamicLyricFormat::Klyric) + fetchedLyrics.klyricLyric;
                            break;
                        }
                    }

                    if (!lineLyrics.empty()) {
                        newLyrics = std::move(lineLyrics);
                        if (!newLyrics.empty()) {
                            lrcTextToSave = fetchedLyrics.lineLyric;
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
                std::ofstream file(cacheFile, std::ios::binary);
                if (file.is_open()) {
                    file << WideToUTF8(lrcTextToSave);
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

