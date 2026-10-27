// Iron Tide - Windows entry point: window, Direct3D 12 device, renderer, Dear ImGui, audio and the
// main loop. Command-line automation (--screen, --scenario, --screenshot ...) drives scripted
// captures for testing.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "audio/Audio.h"
#include "render/D3D12Common.h"

#include <shellapi.h>
#include "core/FileSystem.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "render/Renderer.h"
#include "ui/GameUi.h"
#include "ui/Theme.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace it;

namespace {

struct Options {
    ui::Automation automation;
    bool automate = false;
    std::string screenshot;
    uint32_t width = 1600, height = 900;
    bool sizeGiven = false;
    std::string language;
    bool debugLayer = false;
    int msaa = -1;
    bool noAudio = false;
};

std::string argValue(const std::vector<std::string>& args, const char* name, const std::string& def = {}) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) return args[i + 1];
    return def;
}
bool argFlag(const std::vector<std::string>& args, const char* name) {
    for (auto& a : args)
        if (a == name) return true;
    return false;
}

Options parseOptions(const std::vector<std::string>& args) {
    Options o;
    auto& a = o.automation;
    a.screen = argValue(args, "--screen");
    o.automate = !a.screen.empty();
    a.scenario = argValue(args, "--scenario", a.scenario);
    a.side = argValue(args, "--side", a.side);
    a.unitClass = argValue(args, "--class");
    a.focus = argValue(args, "--focus");
    a.flight = argValue(args, "--flight");
    a.advance = std::atof(argValue(args, "--advance", "0").c_str());
    a.frames = std::atoi(argValue(args, "--frames", "30").c_str());
    a.camYaw = std::atof(argValue(args, "--yaw", "-1").c_str());
    a.camPitch = std::atof(argValue(args, "--pitch", "-100").c_str());
    a.camDistance = std::atof(argValue(args, "--distance", "-1").c_str());
    a.insight = argFlag(args, "--insight");
    a.insightFilter = std::atoi(argValue(args, "--insight-filter", "0").c_str());
    a.pan = std::atof(argValue(args, "--pan", "0").c_str());
    a.lift = std::atof(argValue(args, "--lift", "0").c_str());
    a.large = argFlag(args, "--large");
    a.night = argFlag(args, "--night");
    a.drill = argFlag(args, "--drill");
    a.drillTime = std::atof(argValue(args, "--drill-time", "-1").c_str());
    o.screenshot = argValue(args, "--screenshot");
    o.language = argValue(args, "--lang");
    o.debugLayer = argFlag(args, "--d3d-debug");
    o.noAudio = argFlag(args, "--no-audio");
    o.msaa = std::atoi(argValue(args, "--msaa", "-1").c_str());
    std::string size = argValue(args, "--size");
    if (!size.empty()) {
        unsigned w = 0, h = 0;
        if (std::sscanf(size.c_str(), "%ux%u", &w, &h) == 2 && w > 0 && h > 0) {
            o.width = w;
            o.height = h;
            o.sizeGiven = true;
        }
    }
    return o;
}

class App : public ui::UiServices {
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    void shutdown();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // UiServices
    uint64_t shipPreview(const std::string& cls, const ui::PreviewView& v, uint32_t w, uint32_t h) override {
        render::PreviewParams p;
        p.yaw = v.yaw;
        p.pitch = v.pitch;
        p.zoom = v.zoom;
        p.focus = v.focus;
        p.lift = v.lift;
        p.night = v.night;
        p.animate = v.animate;
        p.drillTime = v.drillTime;
        return renderer_.shipPreview(cls, p, w, h);
    }
    uint64_t chartTexture() override { return renderer_.chartTexture(); }
    sim::EntityId pick3D(float x, float y) override { return renderer_.pick(ui_->camera(), x, y); }
    void settingsChanged(const ui::Settings& s) override;
    void playUiSound(const char* id) override { audio_.playUi(id); }
    std::string gpuDescription() override { return device_.adapterName(); }
    void quit() override { running_ = false; }

private:
    void frame(double dt);
    void setFullscreen(bool on);
    void saveScreenshot(const std::string& path);
    void applyScale();

    HWND hwnd_ = nullptr;
    render::GpuDevice device_;
    render::ShaderLibrary shaders_;
    render::Renderer renderer_;
    audio::AudioEngine audio_;
    ui::Settings settings_;
    std::unique_ptr<ui::GameUi> ui_;
    Options opt_;
    bool running_ = true, minimized_ = false, fullscreen_ = false;
    RECT windowedRect_{};
    float dpiScale_ = 1.0f, appliedScale_ = 0.0f;
    double shaderCheck_ = 0;
    bool imguiReady_ = false;
};

App* gApp = nullptr;
bool gQuietErrors = false;  // automated runs never block on message boxes

void fatal(HWND hwnd, const std::wstring& text) {
    IT_LOG_ERROR("%s", render::narrow(text).c_str());
    if (!gQuietErrors) MessageBoxW(hwnd, text.c_str(), L"Iron Tide", MB_ICONERROR);
}

LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (gApp) return gApp->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void imguiSrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    auto* dev = static_cast<render::GpuDevice*>(info->UserData);
    uint32_t i = dev->gpuHeap().alloc();
    *cpu = dev->gpuHeap().cpu(i);
    *gpu = dev->gpuHeap().gpu(i);
}

void imguiSrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    auto* dev = static_cast<render::GpuDevice*>(info->UserData);
    uint32_t i = uint32_t((cpu.ptr - dev->gpuHeap().cpu(0).ptr) / dev->gpuHeap().increment());
    dev->gpuHeap().free(i);
}

}  // namespace

bool App::init(HINSTANCE inst, const Options& opt) {
    opt_ = opt;
    auto settingsPath = (fs::userDataDir() / "settings.json").string();
    settings_.load(settingsPath);
    if (!opt.language.empty()) settings_.language = opt.language;
    if (opt.msaa > 0) settings_.msaa = opt.msaa;
    if (opt.automate) settings_.fullscreen = false;

    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"IronTideWindow";
    RegisterClassExW(&wc);
    RECT r{0, 0, LONG(opt.width), LONG(opt.height)};
    if (!opt.sizeGiven) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        r.right = LONG(std::min(sw * 85 / 100, 2560));
        r.bottom = LONG(std::min(sh * 85 / 100, 1440));
    }
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Iron Tide: Naval Command 1939–45", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd_) return false;
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
    RECT cr;
    GetClientRect(hwnd_, &cr);
    if (!device_.init(hwnd_, uint32_t(cr.right - cr.left), uint32_t(cr.bottom - cr.top), opt.debugLayer)) {
        fatal(hwnd_, L"Direct3D 12 could not be initialised.\nA DirectX 12 capable GPU (feature level 11_0 or newer) is required.");
        return false;
    }
    if (!shaders_.init(fs::dataPath("shaders"), fs::userDataDir() / "shadercache", false)) {
        fatal(hwnd_, L"The shader compiler (d3dcompiler_47.dll) is missing.");
        return false;
    }

    // Game data and UI.
    ui_ = std::make_unique<ui::GameUi>(*this, settings_);
    ui_->settingsPath = settingsPath;
    std::string err;
    if (!ui_->init(&err)) {
        std::wstring msg = L"Game data could not be loaded:\n" + render::widen(err);
        fatal(hwnd_, msg.c_str());
        return false;
    }
    render::RenderSettings rs;
    rs.msaa = uint32_t(std::max(1, settings_.msaa));
    rs.shadowSize = uint32_t(settings_.shadowSize);
    rs.vsync = settings_.vsync;
    rs.exposureBias = settings_.exposureBias;
    rs.bloom = settings_.bloom;
    rs.filmGrain = settings_.filmGrain && !opt.automate;
    if (!renderer_.init(device_, shaders_, ui_->db(), rs)) {
        fatal(hwnd_, L"The renderer could not be initialised (see log).");
        return false;
    }

    // Dear ImGui.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    dpiScale_ = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd_);
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX12_InitInfo ii;
    ii.Device = device_.device();
    ii.CommandQueue = device_.queue();
    ii.NumFramesInFlight = int(render::kFramesInFlight);
    ii.RTVFormat = device_.backBufferFormat();
    ii.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ii.UserData = &device_;
    ii.SrvDescriptorHeap = device_.gpuHeap().heap();
    ii.SrvDescriptorAllocFn = imguiSrvAlloc;
    ii.SrvDescriptorFreeFn = imguiSrvFree;
    ImGui_ImplDX12_Init(&ii);
    ui::loadFonts(fs::dataPath("fonts").string());
    applyScale();
    imguiReady_ = true;
    IT_LOG_INFO("ImGui ready");

    if (opt.noAudio || !audio_.init()) IT_LOG_WARN("Audio unavailable");
    audio_.loadBank(fs::dataPath("audio").string());
    audio_.setVolumes(settings_.masterVolume, settings_.sfxVolume, settings_.voiceVolume, settings_.ambienceVolume);
    audio_.setLanguage(settings_.language);

    if (settings_.fullscreen) setFullscreen(true);
    if (opt.automate) ui_->startAutomation(opt.automation);
    return true;
}

void App::applyScale() {
    float s = dpiScale_ * settings_.uiScale;
    if (std::abs(s - appliedScale_) < 0.001f) return;
    appliedScale_ = s;
    ui::applyTheme(s);
}

void App::settingsChanged(const ui::Settings& s) {
    audio_.setVolumes(s.masterVolume, s.sfxVolume, s.voiceVolume, s.ambienceVolume);
    audio_.setLanguage(s.language);
    auto& rs = renderer_.settings();
    rs.vsync = s.vsync;
    rs.exposureBias = s.exposureBias;
    rs.bloom = s.bloom;
    rs.filmGrain = s.filmGrain;
    if (s.fullscreen != fullscreen_) setFullscreen(s.fullscreen);
    applyScale();
}

void App::setFullscreen(bool on) {
    if (on == fullscreen_) return;
    fullscreen_ = on;
    if (on) {
        GetWindowRect(hwnd_, &windowedRect_);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd_, nullptr, windowedRect_.left, windowedRect_.top, windowedRect_.right - windowedRect_.left,
                     windowedRect_.bottom - windowedRect_.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    settings_.fullscreen = on;
}

LRESULT App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (imguiReady_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
        case WM_SIZE:
            minimized_ = wp == SIZE_MINIMIZED;
            if (!minimized_ && device_.device()) {
                uint32_t w = LOWORD(lp), h = HIWORD(lp);
                if (w && h && (w != device_.width() || h != device_.height())) {
                    device_.resize(w, h);
                    renderer_.resize(w, h);
                }
            }
            return 0;
        case WM_DPICHANGED:
            dpiScale_ = float(HIWORD(wp)) / 96.0f;
            appliedScale_ = 0;
            return 0;
        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN && (HIWORD(lp) & KF_ALTDOWN)) {
                setFullscreen(!fullscreen_);
                return 0;
            }
            break;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_CLOSE:
            running_ = false;
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::saveScreenshot(const std::string& path) {
    std::vector<uint8_t> rgba;
    if (!device_.captureBackBuffer(rgba)) return;
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    stbi_write_png(path.c_str(), int(device_.width()), int(device_.height()), 4, rgba.data(), int(device_.width() * 4));
    IT_LOG_INFO("Screenshot written: %s", path.c_str());
}

void App::frame(double dt) {
    applyScale();
    // Shader hot reload (development convenience; cheap directory scan once per second).
    shaderCheck_ += dt;
    if (shaderCheck_ > 1.0) {
        shaderCheck_ = 0;
        if (shaders_.sourcesChanged()) renderer_.reloadShaders();
    }
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    auto* cmd = device_.beginFrame();
    (void)cmd;
    ui_->frame(dt);
    if (ui_->consumeWorldChanged()) renderer_.setWorld(ui_->world());
    auto events = ui_->takeEvents();
    renderer_.onEvents(events);
    audio_.onEvents(events, ui_->world(), ui_->camera().pos, ui_->camera().yaw, ui_->playerSide(), ui_->simDtLastFrame() > 0 ? ui_->simDtLastFrame() / std::max(dt, 1e-3) : 1.0);
    audio_.update(dt, ui_->world(), ui_->camera().pos, ui_->paused());
    renderer_.update(dt, ui_->simDtLastFrame());
    if (ui_->wants3D() && ui_->world()) {
        render::SceneOptions so;
        so.selected = ui_->selectedShip();
        so.paused = ui_->paused();
        renderer_.render(ui_->camera(), so);
    } else {
        auto rtv = device_.backBufferRtv();
        const float clear[4] = {0.03f, 0.045f, 0.055f, 1.0f};
        device_.cmd()->ClearRenderTargetView(rtv, clear, 0, nullptr);
    }
    ImGui::Render();
    auto rtv = device_.backBufferRtv();
    device_.cmd()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), device_.cmd());
    if (ui_->automationReady()) {
        if (!opt_.screenshot.empty()) saveScreenshot(opt_.screenshot);
        ui_->finishAutomation();
        running_ = false;
    }
    device_.endFrame(renderer_.settings().vsync && !opt_.automate);
}

int App::run() {
    auto last = std::chrono::steady_clock::now();
    while (running_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running_ = false;
        }
        if (!running_) break;
        if (minimized_) {
            Sleep(30);
            last = std::chrono::steady_clock::now();
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        // Automated captures advance with a fixed step so results do not depend on machine speed.
        if (opt_.automate) dt = 1.0 / 30.0;
        auto t0 = std::chrono::steady_clock::now();
        frame(std::clamp(dt, 0.0, 0.25));
        if (opt_.automate)
            IT_LOG_INFO("frame %llu: %.0f ms", (unsigned long long)device_.frameCounter(),
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    return 0;
}

void App::shutdown() {
    device_.flush();
    if (imguiReady_) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    if (!opt_.automate) settings_.save((fs::userDataDir() / "settings.json").string());
    audio_.shutdown();
    renderer_.shutdown();
    device_.shutdown();
    if (hwnd_) DestroyWindow(hwnd_);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(render::narrow(argvW[i]));
    LocalFree(argvW);
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::string data = argValue(args, "--data");
    if (!data.empty()) fs::setDataRoot(data);
    else if (!fs::locateDataRoot(exeDir)) {
        fatal(nullptr, L"The 'data' folder was not found next to IronTide.exe.");
        return 1;
    }
    // Log to a file in the user folder (and to the debugger).
    static FILE* logFile = nullptr;
    std::error_code ec;
    std::filesystem::create_directories(fs::userDataDir(), ec);
    std::string logPath = argValue(args, "--log", (fs::userDataDir() / "irontide.log").string());
    logFile = std::fopen(logPath.c_str(), "w");
    setLogSink([](LogLevel, const std::string& line) {
        if (logFile) {
            std::fputs(line.c_str(), logFile);
            std::fputc('\n', logFile);
            std::fflush(logFile);
        }
        OutputDebugStringA((line + "\n").c_str());
    });
    setLogLevel(LogLevel::Info);
    Options opt = parseOptions(args);
    gQuietErrors = opt.automate;
    App app;
    gApp = &app;
    int rc = 1;
    if (app.init(inst, opt)) rc = app.run();
    app.shutdown();
    gApp = nullptr;
    if (logFile) std::fclose(logFile);
    return rc;
}
