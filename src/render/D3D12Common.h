#pragma once
// Minimal helpers around Direct3D 12 (no d3dx12.h dependency): COM pointer, error checks and
// descriptor/resource description builders.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <string>
#include <utility>

#include "core/Log.h"

namespace it::render {

template <typename T> class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(const ComPtr& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ~ComPtr() { reset(); }
    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) {
            reset();
            p_ = o.p_;
            if (p_) p_->AddRef();
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            reset();
            p_ = std::exchange(o.p_, nullptr);
        }
        return *this;
    }
    void reset() {
        if (p_) p_->Release();
        p_ = nullptr;
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() {
        reset();
        return &p_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }
    template <typename U> ComPtr<U> as() const {
        ComPtr<U> out;
        if (p_) p_->QueryInterface(__uuidof(U), out.putVoid());
        return out;
    }

private:
    T* p_ = nullptr;
};

inline bool check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        IT_LOG_ERROR("D3D12: %s failed (hr=0x%08lx)", what, static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}
#define IT_CHECK(expr) ::it::render::check((expr), #expr)

/// Address of a temporary for D3D12 calls taking pointers to descriptions (valid for the full expression).
template <typename T> const T* addr(const T& v) { return &v; }

inline D3D12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CreationNodeMask = p.VisibleNodeMask = 1;
    return p;
}

inline D3D12_RESOURCE_DESC bufferDesc(uint64_t size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    return d;
}

inline D3D12_RESOURCE_DESC tex2DDesc(DXGI_FORMAT fmt, uint32_t w, uint32_t h, uint16_t mips = 1, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                     uint32_t samples = 1, uint16_t arraySize = 1) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = arraySize;
    d.MipLevels = mips;
    d.Format = fmt;
    d.SampleDesc.Count = samples;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = flags;
    return d;
}

inline D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                                         UINT sub = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = sub;
    return b;
}

inline D3D12_RESOURCE_BARRIER uavBarrier(ID3D12Resource* r) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = r;
    return b;
}

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace it::render
