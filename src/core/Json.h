#pragma once
// JSON helpers on top of nlohmann::json with defaulting getters that never throw on missing keys.
#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include "core/Math.h"

namespace it {

using Json = nlohmann::json;

std::optional<Json> loadJsonFile(const std::filesystem::path& path, std::string* error = nullptr);
bool saveJsonFile(const std::filesystem::path& path, const Json& j);

template <typename T> T jget(const Json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (...) {
        return def;
    }
}
inline std::string jstr(const Json& j, const char* key, const std::string& def = {}) { return jget<std::string>(j, key, def); }
inline double jnum(const Json& j, const char* key, double def = 0.0) { return jget<double>(j, key, def); }
inline int jint(const Json& j, const char* key, int def = 0) { return jget<int>(j, key, def); }
inline bool jbool(const Json& j, const char* key, bool def = false) { return jget<bool>(j, key, def); }
inline Vec2 jvec2(const Json& j, const char* key, Vec2 def = {}) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 2) return def;
    return {(*it)[0].get<double>(), (*it)[1].get<double>()};
}
inline Vec3 jvec3(const Json& j, const char* key, Vec3 def = {}) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3) return def;
    return {(*it)[0].get<double>(), (*it)[1].get<double>(), (*it)[2].get<double>()};
}
inline const Json& jobj(const Json& j, const char* key) {
    static const Json empty = Json::object();
    auto it = j.find(key);
    return (it != j.end() && it->is_object()) ? *it : empty;
}
inline const Json& jarr(const Json& j, const char* key) {
    static const Json empty = Json::array();
    auto it = j.find(key);
    return (it != j.end() && it->is_array()) ? *it : empty;
}

}  // namespace it
