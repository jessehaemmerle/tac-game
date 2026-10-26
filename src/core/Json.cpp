#include "core/Json.h"

#include "core/FileSystem.h"
#include "core/Log.h"

namespace it {

std::optional<Json> loadJsonFile(const std::filesystem::path& path, std::string* error) {
    auto text = fs::readTextFile(path);
    if (!text) {
        if (error) *error = "cannot read " + path.string();
        return std::nullopt;
    }
    try {
        return Json::parse(*text, nullptr, true, /*ignore_comments=*/true);
    } catch (const std::exception& e) {
        if (error) *error = path.string() + ": " + e.what();
        IT_LOG_ERROR("JSON parse error in %s: %s", path.string().c_str(), e.what());
        return std::nullopt;
    }
}

bool saveJsonFile(const std::filesystem::path& path, const Json& j) { return fs::writeTextFile(path, j.dump(2) + "\n"); }

}  // namespace it
