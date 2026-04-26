#include "WeatherPlugin.h"

#include "Messages.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <zlib.h>

namespace {

constexpr char kQWeatherHost[] = "n94ewu37fy.re.qweatherapi.com";
constexpr wchar_t kDefaultCity[] = L"北京";
constexpr char kDefaultLocationId[] = "101010100";

std::mutex g_weatherConfigMutex;
std::string g_apiKey;
std::string g_cityQueryUtf8 = "北京";
std::string g_locationId = kDefaultLocationId;
std::wstring g_configCityName = kDefaultCity;
std::atomic<bool> g_locationFetched{ true };

struct WeatherSnapshot {
    std::wstring locationText = kDefaultCity;
    std::wstring description = L"Unknown";
    float temperature = 0.0f;
    std::wstring iconId;
    std::vector<HourlyForecast> hourly;
    std::vector<DailyForecast> daily;
    std::wstring lifeSuggestion;
    bool hasSevereWarning = false;
};

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }

    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }

    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }

    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }

    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), len, nullptr, nullptr);
    return utf8;
}

std::wstring TrimLineEndings(std::wstring line) {
    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
        line.pop_back();
    }
    return line;
}

bool StartsWithKey(const std::wstring& line, const std::wstring& key) {
    return line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == L'=';
}

std::wstring ReadUtf8IniValue(const std::wstring& path, const std::wstring& section, const std::wstring& key, const std::wstring& fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return fallback;
    }

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring text = Utf8ToWide(bytes);
    if (text.empty()) {
        return fallback;
    }

    std::wistringstream stream(text);
    std::wstring line;
    std::wstring currentSection;
    const std::wstring targetSection = L"[" + section + L"]";
    while (std::getline(stream, line)) {
        line = TrimLineEndings(line);
        if (line.empty()) {
            continue;
        }
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

std::wstring GetConfigPath() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }

    wchar_t configPath[MAX_PATH] = {};
    wcscpy_s(configPath, exePath);
    wcscat_s(configPath, L"config.ini");
    return configPath;
}

void LoadConfig() {
    const std::wstring configPath = GetConfigPath();

    wchar_t keyBuf[256] = {};
    wchar_t locationBuf[64] = {};
    GetPrivateProfileStringW(L"Weather", L"APIKey", L"", keyBuf, _countof(keyBuf), configPath.c_str());
    GetPrivateProfileStringW(L"Weather", L"LocationId", L"", locationBuf, _countof(locationBuf), configPath.c_str());
    const std::wstring cityValue = ReadUtf8IniValue(configPath, L"Weather", L"City", kDefaultCity);

    std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
    g_apiKey = WideToUtf8(std::wstring(keyBuf));
    g_cityQueryUtf8 = WideToUtf8(cityValue);
    if (g_cityQueryUtf8.empty()) {
        g_cityQueryUtf8 = "北京";
    }
    g_configCityName = cityValue.empty() ? std::wstring(kDefaultCity) : cityValue;
    g_locationId = WideToUtf8(std::wstring(locationBuf));
    if (g_locationId.empty()) {
        g_locationFetched.store(false);
    } else {
        g_locationFetched.store(true);
    }
}

std::string UrlEncode(const std::string& value) {
    std::string encoded;
    char buffer[8] = {};
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%%%02X", ch);
            encoded += buffer;
        }
    }
    return encoded;
}

std::string HttpGetGzip(const char* host, const char* path, bool useHttps) {
    std::string response;

    const std::wstring wideHost = Utf8ToWide(host ? host : "");
    const std::wstring widePath = Utf8ToWide(path ? path : "");
    if (wideHost.empty() || widePath.empty()) {
        return response;
    }

    HINTERNET hSession = WinHttpOpen(
        L"DynamicIsland/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        return response;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    const INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET hConnect = WinHttpConnect(hSession, wideHost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return response;
    }

    const DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        widePath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        std::string compressed;
        DWORD size = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &size) || size == 0) {
                break;
            }

            std::string buffer(size, '\0');
            DWORD downloaded = 0;
            if (WinHttpReadData(hRequest, buffer.data(), size, &downloaded)) {
                buffer.resize(downloaded);
                compressed += buffer;
            }
        } while (size > 0);

        if (!compressed.empty()) {
            z_stream stream{};
            stream.next_in = reinterpret_cast<Bytef*>(compressed.data());
            stream.avail_in = static_cast<uInt>(compressed.size());

            if (inflateInit2(&stream, 16 + MAX_WBITS) == Z_OK) {
                unsigned char outBuffer[8192] = {};
                int ret = Z_OK;
                while (ret != Z_STREAM_END) {
                    stream.next_out = outBuffer;
                    stream.avail_out = sizeof(outBuffer);
                    ret = inflate(&stream, Z_NO_FLUSH);
                    if (ret != Z_OK && ret != Z_STREAM_END) {
                        response.clear();
                        break;
                    }
                    const size_t written = sizeof(outBuffer) - stream.avail_out;
                    response.append(reinterpret_cast<char*>(outBuffer), written);
                }
                inflateEnd(&stream);
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

std::string ExtractJsonField(const std::string& json, const char* key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }

    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) {
        return {};
    }
    ++pos;

    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return {};
    }

    if (json[pos] == '"') {
        ++pos;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
            }
            value += json[pos++];
        }
        return value;
    }

    std::string value;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
        if (!std::isspace(static_cast<unsigned char>(json[pos]))) {
            value += json[pos];
        }
        ++pos;
    }
    return value;
}

std::string ExtractJsonNestedField(const std::string& json, const char* outer, const char* inner) {
    const std::string pattern = "\"" + std::string(outer) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }

    pos = json.find('{', pos + pattern.length());
    if (pos == std::string::npos) {
        return {};
    }

    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }

    if (end == std::string::npos) {
        return {};
    }

    return ExtractJsonField(json.substr(pos, end - pos + 1), inner);
}

std::string ExtractJsonArrayField(const std::string& json, const char* arrayName, int index, const char* field) {
    const std::string pattern = "\"" + std::string(arrayName) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }

    pos = json.find('[', pos + pattern.length());
    if (pos == std::string::npos) {
        return {};
    }

    size_t objectStart = pos;
    for (int i = 0; i <= index; ++i) {
        objectStart = json.find('{', objectStart + (i == 0 ? 0 : 1));
        if (objectStart == std::string::npos) {
            return {};
        }

        if (i == index) {
            int depth = 0;
            size_t objectEnd = std::string::npos;
            for (size_t j = objectStart; j < json.size(); ++j) {
                if (json[j] == '{') ++depth;
                else if (json[j] == '}') {
                    --depth;
                    if (depth == 0) {
                        objectEnd = j;
                        break;
                    }
                }
            }
            if (objectEnd == std::string::npos) {
                return {};
            }
            return ExtractJsonField(json.substr(objectStart, objectEnd - objectStart + 1), field);
        }
    }

    return {};
}

bool FetchLocationIfNeeded(std::string& locationId) {
    if (!locationId.empty()) {
        return true;
    }

    std::string cityQuery;
    std::string apiKey;
    {
        std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
        cityQuery = g_cityQueryUtf8;
        apiKey = g_apiKey;
    }

    if (apiKey.empty()) {
        return false;
    }

    const std::string path =
        "/geo/v2/city/lookup?location=" + UrlEncode(cityQuery) + "&range=cn&number=1&key=" + apiKey;
    const std::string response = HttpGetGzip(kQWeatherHost, path.c_str(), true);
    if (response.empty() || ExtractJsonField(response, "code") != "200") {
        return false;
    }

    const std::string fetchedLocationId = ExtractJsonArrayField(response, "location", 0, "id");
    if (fetchedLocationId.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
        g_locationId = fetchedLocationId;
        g_locationFetched.store(true);
    }
    locationId = fetchedLocationId;
    return true;
}

std::wstring ToWideOrDefault(const std::string& utf8, const std::wstring& fallback = {}) {
    const std::wstring wide = Utf8ToWide(utf8);
    return wide.empty() ? fallback : wide;
}

} // namespace

std::wstring WeatherPlugin::GetLocationText() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_locationText;
}

std::wstring WeatherPlugin::GetWeatherDescription() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_description;
}

float WeatherPlugin::GetTemperature() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_temperature;
}

std::wstring WeatherPlugin::GetIconId() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_iconId;
}

std::vector<HourlyForecast> WeatherPlugin::GetHourlyForecast() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_hourlyForecasts;
}

std::vector<DailyForecast> WeatherPlugin::GetDailyForecast() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_dailyForecasts;
}

std::wstring WeatherPlugin::GetLifeSuggestion() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lifeSuggestion;
}

bool WeatherPlugin::HasSevereWarning() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_hasSevereWarning;
}

bool WeatherPlugin::IsAvailable() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_isAvailable;
}

WeatherPlugin::WeatherPlugin() {
    SetUnavailableState(kDefaultCity, L"未配置 API Key");
}

WeatherPlugin::~WeatherPlugin() {
    Shutdown();
}

PluginInfo WeatherPlugin::GetInfo() const {
    PluginInfo info{};
    info.name = L"WeatherPlugin";
    info.description = L"Weather information";
    return info;
}

bool WeatherPlugin::Initialize() {
    m_shutdownRequested.store(false);
    LoadConfig();

    std::wstring configuredCity = kDefaultCity;
    {
        std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
        if (!g_configCityName.empty()) {
            configuredCity = g_configCityName;
        }
    }

    if (g_apiKey.empty()) {
        SetUnavailableState(configuredCity, L"未配置 API Key");
        return true;
    }

    FetchWeather();
    return true;
}

void WeatherPlugin::Shutdown() {
    m_shutdownRequested.store(true);
    JoinFetchThread();
}

void WeatherPlugin::Update(float deltaTime) {
    (void)deltaTime;
    if (m_shutdownRequested.load()) {
        return;
    }

    const size_t now = GetTickCount64();
    if (m_lastUpdateTime != 0 && (now - m_lastUpdateTime) < 3 * 60 * 1000) {
        return;
    }

    FetchWeather();
}

void WeatherPlugin::JoinFetchThread() {
    if (m_fetchThread.joinable()) {
        m_fetchThread.join();
    }
}

void WeatherPlugin::SetUnavailableState(const std::wstring& locationText, const std::wstring& reason) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_locationText = locationText.empty() ? std::wstring(kDefaultCity) : locationText;
    m_description = reason;
    m_temperature = 0.0f;
    m_iconId.clear();
    m_hourlyForecasts.clear();
    m_dailyForecasts.clear();
    m_lifeSuggestion.clear();
    m_hasSevereWarning = false;
    m_isAvailable = false;
}

void WeatherPlugin::FetchWeather() {
    LoadConfig();

    std::wstring configuredCity = kDefaultCity;
    std::string apiKey;
    std::string locationId;
    {
        std::lock_guard<std::mutex> lock(g_weatherConfigMutex);
        configuredCity = g_configCityName.empty() ? std::wstring(kDefaultCity) : g_configCityName;
        apiKey = g_apiKey;
        locationId = g_locationId;
    }

    if (apiKey.empty()) {
        SetUnavailableState(configuredCity, L"未配置 API Key");
        m_lastUpdateTime = GetTickCount64();
        return;
    }

    if (m_fetchInProgress.load()) {
        return;
    }

    JoinFetchThread();
    m_fetchInProgress.store(true);
    m_lastUpdateTime = GetTickCount64();

    m_fetchThread = std::thread([this, apiKey, locationId, configuredCity]() mutable {
        WeatherSnapshot snapshot;
        snapshot.locationText = configuredCity;
        snapshot.iconId = L"100";
        bool success = false;

        if (FetchLocationIfNeeded(locationId)) {
            const std::string nowPath = "/v7/weather/now?location=" + locationId + "&key=" + apiKey;
            const std::string nowResponse = HttpGetGzip(kQWeatherHost, nowPath.c_str(), true);
            if (!nowResponse.empty() && ExtractJsonField(nowResponse, "code") == "200") {
                snapshot.description = ToWideOrDefault(ExtractJsonNestedField(nowResponse, "now", "text"), L"Unknown");
                snapshot.iconId = ToWideOrDefault(ExtractJsonNestedField(nowResponse, "now", "icon"));
                const std::string temp = ExtractJsonNestedField(nowResponse, "now", "temp");
                if (!temp.empty()) {
                    snapshot.temperature = static_cast<float>(std::atof(temp.c_str()));
                }
                success = true;
            }

            const std::string hourlyPath = "/v7/weather/24h?location=" + locationId + "&key=" + apiKey;
            const std::string hourlyResponse = HttpGetGzip(kQWeatherHost, hourlyPath.c_str(), true);
            if (!hourlyResponse.empty() && ExtractJsonField(hourlyResponse, "code") == "200") {
                for (int i = 0; i < 6; ++i) {
                    HourlyForecast forecast;
                    const std::string fxTime = ExtractJsonArrayField(hourlyResponse, "hourly", i, "fxTime");
                    const std::string temp = ExtractJsonArrayField(hourlyResponse, "hourly", i, "temp");
                    if (fxTime.empty() || temp.empty()) {
                        break;
                    }

                    size_t timePos = fxTime.find('T');
                    if (timePos != std::string::npos && timePos + 6 <= fxTime.size()) {
                        forecast.time = ToWideOrDefault(fxTime.substr(timePos + 1, 5));
                    }
                    forecast.icon = ToWideOrDefault(ExtractJsonArrayField(hourlyResponse, "hourly", i, "icon"));
                    forecast.text = ToWideOrDefault(ExtractJsonArrayField(hourlyResponse, "hourly", i, "text"));
                    forecast.temp = static_cast<float>(std::atof(temp.c_str()));
                    snapshot.hourly.push_back(std::move(forecast));
                }
            }

            const std::string indexPath = "/v7/indices/1d?location=" + locationId + "&key=" + apiKey + "&type=1,3,9";
            const std::string indexResponse = HttpGetGzip(kQWeatherHost, indexPath.c_str(), true);
            if (!indexResponse.empty() && ExtractJsonField(indexResponse, "code") == "200") {
                snapshot.lifeSuggestion = ToWideOrDefault(ExtractJsonArrayField(indexResponse, "daily", 0, "text"));
            }

            const std::string dailyPath = "/v7/weather/7d?location=" + locationId + "&key=" + apiKey;
            const std::string dailyResponse = HttpGetGzip(kQWeatherHost, dailyPath.c_str(), true);
            if (!dailyResponse.empty() && ExtractJsonField(dailyResponse, "code") == "200") {
                for (int i = 0; i < 7; ++i) {
                    const std::string date = ExtractJsonArrayField(dailyResponse, "daily", i, "fxDate");
                    const std::string maxTemp = ExtractJsonArrayField(dailyResponse, "daily", i, "tempMax");
                    const std::string minTemp = ExtractJsonArrayField(dailyResponse, "daily", i, "tempMin");
                    if (date.empty() || maxTemp.empty()) {
                        break;
                    }

                    DailyForecast forecast;
                    forecast.date = ToWideOrDefault(date.size() >= 7 ? date.substr(5) : date);
                    forecast.iconDay = ToWideOrDefault(ExtractJsonArrayField(dailyResponse, "daily", i, "iconDay"));
                    forecast.textDay = ToWideOrDefault(ExtractJsonArrayField(dailyResponse, "daily", i, "textDay"));
                    forecast.tempMax = static_cast<float>(std::atof(maxTemp.c_str()));
                    forecast.tempMin = static_cast<float>(std::atof(minTemp.c_str()));
                    snapshot.daily.push_back(std::move(forecast));
                }
            }
        }

        if (success && !m_shutdownRequested.load()) {
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_locationText = snapshot.locationText;
                m_description = snapshot.description;
                m_temperature = snapshot.temperature;
                m_iconId = snapshot.iconId;
                m_hourlyForecasts = snapshot.hourly;
                m_dailyForecasts = snapshot.daily;
                m_lifeSuggestion = snapshot.lifeSuggestion;
                m_hasSevereWarning = snapshot.hasSevereWarning;
                m_isAvailable = true;
            }

            if (m_notifyHwnd && !m_shutdownRequested.load()) {
                PostMessageW(m_notifyHwnd, WM_WEATHER_UPDATED, 0, 0);
            }
        } else if (!success) {
            m_lastUpdateTime = 0;
        }

        m_fetchInProgress.store(false);
    });
}
