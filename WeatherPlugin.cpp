#pragma once
#include "WeatherPlugin.h"
#include <string>
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cctype>
#include <zlib.h>

// ==================== QWeather API Key（免费key，每天1000次）====================
// API key loaded from config.ini at runtime
static std::string g_qweatherKey;
static const char* GetQWeatherKey() {
    if (g_qweatherKey.empty()) {
        char buffer[256] = { 0 };
        char exePath[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, exePath, sizeof(exePath));
        char* lastSlash = strrchr(exePath, 92);  // 92 = ASCII for backslash
        if (lastSlash) {
            *lastSlash = 0;  // Terminate at last backslash to get directory
            strcat_s(exePath, "\\config.ini");
            GetPrivateProfileStringA("Weather", "APIKey", "", buffer, sizeof(buffer), exePath);
        }
        g_qweatherKey = buffer;
    }
    return g_qweatherKey.c_str();
}
static const char* QWEATHER_HOST = "n94ewu37fy.re.qweatherapi.com";

// ==================== 动态位置数据（GeoAPI获取）====================
static char g_locationId[32] = "101250109";   // 岳麓区 LocationID
static char g_locationLon[16] = "112.93";      // 经度
static char g_locationLat[16] = "27.87";       // 纬度
static wchar_t g_cityName[64] = L"长沙";       // 显示用城市名
static wchar_t g_districtName[64] = L"岳麓区"; // 显示用区县名
static std::atomic<bool> g_locationFetched{ false };

// ==================== 从 config.ini 加载天气配置 ====================
static void LoadWeatherConfig() {
    char buffer[256] = { 0 };
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, sizeof(exePath));
    char* lastSlash = strrchr(exePath, 92);  // 92 = backslash
    if (lastSlash) {
        *lastSlash = 0;
        strcat_s(exePath, "\\config.ini");

        GetPrivateProfileStringA("Weather", "LocationId", "", buffer, sizeof(buffer), exePath);
        if (buffer[0]) {
            strncpy_s(g_locationId, buffer, sizeof(g_locationId) - 1);
            g_locationId[sizeof(g_locationId) - 1] = 0;
        }

        GetPrivateProfileStringA("Weather", "Longitude", "", buffer, sizeof(buffer), exePath);
        if (buffer[0]) {
            strncpy_s(g_locationLon, buffer, sizeof(g_locationLon) - 1);
            g_locationLon[sizeof(g_locationLon) - 1] = 0;
        }

        GetPrivateProfileStringA("Weather", "Latitude", "", buffer, sizeof(buffer), exePath);
        if (buffer[0]) {
            strncpy_s(g_locationLat, buffer, sizeof(g_locationLat) - 1);
            g_locationLat[sizeof(g_locationLat) - 1] = 0;
        }
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
static std::string ExtractJsonField(const std::string& json, const char* key) {
    std::string pattern = "\"" + std::string(key) + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
        json[pos] == '\n' || json[pos] == '\r')) pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                val += json[pos++];
            } else {
                val += json[pos++];
            }
        }
        return val;
    }

    std::string val;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
        val += json[pos++];
    }
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
    return val;
}

// 嵌套 JSON 字段提取
static std::string ExtractJsonNestedField(const std::string& json, const char* outer, const char* inner) {
    std::string outerPattern = "\"" + std::string(outer) + "\":{";
    size_t oPos = json.find(outerPattern);
    if (oPos == std::string::npos) return "";
    oPos += outerPattern.size() - 1;

    int depth = 1;
    size_t p = oPos + 1;
    while (p < json.size() && depth > 0) {
        if (json[p] == '{') depth++;
        else if (json[p] == '}') depth--;
        p++;
    }

    std::string innerObj = json.substr(oPos, p - oPos);
    return ExtractJsonField(innerObj, inner);
}

// ==================== GeoAPI：城市搜索 ====================
// 通过城市名查 LocationID 和坐标（使用 GeoAPI v2）
static void FetchLocation() {
    if (g_locationFetched.load()) return;

    OutputDebugStringA("[Weather] Fetching location via GeoAPI...\n");

    std::string encodedLocation = UrlEncode("岳麓区");
    char path[512];
    snprintf(path, sizeof(path),
        "/geo/v2/city/lookup?location=%s&range=cn&number=1&key=%s",
        encodedLocation.c_str(), GetQWeatherKey());

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

    // 解析 location[0]: name, id, lon, lat, adm2, adm1
    // 找 "location":[{ ... }]
    size_t locArrStart = resp.find("\"location\":[{");
    if (locArrStart == std::string::npos) {
        OutputDebugStringA("[Weather] GeoAPI: location array not found\n");
        g_locationFetched.store(true);
        return;
    }

    // 提取 name
    std::string nameVal = ExtractJsonField(resp, "name");
    std::string idVal = ExtractJsonField(resp, "id");
    std::string lonVal = ExtractJsonField(resp, "lon");
    std::string latVal = ExtractJsonField(resp, "lat");
    std::string adm2Val = ExtractJsonField(resp, "adm2");
    std::string adm1Val = ExtractJsonField(resp, "adm1");

    if (!idVal.empty()) {
        strncpy_s(g_locationId, idVal.c_str(), sizeof(g_locationId) - 1);
        g_locationId[sizeof(g_locationId) - 1] = '\0';
    }
    if (!lonVal.empty()) {
        strncpy_s(g_locationLon, lonVal.c_str(), sizeof(g_locationLon) - 1);
        g_locationLon[sizeof(g_locationLon) - 1] = '\0';
    }
    if (!latVal.empty()) {
        strncpy_s(g_locationLat, latVal.c_str(), sizeof(g_locationLat) - 1);
        g_locationLat[sizeof(g_locationLat) - 1] = '\0';
    }

    // 城市名显示为 "长沙 岳麓区"
    if (!adm2Val.empty() && !nameVal.empty()) {
        std::string cityDistrict = adm2Val + " " + nameVal;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cityDistrict.c_str(), -1, NULL, 0);
        if (wlen > 0 && wlen < 64) {
            MultiByteToWideChar(CP_UTF8, 0, cityDistrict.c_str(), -1, g_cityName, wlen);
            g_cityName[wlen - 1] = L'\0';
        }
    }

    OutputDebugStringA(("[Weather] Location: " + nameVal + " ID=" + idVal +
        " Lon=" + lonVal + " Lat=" + latVal + "\n").c_str());

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
    static std::atomic<bool> isFetching{ false };
    if (isFetching) return;
    isFetching = true;

    m_lastUpdateTime = GetTickCount64();

    std::thread([this]() {
        // 确保位置已获取
        FetchLocation();

        OutputDebugStringA("[Weather] Fetching weather...\n");

        // 使用 LocationID 查天气（更精确）
        char path[512];
        snprintf(path, sizeof(path),
            "/v7/weather/now?location=%s&key=%s",
            g_locationId, GetQWeatherKey());

        std::string resp = HttpGetGzip(QWEATHER_HOST, path, true);

        if (resp.empty()) {
            OutputDebugStringA("[Weather] Weather API returned empty response\n");
            isFetching = false;
            return;
        }

        OutputDebugStringA(("[Weather] Weather raw JSON: " + resp.substr(0, 200) + "\n").c_str());

        // 解析 JSON
        std::string code = ExtractJsonField(resp, "code");
        std::string temp = ExtractJsonField(resp, "temp");
        std::string text = ExtractJsonNestedField(resp, "now", "text");
        std::string humidity = ExtractJsonNestedField(resp, "now", "humidity");
        std::string windDir = ExtractJsonNestedField(resp, "now", "windDir");
        std::string windScale = ExtractJsonNestedField(resp, "now", "windScale");

        if (code != "200") {
            OutputDebugStringA(("[Weather] Weather API error code: " + code + "\n").c_str());
            isFetching = false;
            return;
        }

        // 处理天气文字描述
        std::wstring desc;
        if (!text.empty()) {
            std::string textUtf8 = text;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, textUtf8.c_str(), -1, NULL, 0);
            if (wlen > 0) {
                desc.resize(wlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, textUtf8.c_str(), -1, &desc[0], wlen);
            }
        }

        // 城市名 + 天气描述
        if (!desc.empty()) {
            m_description = std::wstring(g_cityName) + L" " + desc;
        } else {
            m_description = std::wstring(g_cityName) + L" 未知";
        }

        // 温度
        if (!temp.empty()) {
            m_temperature = (float)std::atof(temp.c_str());
        }

        // 调试输出
        wchar_t dbg[512];
        swprintf_s(dbg, L"[Weather] %ls | %sC | humidity: %s%% | wind: %s %s\n",
            m_description.c_str(), temp.c_str(), humidity.c_str(),
            windDir.c_str(), windScale.c_str());
        OutputDebugStringW(dbg);

        isFetching = false;
    }).detach();
}


