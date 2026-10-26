#pragma once
// String table localisation. Keys are dotted identifiers ("menu.start"); files are data/loc/<lang>.json
// (nested objects are flattened). Missing keys fall back to English, then to the key itself.
#include <string>
#include <string_view>
#include <vector>

namespace it::loc {

bool load(const std::string& language);  // "de" or "en"
const std::string& language();
std::vector<std::string> availableLanguages();
const char* tr(std::string_view key);
bool has(std::string_view key);
/// Replace {0}, {1}, ... with the given arguments.
std::string format(std::string_view key, const std::vector<std::string>& args);
/// Register/override a string at runtime (tests, mods).
void set(const std::string& key, const std::string& value);

}  // namespace it::loc

namespace it {
inline const char* tr(std::string_view key) { return loc::tr(key); }
}
