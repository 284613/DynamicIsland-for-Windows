#pragma once

#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Log(LogLevel level, const std::wstring& message) {
#ifdef NDEBUG
        if (level == LogLevel::Debug) return;
#endif
        std::wstring line = FormatLine(level, message);

        std::lock_guard<std::mutex> lock(mutex_);
        EnsureOpen();
        if (file_.is_open()) {
            file_ << line;
            file_.flush();
        }
        OutputDebugStringW(line.c_str());
    }

private:
    Logger() = default;
    ~Logger() {
        if (file_.is_open()) file_.close();
    }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void EnsureOpen() {
        if (file_.is_open()) return;

        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::filesystem::path logDir = std::filesystem::path(exePath).parent_path() / L"logs";
        std::filesystem::create_directories(logDir);

        std::filesystem::path logFile = logDir / L"DynamicIsland.log";
        file_.open(logFile, std::ios::app | std::ios::out);
    }

    static std::wstring FormatLine(LogLevel level, const std::wstring& message) {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        struct tm local{};
        localtime_s(&local, &timeT);

        std::wostringstream wss;
        wss << L'['
            << std::setfill(L'0')
            << std::setw(4) << (1900 + local.tm_year) << L'-'
            << std::setw(2) << (local.tm_mon + 1) << L'-'
            << std::setw(2) << local.tm_mday << L' '
            << std::setw(2) << local.tm_hour << L':'
            << std::setw(2) << local.tm_min << L':'
            << std::setw(2) << local.tm_sec << L'.'
            << std::setw(3) << ms.count()
            << L"] ["
            << LevelTag(level)
            << L"] "
            << message
            << L'\n';
        return wss.str();
    }

    static const wchar_t* LevelTag(LogLevel level) {
        switch (level) {
        case LogLevel::Debug: return L"DEBUG";
        case LogLevel::Info:  return L"INFO ";
        case LogLevel::Warn:  return L"WARN ";
        case LogLevel::Error: return L"ERROR";
        default:              return L"?????";
        }
    }

    std::mutex mutex_;
    std::wofstream file_;
};

#define LOG_DEBUG(msg) ::Logger::Instance().Log(LogLevel::Debug, msg)
#define LOG_INFO(msg)  ::Logger::Instance().Log(LogLevel::Info,  msg)
#define LOG_WARN(msg)  ::Logger::Instance().Log(LogLevel::Warn,  msg)
#define LOG_ERROR(msg) ::Logger::Instance().Log(LogLevel::Error, msg)
