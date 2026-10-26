#include "ui/Theme.h"

#include <filesystem>

namespace it::ui {

Fonts& fonts() {
    static Fonts f;
    return f;
}

ImVec4 sideColor(sim::Side s) {
    switch (s) {
        case sim::Side::Allies: return col::kAllies;
        case sim::Side::Axis: return col::kAxis;
        default: return col::kNeutral;
    }
}

void loadFonts(const std::string& dir) {
    ImGuiIO& io = ImGui::GetIO();
    auto load = [&](const char* file) -> ImFont* {
        std::filesystem::path p = std::filesystem::path(dir) / file;
        if (!std::filesystem::exists(p)) return nullptr;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        return io.Fonts->AddFontFromFileTTF(p.string().c_str(), fonts().base, &cfg);
    };
    Fonts& f = fonts();
    f.body = load("Barlow-Regular.ttf");
    f.bold = load("Barlow-SemiBold.ttf");
    f.heading = load("BarlowCondensed-SemiBold.ttf");
    f.mono = load("IBMPlexMono-Regular.ttf");
    if (!f.body) f.body = io.Fonts->AddFontDefault();
    if (!f.bold) f.bold = f.body;
    if (!f.heading) f.heading = f.bold;
    if (!f.mono) f.mono = f.body;
    io.FontDefault = f.body;
}

void applyTheme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowRounding = 3.0f;
    s.ChildRounding = 2.0f;
    s.FrameRounding = 2.0f;
    s.PopupRounding = 2.0f;
    s.GrabRounding = 2.0f;
    s.TabRounding = 2.0f;
    s.ScrollbarRounding = 2.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = {10, 9};
    s.FramePadding = {8, 4};
    s.ItemSpacing = {8, 6};
    s.ItemInnerSpacing = {6, 4};
    s.IndentSpacing = 16;
    s.ScrollbarSize = 13;
    s.WindowTitleAlign = {0.0f, 0.5f};
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextAlign = {0.0f, 0.5f};
    ImVec4* c = s.Colors;
    const ImVec4 bg{0.055f, 0.075f, 0.09f, 0.94f}, panel{0.08f, 0.105f, 0.125f, 1.0f}, frame{0.12f, 0.155f, 0.18f, 1.0f};
    const ImVec4 steel{0.20f, 0.29f, 0.35f, 1.0f}, steelHi{0.27f, 0.39f, 0.46f, 1.0f}, steelAct{0.33f, 0.47f, 0.55f, 1.0f};
    c[ImGuiCol_Text] = col::kText;
    c[ImGuiCol_TextDisabled] = col::kTextDim;
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = {0.07f, 0.09f, 0.11f, 0.98f};
    c[ImGuiCol_Border] = {0.24f, 0.30f, 0.33f, 0.65f};
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = {0.16f, 0.21f, 0.25f, 1.0f};
    c[ImGuiCol_FrameBgActive] = {0.19f, 0.25f, 0.29f, 1.0f};
    c[ImGuiCol_TitleBg] = panel;
    c[ImGuiCol_TitleBgActive] = {0.10f, 0.14f, 0.17f, 1.0f};
    c[ImGuiCol_TitleBgCollapsed] = panel;
    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_ScrollbarBg] = {0.04f, 0.05f, 0.06f, 0.6f};
    c[ImGuiCol_ScrollbarGrab] = steel;
    c[ImGuiCol_ScrollbarGrabHovered] = steelHi;
    c[ImGuiCol_ScrollbarGrabActive] = steelAct;
    c[ImGuiCol_CheckMark] = col::kBrass;
    c[ImGuiCol_SliderGrab] = col::kBrassDim;
    c[ImGuiCol_SliderGrabActive] = col::kBrass;
    c[ImGuiCol_Button] = steel;
    c[ImGuiCol_ButtonHovered] = steelHi;
    c[ImGuiCol_ButtonActive] = steelAct;
    c[ImGuiCol_Header] = {0.18f, 0.26f, 0.31f, 1.0f};
    c[ImGuiCol_HeaderHovered] = {0.23f, 0.33f, 0.39f, 1.0f};
    c[ImGuiCol_HeaderActive] = {0.28f, 0.40f, 0.47f, 1.0f};
    c[ImGuiCol_Separator] = {0.26f, 0.32f, 0.35f, 0.8f};
    c[ImGuiCol_SeparatorHovered] = col::kBrassDim;
    c[ImGuiCol_SeparatorActive] = col::kBrass;
    c[ImGuiCol_ResizeGrip] = {0.26f, 0.32f, 0.35f, 0.4f};
    c[ImGuiCol_ResizeGripHovered] = col::kBrassDim;
    c[ImGuiCol_ResizeGripActive] = col::kBrass;
    c[ImGuiCol_Tab] = {0.11f, 0.15f, 0.18f, 1.0f};
    c[ImGuiCol_TabHovered] = steelHi;
    c[ImGuiCol_TabSelected] = steel;
    c[ImGuiCol_TabSelectedOverline] = col::kBrass;
    c[ImGuiCol_TabDimmed] = {0.09f, 0.12f, 0.14f, 1.0f};
    c[ImGuiCol_TabDimmedSelected] = {0.15f, 0.21f, 0.25f, 1.0f};
    c[ImGuiCol_PlotLines] = col::kBrass;
    c[ImGuiCol_PlotHistogram] = col::kBrassDim;
    c[ImGuiCol_PlotHistogramHovered] = col::kBrass;
    c[ImGuiCol_TableHeaderBg] = {0.11f, 0.15f, 0.18f, 1.0f};
    c[ImGuiCol_TableBorderStrong] = {0.24f, 0.30f, 0.33f, 1.0f};
    c[ImGuiCol_TableBorderLight] = {0.17f, 0.21f, 0.24f, 1.0f};
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, 0.025f};
    c[ImGuiCol_TextSelectedBg] = {0.86f, 0.70f, 0.40f, 0.30f};
    c[ImGuiCol_NavCursor] = col::kBrass;
    c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.55f};
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

}  // namespace it::ui
