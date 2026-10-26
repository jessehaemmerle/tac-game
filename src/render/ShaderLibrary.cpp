#include "render/ShaderLibrary.h"

#include <d3dcompiler.h>

#include "core/FileSystem.h"

namespace it::render {
namespace {

uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    auto p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

class IncludeHandler : public ID3DInclude {
public:
    explicit IncludeHandler(const std::filesystem::path& dir) : dir_(dir) {}
    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* bytes) override {
        auto text = fs::readTextFile(dir_ / name);
        if (!text) return E_FAIL;
        files.push_back(std::move(*text));
        hash = fnv1a(files.back().data(), files.back().size(), hash);
        *data = files.back().data();
        *bytes = UINT(files.back().size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }
    std::vector<std::string> files;
    uint64_t hash = 0;

private:
    std::filesystem::path dir_;
};

}  // namespace

bool ShaderLibrary::init(const std::filesystem::path& shaderDir, const std::filesystem::path& cacheDir, bool debug) {
    dir_ = shaderDir;
    cache_ = cacheDir;
    debug_ = debug;
    std::error_code ec;
    std::filesystem::create_directories(cache_, ec);
    dll_ = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!dll_) {
        IT_LOG_ERROR("d3dcompiler_47.dll not found");
        return false;
    }
    compile_ = reinterpret_cast<CompileFn>(reinterpret_cast<void*>(GetProcAddress(dll_, "D3DCompile")));
    sourcesChanged();
    return compile_ != nullptr;
}

bool ShaderLibrary::compile(const std::string& file, const std::string& entry, const std::string& target, const std::vector<std::string>& defines,
                            Bytecode& out) {
    auto src = fs::readTextFile(dir_ / file);
    if (!src) {
        lastError_ = "missing shader " + file;
        IT_LOG_ERROR("%s", lastError_.c_str());
        return false;
    }
    std::vector<std::string> names, values;
    for (auto& d : defines) {
        auto eq = d.find('=');
        names.push_back(d.substr(0, eq));
        values.push_back(eq == std::string::npos ? "1" : d.substr(eq + 1));
    }
    std::vector<D3D_SHADER_MACRO> macros;
    for (size_t i = 0; i < names.size(); ++i) macros.push_back({names[i].c_str(), values[i].c_str()});
    macros.push_back({nullptr, nullptr});

    // Cache key: source, includes (resolved by a dry preprocess hash), entry, target, defines.
    IncludeHandler inc(dir_);
    std::string keyText = *src + "|" + entry + "|" + target + "|v3";
    for (auto& d : defines) keyText += "|" + d;
    // Hash includes by scanning #include lines directly (cheap and good enough for invalidation).
    uint64_t h = fnv1a(keyText.data(), keyText.size());
    size_t pos = 0;
    while ((pos = src->find("#include \"", pos)) != std::string::npos) {
        size_t s = pos + 10, e = src->find('"', s);
        if (e == std::string::npos) break;
        if (auto t = fs::readTextFile(dir_ / src->substr(s, e - s))) h = fnv1a(t->data(), t->size(), h);
        pos = e;
    }
    char keyName[32];
    std::snprintf(keyName, sizeof(keyName), "%016llx.cso", static_cast<unsigned long long>(h));
    auto cached = cache_ / keyName;
    if (!debug_)
        if (auto bin = fs::readBinaryFile(cached); bin && !bin->empty()) {
            out.data = std::move(*bin);
            return true;
        }
    UINT flags = debug_ ? (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION) : D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ComPtr<ID3DBlob> code, errors;
    std::string path = (dir_ / file).string();
    HRESULT hr = compile_(src->data(), src->size(), path.c_str(), macros.data(), &inc, entry.c_str(), target.c_str(), flags, 0, code.put(),
                          errors.put());
    if (FAILED(hr)) {
        lastError_ = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "compile failed";
        IT_LOG_ERROR("Shader %s:%s (%s) failed:\n%s", file.c_str(), entry.c_str(), target.c_str(), lastError_.c_str());
        return false;
    }
    out.data.assign(static_cast<const uint8_t*>(code->GetBufferPointer()),
                    static_cast<const uint8_t*>(code->GetBufferPointer()) + code->GetBufferSize());
    fs::writeBinaryFile(cached, out.data.data(), out.data.size());
    return true;
}

bool ShaderLibrary::sourcesChanged() {
    bool changed = false;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir_, ec)) {
        auto t = std::filesystem::last_write_time(e.path(), ec);
        auto key = e.path().filename().string();
        auto it = stamps_.find(key);
        if (it == stamps_.end() || it->second != t) {
            if (it != stamps_.end()) changed = true;
            stamps_[key] = t;
        }
    }
    return changed;
}

}  // namespace it::render
