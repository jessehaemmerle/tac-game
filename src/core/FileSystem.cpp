#include "core/FileSystem.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace it::fs {
namespace {
std::filesystem::path g_root = "data";
}

void setDataRoot(const std::filesystem::path& root) { g_root = root; }
const std::filesystem::path& dataRoot() { return g_root; }

bool locateDataRoot(const std::filesystem::path& exeDir) {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates = {exeDir / "data", exeDir / ".." / "data", exeDir / ".." / ".." / "data",
                                                     std::filesystem::current_path(ec) / "data"};
    for (auto& c : candidates) {
        if (std::filesystem::exists(c / "db", ec)) {
            g_root = std::filesystem::weakly_canonical(c, ec);
            return true;
        }
    }
    return false;
}

std::filesystem::path dataPath(const std::string& relative) { return g_root / relative; }

std::optional<std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::nullopt;
    auto size = f.tellg();
    std::vector<uint8_t> data(size_t(std::max<std::streamoff>(0, size)));
    f.seekg(0);
    if (!data.empty()) f.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
    return data;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& content) {
    return writeBinaryFile(path, content.data(), content.size());
}

bool writeBinaryFile(const std::filesystem::path& path, const void* data, size_t size) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(f);
}

std::vector<std::filesystem::path> listFiles(const std::filesystem::path& dir, const std::string& ext) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ext) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

std::filesystem::path userDataDir() {
#ifdef _WIN32
    if (const char* p = std::getenv("LOCALAPPDATA")) return std::filesystem::path(p) / "IronTide";
    return std::filesystem::current_path() / "userdata";
#else
    if (const char* p = std::getenv("XDG_DATA_HOME")) return std::filesystem::path(p) / "irontide";
    if (const char* h = std::getenv("HOME")) return std::filesystem::path(h) / ".local" / "share" / "irontide";
    return std::filesystem::current_path() / "userdata";
#endif
}

}  // namespace it::fs
