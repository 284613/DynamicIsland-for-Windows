#pragma once
#include "WeatherPlugin.h"
#include "Messages.h"
#include <string>
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cctype>
#include <zlib.h>

// ==================== QWeather 配置（从 config.ini 读取）====================
// config.ini 位于 exe 同目录，格式：
//   [Weather]
//   APIKey=xxxxxxxx
static const char* QWEATHER_HOST = "n94ewu37fy.re.qweatherapi.com";  // GeoAPI 与天气 API 共用自定义域名

static std::string g_apiKey;   // 从 config.ini 动态加载
static std::string g_cityQueryUtf8 = "北京";
static std::mutex g_weatherConfigMutex;
static char g_locationId[32] = "101250109";   // LocationID
static char g_locationLon[16] = "112.93";      // 经度
static char g_locationLat[16] = "27.87";       // 纬度
static wchar_t g_configCityName[64] = L"北京";  // 设置页保存的城市名
static wchar_t g_districtName[64] = L"岳麓区"; // 显示用区县名
static std::atomic<bool> g_locationFetched{ false };

static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), wide.data(), len);
    return wide;
}

static std::wstring TrimLineEndings(std::wstring line) {
    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
        line.pop_back();
    }
    return line;
}

static bool StartsWithKey(const std::wstring& line, const std::wstring& key) {
    return line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == L'=';
}

static std::wstring ReadUtf8IniValue(const std::wstring& path, const std::wstring& section, const std::wstring& key, const std::wstring& fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return fallback;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring text = Utf8ToWide(bytes);
    if (text.empty()) return fallback;

    std::wistringstream stream(text);
    std::wstring line;
    std::wstring currentSection;
    const std::wstring targetSection = L"[" + section + L"]";
    while (std::getline(stream, line)) {
        line = TrimLineEndings(line);
        if (line.empty()) continue;
        if (line.front() == L'[' && line.back() == L']') {
            currentSection = line;
            continue;
        }
        if (currentSection == targetSection && StartsWithKey(line, key)) {
            return line.substr(key.size() + 1);
        }
    }
    return fallback;
}

static void LoadConfig() {
    // 获取 exe 所在目录
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    // 截断到目录
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    wchar_t iniPath[MAX_PATH];
    wcscpy_s(iniPath, exePath);
    wcscat_s(iniPath, L"config.ini");

    wchar_t keyBuf[128] = {};
    GetPrivateProfileStringW(L"Weather", L"APIKey", L"", keyBuf, 128, iniPath);
    wchar_t locationBuf[32] = {};
    GetPrivateProfileStringW(L"Weather", L"LocationId", L"", locationBuf, 32, iniPath);
    wchar_t cityBuf[64] = {};
    const std::wstring cityValue = ReadUtf8IniValue(iniPath, L"Weather", L"City", L"北京");
    wcsncpy_s(cityBuf, _countof(cityBuf), cityValue.c_str(), _TRUNCATE);

    std::lock_guard<std::mutex> lock(g_weatherConfigMutex);

    if (keyBuf[0] != L'\0') {
        int len = WideCharToMultiByte(CP_UTF8, 0, keyBuf, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            g_apiKey.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, keyBuf, -1, &g_apiKey[0], len, nullptr, nullptr);
        }
        OutputDebugStringA(("[Weather] Loaded APIKey from config.ini: " + g_apiKey.substr(0, 8) + "...\n").c_str());
    } else {
        // 回退：使用硬编码 key
        g_apiKey = "bf25bfa431394152adb2f4ed57ac092e";
        OutputDebugStringA("[Weather] config.ini not found or empty, using fallback key\n");
    }

    int cityLen = WideCharToMultiByte(CP_UTF8, 0, cityBuf, -1, nullptr, 0, nullptr, nullptr);
    if (cityLen > 0) {
        g_cityQueryUtf8.resize(cityLen - 1);
        WideCharToMultiByte(CP_UTF8, 0, cityBuf, -1, &g_cityQueryUtf8[0], cityLen, nullptr, nullptr);
        wcscpy_s(g_configCityName, _countof(g_configCityName), cityBuf);
    }

    if (locationBuf[0] != L'\0') {
        WideCharToMultiByte(CP_UTF8, 0, locationBuf, -1, g_locationId, (int)sizeof(g_locationId), nullptr, nullptr);
        g_locationFetched.store(true);
    } else {
        g_locationFetched.store(false);
    }
}

// ==================== 辅助函数：GBK 转 UTF-8 ====================
static std::string GbkToUtf8(const std::string& gbkStr) {
    if (gbkStr.empty()) return "";
    int wlen = MultiByteToWideChar(936, 0, gbkStr.c_str(), -1, NULL, 0);
    if (wlen == 0) return gbkStr;
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(936, 0, gbkStr.c_str(), -1, &wstr[0], wlen);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string utf8Str(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8Str[0], utf8Len, NULL, NULL);
    while (!utf8Str.empty() && utf8Str.back() == '\0') utf8Str.pop_back();
    return utf8Str;
}

// ==================== 辅助：URL 编码 ====================
static std::string UrlEncode(const std::string& str) {
    std::string encoded;
    char buf[10];
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

// ==================== HTTP GET + gzip 解压（zlib）====================
static std::string HttpGetGzip(const char* host, const char* path, bool useHttps = false) {
    std::string response;

    int hlen = MultiByteToWideChar(CP_UTF8, 0, host, -1, NULL, 0);
    std::wstring whost;
    if (hlen > 0) { whost.resize(hlen - 1); MultiByteToWideChar(CP_UTF8, 0, host, -1, &whost[0], hlen); }

    int plen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    std::wstring wpath;
    if (plen > 0) { wpath.resize(plen - 1); MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], plen); }

    HINTERNET hSession = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return response;

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return response; }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", wpath.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return response; }

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            std::string compressed;
            DWORD size = 0;
            do {
                if (!WinHttpQueryDataAvailable(hRequest, &size) || size == 0) break;
                std::string buf;
                buf.resize(size);
                DWORD downloaded = 0;
                if (WinHttpReadData(hRequest, &buf[0], size, &downloaded)) {
                    buf.resize(downloaded);
                    compressed += buf;
                }
            } while (size > 0);

            if (!compressed.empty()) {
                z_stream strm = {};
                strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
                strm.avail_in = static_cast<uInt>(compressed.size());

                if (inflateInit2(&strm, 16 + MAX_WBITS) == Z_OK) {
                    std::string decompressed;
                    unsigned char outBuffer[8192];

                    int ret;
                    do {
                        strm.next_out = outBuffer;
                        strm.avail_out = sizeof(outBuffer);
                        ret = inflate(&strm, Z_NO_FLUSH);
                        if (ret != Z_OK && ret != Z_STREAM_END) {
                            OutputDebugStringA("[Weather] zlib inflate error, breaking\n");
                            break;
                        }
                        size_t have = sizeof(outBuffer) - strm.avail_out;
                        decompressed.append(reinterpret_cast<char*>(outBuffer), have);
                    } while (ret != Z_STREAM_END);

                    inflateEnd(&strm);
                    response = decompressed;
                } else {
                    OutputDebugStringA("[Weather] inflateInit2 failed\n");
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

// ==================== 简单 JSON 解析（手写，无外部依赖）====================
// 核心原则：回归最原始的字符串查找，确保与当前天气提取逻辑高度一致
static std::string ExtractJsonField(const std::string& json, const char* key) {
    std::string pattern = "\"" + std::string(key) + "\"";
    size_t p = json.find(pattern);
    if (p == std::string::npos) return "";
    
    // 找冒号
    p = json.find(':', p + pattern.length());
    if (p == std::string::npos) return "";
    p++;
    
    // 跳过空白
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) p++;
    if (p >= json.size()) return "";

    // 字符串值
    if (json[p] == '"') {
        p++;
        std::string val;
        while (p < json.size() && json[p] != '"') {
            if (json[p] == '\\' && p + 1 < json.size()) { p++; val += json[p++]; }
            else val += json[p++];
        }
        return val;
    }

    // 数值
    std::string val;
    while (p < json.size() && json[p] != ',' && json[p] != '}' && json[p] != ']') {
        if (json[p] != ' ' && json[p] != '\t' && json[p] != '\r' && json[p] != '\n') val += json[p];
        p++;
    }
    return val;
}

static std::string ExtractJsonNestedField(const std::string& json, const char* outer, const char* inner) {
    std::string pattern = "\"" + std::string(outer) + "\"";
    size_t p = json.find(pattern);
    if (p == std::string::npos) return "";
    
    p = json.find('{', p + pattern.length());
    if (p == std::string::npos) return "";
    
    int depth = 0;
    size_t start = p;
    size_t end = std::string::npos;
    for (size_t i = p; i < json.size(); ++i) {
        if (json[i] == '{') depth++;
        else if (json[i] == '}') {
            depth--;
            if (depth == 0) { end = i; break; }
        }
    }
    if (end == std::string::npos) return "";
    
    std::string obj = json.substr(start, end - start + 1);
    return ExtractJsonField(obj, inner);
}

static std::string ExtractJsonArrayField(const std::string& json, const char* arrayName, int index, const char* field) {
    std::string pattern = "\"" + std::string(arrayName) + "\"";
    size_t p = json.find(pattern);
    if (p == std::string::npos) return "";
    
    p = json.find('[', p + pattern.length());
    if (p == std::string::npos) return "";
    
    size_t objStart = p;
    for (int i = 0; i <= index; ++i) {
        objStart = json.find('{', objStart + (i == 0 ? 0 : 1));
        if (objStart == std::string::npos) return "";
        
        if (i == index) {
            int depth = 0;
            size_t objEnd = std::string::npos;
            for (size_t j = objStart; j < json.size(); ++j) {
                if (json[j] == '{') depth++;
                else if (json[j] == '}') {
                    depth--;
                    if (depth == 0) { objEnd = j; break; }
                }
            }
            if (objEnd == std::string::npos) return "";
            std::string obj = json.substr(objStart, objEnd - objStart + 1);
            return ExtractJsonField(obj, field);
        }
    }
    return "";
}

// ==================== GeoAPI：城市搜索 ====================
// 通过城市名查 LocationID 和坐标（使用 GeoAPI v2）
static void FetchLocation() {
    if (g_locationFetched.load()) return;
    LoadConfig();

    OutputDebugStringA("[Weather] Fetching location via GeoAPI...\n");

    std::string cityQuery;
    std::string apiKey;
    {
        std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
        cityQuery = g_cityQueryUtf8.empty() ? "北京" : g_cityQueryUtf8;
        apiKey = g_apiKey;
    }

    std::string encodedLocation = UrlEncode(cityQuery);
    char path[512];
    snprintf(path, sizeof(path),
        "/geo/v2/city/lookup?location=%s&range=cn&number=1&key=%s",
        encodedLocation.c_str(), apiKey.c_str());

    std::string resp = HttpGetGzip(QWEATHER_HOST, path, true);

    if (resp.empty()) {
        OutputDebugStringA("[Weather] GeoAPI returned empty response, using defaults\n");
        g_locationFetched.store(true);
        return;
    }

    OutputDebugStringA(("[Weather] GeoAPI raw: " + resp.substr(0, 300) + "\n").c_str());

    std::string code = ExtractJsonField(resp, "code");
    if (code != "200") {
        OutputDebugStringA(("[Weather] GeoAPI error: " + code + "\n").c_str());
        g_locationFetched.store(true);
        return;
    }

    // 解析 location[0]
    std::string idVal = ExtractJsonArrayField(resp, "location", 0, "id");
    std::string nameVal = ExtractJsonArrayField(resp, "location", 0, "name");
    std::string adm2Val = ExtractJsonArrayField(resp, "location", 0, "adm2");

    if (!idVal.empty()) {
        strncpy_s(g_locationId, idVal.c_str(), sizeof(g_locationId) - 1);
        g_locationId[sizeof(g_locationId) - 1] = '\0';
    }

    if (!adm2Val.empty() && !nameVal.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, adm2Val.c_str(), -1, NULL, 0);
        if (wlen > 0 && wlen < 64) {
            MultiByteToWideChar(CP_UTF8, 0, adm2Val.c_str(), -1, g_districtName, wlen);
            g_districtName[wlen - 1] = L'\0';
        }
    }

    g_locationFetched.store(true);
}

// ==================== 天气代码翻译表====================
static const char* WeatherCodeToText(int code) {
    switch (code) {
        case 0: return "晴";
        case 1: return "晴间多云";
        case 2: return "多云";
        case 3: return "阴";
        case 45: case 48: return "雾";
        case 51: case 53: case 55: return "毛毛雨";
        case 56: case 57: return "冻毛毛雨";
        case 61: case 63: case 65: return "小雨";
        case 66: case 67: return "冻雨";
        case 71: case 73: case 75: return "雪";
        case 77: return "雪粒";
        case 80: case 81: case 82: return "阵雨";
        case 85: case 86: return "阵雪";
        case 95: return "雷暴";
        case 96: case 99: return "雷暴冰雹";
        default: return "未知";
    }
}

// ==================== WeatherPlugin 实现====================
WeatherPlugin::WeatherPlugin() {
    m_locationText = L"北京";
    m_description = L"晴";
    m_temperature = 25.0f;
    m_lastUpdateTime = 0;
}

WeatherPlugin::~WeatherPlugin() {
}

PluginInfo WeatherPlugin::GetInfo() const {
    PluginInfo info = {};
    info.name = L"WeatherPlugin";
    info.description = L"Weather information";
    return info;
}

bool WeatherPlugin::Initialize() {
    // 初始化时获取一次位置
    LoadConfig();
    FetchLocation();
    FetchWeather();
    return true;
}

void WeatherPlugin::Shutdown() {
}

void WeatherPlugin::Update(float deltaTime) {
    size_t now = GetTickCount64();
    if (now - m_lastUpdateTime < 3 * 60 * 1000 && m_lastUpdateTime != 0) {
        return;
    }
    FetchWeather();
}

void WeatherPlugin::FetchWeather() {
    LoadConfig();  // 确保 APIKey 已从 config.ini 加载

    {
        std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
        m_locationText = g_configCityName[0] ? std::wstring(g_configCityName) : L"北京";
    }

    static std::atomic<bool> isFetching{ false };
    if (isFetching) return;
    isFetching = true;

    m_lastUpdateTime = GetTickCount64();

    std::thread([this]() {
        // 确保位置已获取
        FetchLocation();
        std::string apiKey;
        std::string locationId;
        std::wstring configuredCity;
        {
            std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
            apiKey = g_apiKey;
            locationId = g_locationId;
            configuredCity = g_configCityName;
        }

        OutputDebugStringA("[Weather] Fetching weather...\n");

        // 使用 LocationID 查天气（更精确）
        char path[512];
        snprintf(path, sizeof(path),
            "/v7/weather/now?location=%s&key=%s",
            locationId.c_str(), apiKey.c_str());

        std::string resp = HttpGetGzip(QWEATHER_HOST, path, true);

        if (resp.empty()) {
            OutputDebugStringA("[Weather] Weather API returned empty response\n");
            isFetching = false;
            return;
        }

        OutputDebugStringA(("[Weather] Weather raw JSON: " + resp.substr(0, 200) + "\n").c_str());

        // 解析 JSON —— 字段路径对应 /v7/weather/now 响应结构
        std::string code      = ExtractJsonField(resp, "code");
        std::string temp      = ExtractJsonNestedField(resp, "now", "temp");
        std::string feelsLike = ExtractJsonNestedField(resp, "now", "feelsLike");
        std::string icon      = ExtractJsonNestedField(resp, "now", "icon");
        std::string text      = ExtractJsonNestedField(resp, "now", "text");
        std::string humidity  = ExtractJsonNestedField(resp, "now", "humidity");
        std::string windDir   = ExtractJsonNestedField(resp, "now", "windDir");
        std::string windScale = ExtractJsonNestedField(resp, "now", "windScale");

        if (code != "200") {
            OutputDebugStringA(("[Weather] Weather API error code: " + code + "\n").c_str());
            isFetching = false;
            return;
        }

        // 天气图标 ID（如 "100" = 晴）
        if (!icon.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, icon.c_str(), -1, NULL, 0);
            if (wlen > 0) {
                m_iconId.resize(wlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, icon.c_str(), -1, &m_iconId[0], wlen);
            }
        }

        // 天气文字描述（如 "晴"）
        std::wstring desc;
        if (!text.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
            if (wlen > 0) {
                desc.resize(wlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &desc[0], wlen);
            }
        }

        m_locationText = configuredCity.empty() ? L"北京" : configuredCity;
        m_description = desc.empty() ? L"未知" : desc;

        // 温度
        if (!temp.empty()) {
            m_temperature = (float)std::atof(temp.c_str());
        }

        // ------------------ 获取逐小时预报 ------------------
        snprintf(path, sizeof(path),
            "/v7/weather/24h?location=%s&key=%s",
            locationId.c_str(), apiKey.c_str());
        std::string respHourly = HttpGetGzip(QWEATHER_HOST, path, true);
        if (respHourly.empty()) {
            OutputDebugStringA("[Weather] Hourly API returned empty response\n");
        } else {
            std::string hourlyCode = ExtractJsonField(respHourly, "code");
            OutputDebugStringA(("[Weather] Hourly API code: " + hourlyCode + "\n").c_str());
            if (hourlyCode == "200") {
                std::vector<HourlyForecast> forecasts;
                for (int i = 0; i < 6; ++i) {
                    std::string timeStr  = ExtractJsonArrayField(respHourly, "hourly", i, "fxTime");
                    std::string htempStr = ExtractJsonArrayField(respHourly, "hourly", i, "temp");
                    std::string iconStr  = ExtractJsonArrayField(respHourly, "hourly", i, "icon");
                    std::string textStr  = ExtractJsonArrayField(respHourly, "hourly", i, "text");
                    if (timeStr.empty() || htempStr.empty()) {
                        OutputDebugStringA(("[Weather] Hourly[" + std::to_string(i) + "] missing fxTime/temp, stopping\n").c_str());
                        break;
                    }

                    HourlyForecast hf;
                    // fxTime 格式 "2021-02-16T15:00+08:00"，提取 "15:00"
                    size_t tPos = timeStr.find('T');
                    if (tPos != std::string::npos && tPos + 6 <= timeStr.length()) {
                        std::string timeOnly = timeStr.substr(tPos + 1, 5);
                        int wlen = MultiByteToWideChar(CP_UTF8, 0, timeOnly.c_str(), -1, NULL, 0);
                        if (wlen > 0) {
                            hf.time.resize(wlen - 1);
                            MultiByteToWideChar(CP_UTF8, 0, timeOnly.c_str(), -1, &hf.time[0], wlen);
                        }
                    }
                    hf.temp = (float)std::atof(htempStr.c_str());

                    // icon ID（如 "100"）
                    if (!iconStr.empty()) {
                        int wlen = MultiByteToWideChar(CP_UTF8, 0, iconStr.c_str(), -1, NULL, 0);
                        if (wlen > 0) {
                            hf.icon.resize(wlen - 1);
                            MultiByteToWideChar(CP_UTF8, 0, iconStr.c_str(), -1, &hf.icon[0], wlen);
                        }
                    }

                    // 天气描述文字（如 "晴"）
                    if (!textStr.empty()) {
                        int wlen = MultiByteToWideChar(CP_UTF8, 0, textStr.c_str(), -1, NULL, 0);
                        if (wlen > 0) {
                            hf.text.resize(wlen - 1);
                            MultiByteToWideChar(CP_UTF8, 0, textStr.c_str(), -1, &hf.text[0], wlen);
                        }
                    }

                    forecasts.push_back(hf);
                }
                m_hourlyForecasts = forecasts;
                OutputDebugStringA(("[Weather] Hourly parsed count: " + std::to_string(forecasts.size()) + "\n").c_str());
            }
        }

        // ------------------ 获取生活指数 (建议) ------------------
        snprintf(path, sizeof(path),
            "/v7/indices/1d?location=%s&key=%s&type=1,3,9",
            locationId.c_str(), apiKey.c_str());
        std::string respIndex = HttpGetGzip(QWEATHER_HOST, path, true);
        if (!respIndex.empty() && ExtractJsonField(respIndex, "code") == "200") {
            // 尝试找 type=9 (感冒指数) 或第一个
            std::string suggestion = ExtractJsonArrayField(respIndex, "daily", 0, "text");
            if (!suggestion.empty()) {
                int wlen = MultiByteToWideChar(CP_UTF8, 0, suggestion.c_str(), -1, NULL, 0);
                if (wlen > 0) {
                    m_lifeSuggestion.resize(wlen - 1);
                    MultiByteToWideChar(CP_UTF8, 0, suggestion.c_str(), -1, &m_lifeSuggestion[0], wlen);
                }
            }
        }

        // ------------------ 获取逐日预报 (7天) ------------------
        snprintf(path, sizeof(path),
            "/v7/weather/7d?location=%s&key=%s",
            locationId.c_str(), apiKey.c_str());
        std::string respDaily = HttpGetGzip(QWEATHER_HOST, path, true);
        if (!respDaily.empty() && ExtractJsonField(respDaily, "code") == "200") {
            std::vector<DailyForecast> dailyForecasts;
            for (int i = 0; i < 7; ++i) {
                std::string dateStr    = ExtractJsonArrayField(respDaily, "daily", i, "fxDate");
                std::string tmaxStr    = ExtractJsonArrayField(respDaily, "daily", i, "tempMax");
                std::string tminStr    = ExtractJsonArrayField(respDaily, "daily", i, "tempMin");
                std::string iconStr    = ExtractJsonArrayField(respDaily, "daily", i, "iconDay");
                std::string textStr    = ExtractJsonArrayField(respDaily, "daily", i, "textDay");
                if (dateStr.empty() || tmaxStr.empty()) break;

                DailyForecast df;
                df.tempMax = (float)std::atof(tmaxStr.c_str());
                df.tempMin = (float)std::atof(tminStr.c_str());

                // fxDate 格式 "2021-02-16"，提取 "02-16"
                if (dateStr.size() >= 7) {
                    std::string mmdd = dateStr.substr(5);
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, mmdd.c_str(), -1, NULL, 0);
                    if (wlen > 0) { df.date.resize(wlen - 1); MultiByteToWideChar(CP_UTF8, 0, mmdd.c_str(), -1, &df.date[0], wlen); }
                }

                auto toWide = [](const std::string& s, std::wstring& out) {
                    if (s.empty()) return;
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
                    if (wlen > 0) { out.resize(wlen - 1); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], wlen); }
                };
                toWide(iconStr, df.iconDay);
                toWide(textStr, df.textDay);

                dailyForecasts.push_back(df);
            }
            m_dailyForecasts = dailyForecasts;
            OutputDebugStringA(("[Weather] Daily parsed count: " + std::to_string(dailyForecasts.size()) + "\n").c_str());
        } else {
            OutputDebugStringA("[Weather] Daily API failed or empty\n");
        }

        // 调试输出
        wchar_t dbg[512];
        swprintf_s(dbg, L"[Weather] %ls | %ls | %hsC | hourly count: %zu | daily count: %zu\n",
            m_locationText.c_str(), m_description.c_str(), temp.c_str(), m_hourlyForecasts.size(), m_dailyForecasts.size());
        OutputDebugStringW(dbg);

        isFetching = false;
        // 通知主窗口刷新天气显示
        if (m_notifyHwnd) PostMessage(m_notifyHwnd, WM_WEATHER_UPDATED, 0, 0);
    }).detach();
}
