#pragma once
#include <cstdarg>
#include <functional>
#include <string>

namespace it {

enum class LogLevel { Debug, Info, Warn, Error };

// MinGW builds use the C99-conforming mingw stdio (__USE_MINGW_ANSI_STDIO, see CMakeLists.txt), so the
// format check must follow GNU rules rather than the old msvcrt ones (which reject %zu).
#if defined(__MINGW32__)
#define IT_PRINTF_FORMAT(f, a) __attribute__((format(gnu_printf, f, a)))
#elif defined(__GNUC__)
#define IT_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define IT_PRINTF_FORMAT(f, a)
#endif

void logMessage(LogLevel level, const char* fmt, ...) IT_PRINTF_FORMAT(2, 3);
void setLogLevel(LogLevel level);
/// Additional sink (e.g. in-game console). Receives formatted lines.
void setLogSink(std::function<void(LogLevel, const std::string&)> sink);
std::string formatString(const char* fmt, ...) IT_PRINTF_FORMAT(1, 2);

#define IT_LOG_DEBUG(...) ::it::logMessage(::it::LogLevel::Debug, __VA_ARGS__)
#define IT_LOG_INFO(...) ::it::logMessage(::it::LogLevel::Info, __VA_ARGS__)
#define IT_LOG_WARN(...) ::it::logMessage(::it::LogLevel::Warn, __VA_ARGS__)
#define IT_LOG_ERROR(...) ::it::logMessage(::it::LogLevel::Error, __VA_ARGS__)

}  // namespace it
