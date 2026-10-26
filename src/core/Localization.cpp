#include "core/Localization.h"

#include <unordered_map>

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"

namespace it::loc {
namespace {
std::string g_lang = "en";
std::unordered_map<std::string, std::string> g_table;
std::unordered_map<std::string, std::string> g_fallback;

void flatten(const Json& j, const std::string& prefix, std::unordered_map<std::string, std::string>& out) {
    for (auto it = j.begin(); it != j.end(); ++it) {
        std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
        if (it->is_object())
            flatten(*it, key, out);
        else if (it->is_string())
            out[key] = it->get<std::string>();
    }
}

bool loadInto(const std::string& lang, std::unordered_map<std::string, std::string>& out) {
    out.clear();
    bool any = false;
    // Main table plus optional extra tables (loc/<lang>/*.json) for large content (e.g. unit histories).
    if (auto j = loadJsonFile(fs::dataPath("loc/" + lang + ".json"))) {
        flatten(*j, "", out);
        any = true;
    }
    for (auto& p : fs::listFiles(fs::dataPath("loc/" + lang), ".json"))
        if (auto j = loadJsonFile(p)) {
            flatten(*j, "", out);
            any = true;
        }
    return any;
}
}  // namespace

bool load(const std::string& language) {
    loadInto("en", g_fallback);
    g_lang = language;
    if (language == "en") {
        g_table = g_fallback;
        return !g_table.empty();
    }
    bool ok = loadInto(language, g_table);
    if (!ok) IT_LOG_WARN("Language '%s' not found, falling back to English", language.c_str());
    return ok;
}

const std::string& language() { return g_lang; }

std::vector<std::string> availableLanguages() {
    std::vector<std::string> out;
    for (auto& p : fs::listFiles(fs::dataPath("loc"), ".json")) out.push_back(p.stem().string());
    return out;
}

const char* tr(std::string_view key) {
    std::string k(key);
    if (auto it = g_table.find(k); it != g_table.end()) return it->second.c_str();
    if (auto it = g_fallback.find(k); it != g_fallback.end()) return it->second.c_str();
    // Remember the key itself so the returned pointer stays valid.
    auto [it, inserted] = g_fallback.emplace(k, k);
    return it->second.c_str();
}

bool has(std::string_view key) {
    std::string k(key);
    return g_table.count(k) || (g_fallback.count(k) && g_fallback[k] != k);
}

std::string format(std::string_view key, const std::vector<std::string>& args) {
    std::string s = tr(key);
    for (size_t i = 0; i < args.size(); ++i) {
        std::string token = "{" + std::to_string(i) + "}";
        for (size_t pos = s.find(token); pos != std::string::npos; pos = s.find(token, pos + args[i].size()))
            s.replace(pos, token.size(), args[i]);
    }
    return s;
}

void set(const std::string& key, const std::string& value) { g_table[key] = value; }

}  // namespace it::loc
