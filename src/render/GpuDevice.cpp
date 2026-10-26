#include "render/GpuDevice.h"

#include <cstring>

namespace it::render {

// ------------------------------------------------------------------------------------- DescriptorHeap
bool DescriptorHeap::init(ID3D12Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible) {
    D3D12_DESCRIPTOR_HEAP_DESC d{};
    d.Type = type;
    d.NumDescriptors = capacity;
    d.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!IT_CHECK(dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(heap_.put())))) return false;
    cpuStart_ = heap_->GetCPUDescriptorHandleForHeapStart();
    if (shaderVisible) gpuStart_ = heap_->GetGPUDescriptorHandleForHeapStart();
    inc_ = dev->GetDescriptorHandleIncrementSize(type);
    capacity_ = capacity;
    next_ = 0;
    end_ = capacity;
    return true;
}

uint32_t DescriptorHeap::alloc() {
    if (!free_.empty()) {
        uint32_t i = free_.back();
        free_.pop_back();
        return i;
    }
    if (next_ >= end_) {
        IT_LOG_ERROR("Descriptor heap exhausted (%u)", capacity_);
        return end_ - 1;
    }
    return next_++;
}

void DescriptorHeap::free(uint32_t index) {
    if (index != UINT32_MAX) free_.push_back(index);
}

// ------------------------------------------------------------------------------------------ GpuDevice
bool GpuDevice::init(HWND hwnd, uint32_t width, uint32_t height, bool debugLayer) {
    hwnd_ = hwnd;
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    UINT factoryFlags = 0;
    if (debugLayer) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(dbg.put())))) {
            dbg->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
    if (!IT_CHECK(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(factory_.put())))) return false;

    // Prefer the high-performance GPU; fall back to any adapter that can create a device.
    std::vector<ComPtr<IDXGIAdapter1>> adapters;
    if (auto f6 = factory_.as<IDXGIFactory6>()) {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(a.put())) == DXGI_ERROR_NOT_FOUND) break;
            adapters.push_back(a);
        }
    }
    if (adapters.empty())
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory_->EnumAdapters1(i, a.put()) == DXGI_ERROR_NOT_FOUND) break;
            adapters.push_back(a);
        }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    for (auto& a : adapters) {
        DXGI_ADAPTER_DESC1 desc{};
        a->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(a.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.put())))) {
            adapter_ = a;
            adapterName_ = narrow(desc.Description);
            break;
        }
    }
    if (!device_ && !IT_CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device_.put())))) return false;
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
    fl.NumFeatureLevels = UINT(std::size(levels));
    fl.pFeatureLevelsRequested = levels;
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))) featureLevel_ = fl.MaxSupportedFeatureLevel;
    IT_LOG_INFO("D3D12 device: %s, feature level %x", adapterName_.c_str(), unsigned(featureLevel_));

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!IT_CHECK(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(queue_.put())))) return false;
    for (auto& a : alloc_)
        if (!IT_CHECK(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.put())))) return false;
    if (!IT_CHECK(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0].get(), nullptr, IID_PPV_ARGS(cmd_.put()))))
        return false;
    cmd_->Close();
    if (!IT_CHECK(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.put())))) return false;
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (!rtvHeap_.init(device_.get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256, false)) return false;
    if (!dsvHeap_.init(device_.get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64, false)) return false;
    if (!staging_.init(device_.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8192, false)) return false;
    if (!gpuHeap_.init(device_.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16384, true)) return false;
    constexpr uint32_t kPersistent = 2048;
    gpuHeap_.setRange(0, kPersistent);
    tableStart_ = kPersistent;

    uploadSize_ = 96ull << 20;
    if (!IT_CHECK(device_->CreateCommittedResource(addr(heapProps(D3D12_HEAP_TYPE_UPLOAD)), D3D12_HEAP_FLAG_NONE, addr(bufferDesc(uploadSize_)),
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(uploadBuf_.put()))))
        return false;
    D3D12_RANGE none{0, 0};
    uploadBuf_->Map(0, &none, reinterpret_cast<void**>(&uploadCpu_));

    if (hwnd) {
        if (auto f5 = factory_.as<IDXGIFactory5>()) {
            BOOL allow = FALSE;
            if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) tearing_ = allow;
        }
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = width_;
        sd.Height = height_;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = kFramesInFlight;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        ComPtr<IDXGISwapChain1> sc1;
        if (!IT_CHECK(factory_->CreateSwapChainForHwnd(queue_.get(), hwnd, &sd, nullptr, nullptr, sc1.put()))) return false;
        factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        swap_ = sc1.as<IDXGISwapChain3>();
        for (auto& i : backBufferRtv_) i = rtvHeap_.alloc();
        if (!createSwapchainResources()) return false;
    }
    return true;
}

bool GpuDevice::createSwapchainResources() {
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (!IT_CHECK(swap_->GetBuffer(i, IID_PPV_ARGS(backBuffers_[i].put())))) return false;
        device_->CreateRenderTargetView(backBuffers_[i].get(), nullptr, rtvHeap_.cpu(backBufferRtv_[i]));
    }
    frame_ = swap_->GetCurrentBackBufferIndex();
    return true;
}

bool GpuDevice::resize(uint32_t width, uint32_t height) {
    if (!swap_ || width == 0 || height == 0 || (width == width_ && height == height_)) return true;
    flush();
    for (auto& b : backBuffers_) b.reset();
    if (!IT_CHECK(swap_->ResizeBuffers(kFramesInFlight, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                       tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0)))
        return false;
    width_ = width;
    height_ = height;
    return createSwapchainResources();
}

void GpuDevice::waitFence(uint64_t value) {
    if (value == 0 || fence_->GetCompletedValue() >= value) return;
    fence_->SetEventOnCompletion(value, fenceEvent_);
    WaitForSingleObject(fenceEvent_, INFINITE);
}

void GpuDevice::flush() {
    if (!queue_) return;
    uint64_t v = ++fenceCounter_;
    queue_->Signal(fence_.get(), v);
    waitFence(v);
}

ID3D12GraphicsCommandList* GpuDevice::beginFrame() {
    if (swap_) frame_ = swap_->GetCurrentBackBufferIndex();
    else frame_ = uint32_t(frameCounter_ % kFramesInFlight);
    waitFence(fenceValues_[frame_]);
    deferred_[frame_].clear();
    alloc_[frame_]->Reset();
    cmd_->Reset(alloc_[frame_].get(), nullptr);
    uint64_t seg = uploadSize_ / kFramesInFlight;
    uploadFrameStart_ = seg * frame_;
    uploadFrameEnd_ = uploadFrameStart_ + seg;
    uploadHead_ = uploadFrameStart_;
    uint32_t span = (gpuHeap_.capacity() - tableStart_) / kFramesInFlight;
    tableNext_ = tableStart_ + span * frame_;
    tableEnd_ = tableNext_ + span;
    ID3D12DescriptorHeap* heaps[] = {gpuHeap_.heap()};
    cmd_->SetDescriptorHeaps(1, heaps);
    if (swap_) {
        auto b = render::transition(backBuffers_[frame_].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd_->ResourceBarrier(1, &b);
    }
    return cmd_.get();
}

void GpuDevice::endFrame(bool vsync) {
    if (swap_) {
        auto b = render::transition(backBuffers_[frame_].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        cmd_->ResourceBarrier(1, &b);
    }
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.get()};
    queue_->ExecuteCommandLists(1, lists);
    if (swap_) swap_->Present(vsync ? 1 : 0, (!vsync && tearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0);
    fenceValues_[frame_] = ++fenceCounter_;
    queue_->Signal(fence_.get(), fenceValues_[frame_]);
    ++frameCounter_;
}

void GpuDevice::shutdown() {
    flush();
    if (fenceEvent_) CloseHandle(fenceEvent_);
    fenceEvent_ = nullptr;
}

uint32_t GpuDevice::allocTable(uint32_t count) {
    if (tableNext_ + count > tableEnd_) {
        IT_LOG_ERROR("Descriptor table ring exhausted");
        tableNext_ = tableEnd_ - count;
    }
    uint32_t start = tableNext_;
    tableNext_ += count;
    return start;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuDevice::table(const uint32_t* idx, uint32_t count) {
    uint32_t start = allocTable(std::max(1u, count));
    for (uint32_t i = 0; i < count; ++i) {
        if (idx[i] == UINT32_MAX) continue;
        device_->CopyDescriptorsSimple(1, gpuHeap_.cpu(start + i), staging_.cpu(idx[i]), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    return gpuHeap_.gpu(start);
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuDevice::table(std::initializer_list<uint32_t> idx) { return table(idx.begin(), uint32_t(idx.size())); }

UploadAlloc GpuDevice::upload(uint64_t size, uint64_t align) {
    uint64_t start = (uploadHead_ + align - 1) & ~(align - 1);
    if (start + size > uploadFrameEnd_) {
        IT_LOG_ERROR("Upload ring overflow (%llu bytes)", (unsigned long long)size);
        start = uploadFrameStart_;
    }
    uploadHead_ = start + size;
    return {uploadCpu_ + start, uploadBuf_->GetGPUVirtualAddress() + start, uploadBuf_.get(), start};
}

bool GpuDevice::createTexture(GpuResource& out, DXGI_FORMAT fmt, uint32_t w, uint32_t h, uint32_t mips, D3D12_RESOURCE_FLAGS flags,
                              D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear, uint32_t samples, uint32_t arraySize,
                              const char* name) {
    release(out);
    auto desc = tex2DDesc(fmt, w, h, uint16_t(mips), flags, samples, uint16_t(arraySize));
    if (fmt == DXGI_FORMAT_D32_FLOAT && (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) desc.Format = DXGI_FORMAT_R32_TYPELESS;
    if (!IT_CHECK(device_->CreateCommittedResource(addr(heapProps(D3D12_HEAP_TYPE_DEFAULT)), D3D12_HEAP_FLAG_NONE, &desc, initial, clear,
                                                   IID_PPV_ARGS(out.res.put()))))
        return false;
    if (name) out.res->SetName(widen(name).c_str());
    out.state = initial;
    out.format = fmt;
    out.width = w;
    out.height = h;
    out.mips = mips;
    out.samples = samples;
    out.depth = arraySize;
    return true;
}

bool GpuDevice::createBuffer(GpuResource& out, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial, const char* name) {
    release(out);
    if (!IT_CHECK(device_->CreateCommittedResource(addr(heapProps(D3D12_HEAP_TYPE_DEFAULT)), D3D12_HEAP_FLAG_NONE, addr(bufferDesc(size, flags)), initial,
                                                   nullptr, IID_PPV_ARGS(out.res.put()))))
        return false;
    if (name) out.res->SetName(widen(name).c_str());
    out.state = initial;
    out.width = uint32_t(size);
    return true;
}

bool GpuDevice::uploadBuffer(GpuResource& dst, const void* data, uint64_t size, D3D12_RESOURCE_STATES finalState) {
    ComPtr<ID3D12Resource> staging;
    if (!IT_CHECK(device_->CreateCommittedResource(addr(heapProps(D3D12_HEAP_TYPE_UPLOAD)), D3D12_HEAP_FLAG_NONE, addr(bufferDesc(size)),
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.put()))))
        return false;
    void* p = nullptr;
    staging->Map(0, nullptr, &p);
    std::memcpy(p, data, size);
    staging->Unmap(0, nullptr);
    ComPtr<ID3D12CommandAllocator> a;
    ComPtr<ID3D12GraphicsCommandList> l;
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.put()));
    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a.get(), nullptr, IID_PPV_ARGS(l.put()));
    if (dst.state != D3D12_RESOURCE_STATE_COPY_DEST) {
        auto b = render::transition(dst.get(), dst.state, D3D12_RESOURCE_STATE_COPY_DEST);
        l->ResourceBarrier(1, &b);
    }
    l->CopyBufferRegion(dst.get(), 0, staging.get(), 0, size);
    auto b = render::transition(dst.get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    l->ResourceBarrier(1, &b);
    l->Close();
    ID3D12CommandList* lists[] = {l.get()};
    queue_->ExecuteCommandLists(1, lists);
    flush();
    dst.state = finalState;
    return true;
}

bool GpuDevice::uploadTexture(GpuResource& dst, const void* data, uint32_t bpp, D3D12_RESOURCE_STATES finalState, uint32_t slices) {
    auto desc = dst.res->GetDesc();
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(slices);
    UINT64 total = 0;
    // Mip 0 of every array slice: subresource index = slice * mips.
    for (uint32_t s = 0; s < slices; ++s) {
        UINT64 size = 0;
        device_->GetCopyableFootprints(&desc, s * dst.mips, 1, total, &fp[s], nullptr, nullptr, &size);
        total = (fp[s].Offset + size + 511) & ~UINT64(511);
    }
    ComPtr<ID3D12Resource> staging;
    if (!IT_CHECK(device_->CreateCommittedResource(addr(heapProps(D3D12_HEAP_TYPE_UPLOAD)), D3D12_HEAP_FLAG_NONE, addr(bufferDesc(total)),
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.put()))))
        return false;
    uint8_t* p = nullptr;
    staging->Map(0, nullptr, reinterpret_cast<void**>(&p));
    const size_t slicePitch = size_t(dst.width) * dst.height * bpp;
    for (uint32_t s = 0; s < slices; ++s)
        for (uint32_t y = 0; y < dst.height; ++y)
            std::memcpy(p + fp[s].Offset + size_t(y) * fp[s].Footprint.RowPitch,
                        static_cast<const uint8_t*>(data) + s * slicePitch + size_t(y) * dst.width * bpp, size_t(dst.width) * bpp);
    staging->Unmap(0, nullptr);
    ComPtr<ID3D12CommandAllocator> a;
    ComPtr<ID3D12GraphicsCommandList> l;
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.put()));
    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a.get(), nullptr, IID_PPV_ARGS(l.put()));
    if (dst.state != D3D12_RESOURCE_STATE_COPY_DEST) {
        auto b = render::transition(dst.get(), dst.state, D3D12_RESOURCE_STATE_COPY_DEST);
        l->ResourceBarrier(1, &b);
    }
    for (uint32_t s = 0; s < slices; ++s) {
        D3D12_TEXTURE_COPY_LOCATION d{}, src{};
        d.pResource = dst.get();
        d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        d.SubresourceIndex = s * dst.mips;
        src.pResource = staging.get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp[s];
        l->CopyTextureRegion(&d, 0, 0, 0, &src, nullptr);
    }
    auto b = render::transition(dst.get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    l->ResourceBarrier(1, &b);
    l->Close();
    ID3D12CommandList* lists[] = {l.get()};
    queue_->ExecuteCommandLists(1, lists);
    flush();
    dst.state = finalState;
    return true;
}

void GpuDevice::makeSrv(GpuResource& r, DXGI_FORMAT viewFormat) {
    if (r.srv == UINT32_MAX) r.srv = staging_.alloc();
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Format = viewFormat != DXGI_FORMAT_UNKNOWN ? viewFormat : (r.format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : r.format);
    if (r.samples > 1) {
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    } else if (r.depth > 1) {
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray.MipLevels = r.mips;
        d.Texture2DArray.ArraySize = r.depth;
    } else {
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Texture2D.MipLevels = r.mips;
    }
    device_->CreateShaderResourceView(r.get(), &d, staging_.cpu(r.srv));
}

uint32_t GpuDevice::makeMipSrv(const GpuResource& r, uint32_t mip) {
    uint32_t idx = staging_.alloc();
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Format = r.format;
    if (r.depth > 1) {
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray.MostDetailedMip = mip;
        d.Texture2DArray.MipLevels = 1;
        d.Texture2DArray.ArraySize = r.depth;
    } else {
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Texture2D.MostDetailedMip = mip;
        d.Texture2D.MipLevels = 1;
    }
    device_->CreateShaderResourceView(r.get(), &d, staging_.cpu(idx));
    return idx;
}

uint32_t GpuDevice::makeReadOnlyDsv(const GpuResource& r, DXGI_FORMAT fmt) {
    uint32_t idx = dsvHeap_.alloc();
    D3D12_DEPTH_STENCIL_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = r.samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    d.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    device_->CreateDepthStencilView(r.get(), &d, dsvHeap_.cpu(idx));
    return idx;
}

void GpuDevice::makeBufferSrv(GpuResource& r, uint32_t elements, uint32_t stride) {
    if (r.srv == UINT32_MAX) r.srv = staging_.alloc();
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    d.Buffer.NumElements = elements;
    d.Buffer.StructureByteStride = stride;
    device_->CreateShaderResourceView(r.get(), &d, staging_.cpu(r.srv));
}

void GpuDevice::makeUav(GpuResource& r) {
    r.uav.resize(r.mips, UINT32_MAX);
    for (uint32_t m = 0; m < r.mips; ++m) {
        if (r.uav[m] == UINT32_MAX) r.uav[m] = staging_.alloc();
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format = r.format;
        if (r.depth > 1) {
            d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray.MipSlice = m;
            d.Texture2DArray.ArraySize = r.depth;
        } else {
            d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            d.Texture2D.MipSlice = m;
        }
        device_->CreateUnorderedAccessView(r.get(), nullptr, &d, staging_.cpu(r.uav[m]));
    }
}

void GpuDevice::makeRtv(GpuResource& r) {
    if (r.rtv == UINT32_MAX) r.rtv = rtvHeap_.alloc();
    device_->CreateRenderTargetView(r.get(), nullptr, rtvHeap_.cpu(r.rtv));
}

void GpuDevice::makeDsv(GpuResource& r, DXGI_FORMAT fmt) {
    if (r.dsv == UINT32_MAX) r.dsv = dsvHeap_.alloc();
    D3D12_DEPTH_STENCIL_VIEW_DESC d{};
    d.Format = fmt;
    d.ViewDimension = r.samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    if (r.depth > 1) {
        d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray.ArraySize = r.depth;
    }
    device_->CreateDepthStencilView(r.get(), &d, dsvHeap_.cpu(r.dsv));
}

void GpuDevice::makeUiSrv(GpuResource& r) {
    if (r.srv == UINT32_MAX) makeSrv(r);
    if (r.uiSrv == UINT32_MAX) r.uiSrv = gpuHeap_.alloc();
    device_->CopyDescriptorsSimple(1, gpuHeap_.cpu(r.uiSrv), staging_.cpu(r.srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void GpuDevice::release(GpuResource& r) {
    if (r.res) deferRelease(r.res);
    r.res.reset();
    r.state = D3D12_RESOURCE_STATE_COMMON;
}

void GpuDevice::deferRelease(ComPtr<ID3D12Resource> res) { deferred_[frame_].push_back(std::move(res)); }

void GpuDevice::transition(GpuResource& r, D3D12_RESOURCE_STATES state) {
    if (!r.res || r.state == state) return;
    auto b = it::render::transition(r.get(), r.state, state);
    cmd_->ResourceBarrier(1, &b);
    r.state = state;
}

bool GpuDevice::captureBackBuffer(std::vector<uint8_t>& rgba) {
    ID3D12Resource* bb = backBuffer();
    if (!bb) return false;
    auto desc = bb->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &total);
    ComPtr<ID3D12Resource> rb;
    if (!IT_CHECK(device_->CreateCommittedResource(addr(heapProps(D3D12_HEAP_TYPE_READBACK)), D3D12_HEAP_FLAG_NONE, addr(bufferDesc(total)),
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(rb.put()))))
        return false;
    auto b0 = it::render::transition(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd_->ResourceBarrier(1, &b0);
    D3D12_TEXTURE_COPY_LOCATION d{}, s{};
    d.pResource = rb.get();
    d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    d.PlacedFootprint = fp;
    s.pResource = bb;
    s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cmd_->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    auto b1 = it::render::transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd_->ResourceBarrier(1, &b1);
    cmd_->Close();
    ID3D12CommandList* lists[] = {cmd_.get()};
    queue_->ExecuteCommandLists(1, lists);
    flush();
    uint8_t* p = nullptr;
    rb->Map(0, nullptr, reinterpret_cast<void**>(&p));
    rgba.resize(size_t(width_) * height_ * 4);
    for (uint32_t y = 0; y < height_; ++y) std::memcpy(rgba.data() + size_t(y) * width_ * 4, p + fp.Offset + size_t(y) * fp.Footprint.RowPitch, size_t(width_) * 4);
    rb->Unmap(0, nullptr);
    cmd_->Reset(alloc_[frame_].get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = {gpuHeap_.heap()};
    cmd_->SetDescriptorHeaps(1, heaps);
    return true;
}

}  // namespace it::render
