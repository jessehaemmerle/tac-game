#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace it::fs {

/// Root folder of game data ("data/"). Resolved at startup; overridable for tests/tools.
void setDataRoot(const std::filesystem::path& root);
const std::filesystem::path& dataRoot();
/// Try to locate the data root relative to the executable / working directory.
bool locateDataRoot(const std::filesystem::path& exeDir);
std::filesystem::path dataPath(const std::string& relative);

std::optional<std::string> readTextFile(const std::filesystem::path& path);
std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path);
bool writeTextFile(const std::filesystem::path& path, const std::string& content);
bool writeBinaryFile(const std::filesystem::path& path, const void* data, size_t size);
/// Files in a directory (non-recursive) with the given extension (".json"), sorted by name.
std::vector<std::filesystem::path> listFiles(const std::filesystem::path& dir, const std::string& ext);
/// Per-user writable directory (%LOCALAPPDATA%/IronTide on Windows, ~/.local/share/irontide elsewhere).
std::filesystem::path userDataDir();

}  // namespace it::fs
