#pragma once
// Direct3D 12 device, swap chain, per-frame resources, descriptor heaps and upload ring.
#include <array>
#include <cstring>
#include <vector>

#include "render/D3D12Common.h"

namespace it::render {

constexpr uint32_t kFramesInFlight = 3;

class DescriptorHeap {
public:
    bool init(ID3D12Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);
    uint32_t alloc();
    void free(uint32_t index);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t i) const { return {cpuStart_.ptr + size_t(i) * inc_}; }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(uint32_t i) const { return {gpuStart_.ptr + uint64_t(i) * inc_}; }
    ID3D12DescriptorHeap* heap() const { return heap_.get(); }
    uint32_t increment() const { return inc_; }
    uint32_t capacity() const { return capacity_; }
    void setRange(uint32_t first, uint32_t end) {
        next_ = first;
        end_ = end;
    }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart_{};
    uint32_t inc_ = 0, capacity_ = 0, next_ = 0, end_ = 0;
    std::vector<uint32_t> free_;
};

struct UploadAlloc {
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    ID3D12Resource* resource = nullptr;
    uint64_t offset = 0;
};

/// GPU texture or buffer with tracked state and descriptor indices (staging heap / RTV / DSV).
struct GpuResource {
    ComPtr<ID3D12Resource> res;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0, height = 0, mips = 1, samples = 1, depth = 1;
    uint32_t srv = UINT32_MAX;               // staging (CPU) heap
    std::vector<uint32_t> uav;               // per mip, staging heap
    uint32_t rtv = UINT32_MAX, dsv = UINT32_MAX;
    uint32_t uiSrv = UINT32_MAX;             // persistent shader-visible SRV (ImGui images)
    ID3D12Resource* get() const { return res.get(); }
};

class GpuDevice {
public:
    bool init(HWND hwnd, uint32_t width, uint32_t height, bool debugLayer);
    void shutdown();
    bool resize(uint32_t width, uint32_t height);

    ID3D12GraphicsCommandList* beginFrame();
    void endFrame(bool vsync);
    void flush();

    ID3D12Device* device() const { return device_.get(); }
    ID3D12CommandQueue* queue() const { return queue_.get(); }
    ID3D12GraphicsCommandList* cmd() const { return cmd_.get(); }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t frameIndex() const { return frame_; }
    uint64_t frameCounter() const { return frameCounter_; }
    ID3D12Resource* backBuffer() const { return backBuffers_[frame_].get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv() const { return rtvHeap_.cpu(backBufferRtv_[frame_]); }
    DXGI_FORMAT backBufferFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }
    const std::string& adapterName() const { return adapterName_; }
    D3D_FEATURE_LEVEL featureLevel() const { return featureLevel_; }

    // Descriptors
    DescriptorHeap& rtvHeap() { return rtvHeap_; }
    DescriptorHeap& dsvHeap() { return dsvHeap_; }
    DescriptorHeap& stagingHeap() { return staging_; }
    DescriptorHeap& gpuHeap() { return gpuHeap_; }  // shader visible; persistent part [0, kPersistent)
    /// Allocate `count` contiguous transient shader-visible descriptors for this frame.
    uint32_t allocTable(uint32_t count);
    /// Copy staging descriptors into a new transient table and return its GPU handle.
    D3D12_GPU_DESCRIPTOR_HANDLE table(std::initializer_list<uint32_t> stagingIndices);
    D3D12_GPU_DESCRIPTOR_HANDLE table(const uint32_t* stagingIndices, uint32_t count);

    // Upload ring (constants, dynamic geometry)
    UploadAlloc upload(uint64_t size, uint64_t align = 256);
    template <typename T> D3D12_GPU_VIRTUAL_ADDRESS constants(const T& data) {
        UploadAlloc a = upload(sizeof(T));
        std::memcpy(a.cpu, &data, sizeof(T));
        return a.gpu;
    }

    // Resource creation helpers
    bool createTexture(GpuResource& out, DXGI_FORMAT fmt, uint32_t w, uint32_t h, uint32_t mips, D3D12_RESOURCE_FLAGS flags,
                       D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear = nullptr, uint32_t samples = 1,
                       uint32_t arraySize = 1, const char* name = nullptr);
    bool createBuffer(GpuResource& out, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial, const char* name = nullptr);
    /// Synchronous upload of initial data into a default-heap buffer.
    bool uploadBuffer(GpuResource& dst, const void* data, uint64_t size, D3D12_RESOURCE_STATES finalState);
    /// Synchronous upload of mip 0 of a 2D texture or of each slice of an array (tightly packed rows and slices).
    bool uploadTexture(GpuResource& dst, const void* data, uint32_t bytesPerPixel, D3D12_RESOURCE_STATES finalState, uint32_t slices = 1);
    void makeSrv(GpuResource& r, DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN);
    /// Extra SRV of a single mip level (staging heap index).
    uint32_t makeMipSrv(const GpuResource& r, uint32_t mip);
    /// Read-only depth-stencil view (for depth testing while the depth buffer is also sampled).
    uint32_t makeReadOnlyDsv(const GpuResource& r, DXGI_FORMAT fmt);
    void makeUav(GpuResource& r);
    void makeRtv(GpuResource& r);
    void makeDsv(GpuResource& r, DXGI_FORMAT fmt);
    void makeUiSrv(GpuResource& r);
    void makeBufferSrv(GpuResource& r, uint32_t elements, uint32_t stride);
    void release(GpuResource& r);
    void transition(GpuResource& r, D3D12_RESOURCE_STATES state);
    void deferRelease(ComPtr<ID3D12Resource> r);

    /// Read back the current back buffer (call after rendering, before endFrame). RGBA8.
    bool captureBackBuffer(std::vector<uint8_t>& rgba);

private:
    bool createSwapchainResources();
    void waitFence(uint64_t value);

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swap_;
    ComPtr<ID3D12GraphicsCommandList> cmd_;
    std::array<ComPtr<ID3D12CommandAllocator>, kFramesInFlight> alloc_;
    std::array<ComPtr<ID3D12Resource>, kFramesInFlight> backBuffers_;
    std::array<uint32_t, kFramesInFlight> backBufferRtv_{};
    std::array<uint64_t, kFramesInFlight> fenceValues_{};
    std::array<std::vector<ComPtr<ID3D12Resource>>, kFramesInFlight> deferred_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceCounter_ = 0;
    DescriptorHeap rtvHeap_, dsvHeap_, staging_, gpuHeap_;
    uint32_t tableStart_ = 0, tableEnd_ = 0, tableNext_ = 0;
    ComPtr<ID3D12Resource> uploadBuf_;
    uint8_t* uploadCpu_ = nullptr;
    uint64_t uploadSize_ = 0, uploadHead_ = 0, uploadFrameStart_ = 0, uploadFrameEnd_ = 0;
    uint32_t width_ = 0, height_ = 0, frame_ = 0;
    uint64_t frameCounter_ = 0;
    bool tearing_ = false;
    std::string adapterName_;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
    HWND hwnd_ = nullptr;
};

}  // namespace it::render
