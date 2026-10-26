#include "core/Log.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace it {
namespace {
LogLevel g_level = LogLevel::Info;
std::function<void(LogLevel, const std::string&)> g_sink;
std::mutex g_mutex;

std::string vformat(const char* fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n <= 0) return {};
    std::string out(size_t(n), '\0');
    std::vsnprintf(out.data(), out.size() + 1, fmt, args);
    return out;
}
}  // namespace

void setLogLevel(LogLevel level) { g_level = level; }
void setLogSink(std::function<void(LogLevel, const std::string&)> sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

std::string formatString(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string s = vformat(fmt, args);
    va_end(args);
    return s;
}

void logMessage(LogLevel level, const char* fmt, ...) {
    if (level < g_level) return;
    va_list args;
    va_start(args, fmt);
    std::string msg = vformat(fmt, args);
    va_end(args);
    static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::lock_guard lock(g_mutex);
    std::fprintf(level >= LogLevel::Warn ? stderr : stdout, "[%s] %s\n", names[int(level)], msg.c_str());
    if (g_sink) g_sink(level, msg);
}

}  // namespace it
