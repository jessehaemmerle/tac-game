#pragma once
// Visual identity of the interface: colours, ImGui style, fonts.
#include <string>

#include "imgui.h"
#include "sim/Types.h"

namespace it::ui {

namespace col {
constexpr ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline const ImVec4 kBrass{0.86f, 0.70f, 0.40f, 1.0f};
inline const ImVec4 kBrassDim{0.62f, 0.50f, 0.30f, 1.0f};
inline const ImVec4 kText{0.86f, 0.88f, 0.86f, 1.0f};
inline const ImVec4 kTextDim{0.58f, 0.62f, 0.63f, 1.0f};
inline const ImVec4 kGood{0.45f, 0.82f, 0.50f, 1.0f};
inline const ImVec4 kWarn{0.95f, 0.75f, 0.30f, 1.0f};
inline const ImVec4 kBad{0.95f, 0.36f, 0.30f, 1.0f};
inline const ImVec4 kAllies{0.42f, 0.66f, 1.0f, 1.0f};
inline const ImVec4 kAxis{0.98f, 0.40f, 0.34f, 1.0f};
inline const ImVec4 kUnknown{0.98f, 0.86f, 0.35f, 1.0f};
inline const ImVec4 kNeutral{0.7f, 0.72f, 0.74f, 1.0f};
}  // namespace col

ImVec4 sideColor(sim::Side s);

struct Fonts {
    ImFont* body = nullptr;     // Barlow Regular
    ImFont* bold = nullptr;     // Barlow SemiBold
    ImFont* heading = nullptr;  // Barlow Condensed SemiBold
    ImFont* mono = nullptr;     // IBM Plex Mono
    float base = 17.0f;
};
Fonts& fonts();

/// Load fonts from data/fonts and apply the style. `scale` multiplies sizes (DPI and user scale).
void applyTheme(float scale);
void loadFonts(const std::string& fontDir);

/// RAII helpers for fonts at a size relative to the body size.
struct FontScope {
    FontScope(ImFont* f, float relSize = 1.0f) { ImGui::PushFont(f, fonts().base * relSize); }
    ~FontScope() { ImGui::PopFont(); }
};

}  // namespace it::ui
