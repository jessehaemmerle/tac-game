#pragma once
// Runtime HLSL compilation (Shader Model 5.1, FXC via d3dcompiler_47.dll) with an on-disk bytecode
// cache and hot reload of changed sources (ADR-0003).
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "render/D3D12Common.h"

namespace it::render {

struct Bytecode {
    std::vector<uint8_t> data;
    D3D12_SHADER_BYTECODE bc() const { return {data.data(), data.size()}; }
    bool empty() const { return data.empty(); }
};

class ShaderLibrary {
public:
    bool init(const std::filesystem::path& shaderDir, const std::filesystem::path& cacheDir, bool debug);
    /// Compile `entry` of `file` for `target` ("vs_5_1", "ps_5_1", "cs_5_1") with "NAME=VALUE" defines.
    bool compile(const std::string& file, const std::string& entry, const std::string& target, const std::vector<std::string>& defines,
                 Bytecode& out);
    /// True if any shader source changed since the last call (development hot reload).
    bool sourcesChanged();
    const std::string& lastError() const { return lastError_; }
    const std::filesystem::path& dir() const { return dir_; }

private:
    using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                       ID3DBlob**);
    HMODULE dll_ = nullptr;
    CompileFn compile_ = nullptr;
    std::filesystem::path dir_, cache_;
    bool debug_ = false;
    std::map<std::string, std::filesystem::file_time_type> stamps_;
    std::string lastError_;
};

}  // namespace it::render
