#pragma once
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include <string>
#include <cstdarg>

enum class LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    _ERROR
};

class Logger
{
public:
    Logger(const char *filePath) : m_file(fopen(filePath, "a")) {}
    static constexpr const char *filePath = "GlossTrainer.log";

    ~Logger()
    {
        if (m_file)
        {
            fflush(m_file);
            fclose(m_file);
        }
    }

    static void info(const char *tag, const std::string &format, ...)
    {
        // 获取参数
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
        va_end(args);

        Logger(filePath).Write(LogLevel::INFO, tag, buffer);
    }
    static void debug(const char *tag, const char *message)
    {
        Logger(filePath).Write(LogLevel::DEBUG, tag, message);
    }

    static void warning(const char *tag, const char *message)
    {
        Logger(filePath).Write(LogLevel::WARNING, tag, message);
    }

    static void error(const char *tag, const char *message)
    {
        Logger(filePath).Write(LogLevel::_ERROR, tag, message);
    }

    void Write(LogLevel level, const char *tag, const char *message, ...)
    {
        if (!m_file)
            return;

        const auto now = std::chrono::system_clock::now();
        const auto now_c = std::chrono::system_clock::to_time_t(now);
        std::fprintf(m_file, "[%s][%s][%s] %s\n", LevelToChar(level), TimeToString(now_c).c_str(), tag, message);
    }

private:
    FILE *m_file;

    static const char *LevelToChar(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::_ERROR:
            return "ERROR";
        default:
            return "";
        }
    }

    static std::string TimeToString(std::time_t time)
    {
        const auto ts = *std::localtime(&time);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &ts);
        return buf;
    }
};