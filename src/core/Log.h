#pragma once
#include <cstdarg>
#include <functional>
#include <string>

namespace it {

enum class LogLevel { Debug, Info, Warn, Error };

void logMessage(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void setLogLevel(LogLevel level);
/// Additional sink (e.g. in-game console). Receives formatted lines.
void setLogSink(std::function<void(LogLevel, const std::string&)> sink);
std::string formatString(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#define IT_LOG_DEBUG(...) ::it::logMessage(::it::LogLevel::Debug, __VA_ARGS__)
#define IT_LOG_INFO(...) ::it::logMessage(::it::LogLevel::Info, __VA_ARGS__)
#define IT_LOG_WARN(...) ::it::logMessage(::it::LogLevel::Warn, __VA_ARGS__)
#define IT_LOG_ERROR(...) ::it::logMessage(::it::LogLevel::Error, __VA_ARGS__)

}  // namespace it
