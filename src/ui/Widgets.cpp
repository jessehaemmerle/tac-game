#include "ui/Widgets.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "core/Localization.h"
#include "core/Math.h"
#include "ui/Format.h"
#include "ui/Theme.h"

namespace it::ui {

ImU32 toU32(const ImVec4& c, float a) { return ImGui::ColorConvertFloat4ToU32({c.x, c.y, c.z, c.w * a}); }

void textColored(const ImVec4& c, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ImGui::TextColored(c, "%s", buf);
}

void sectionHeader(const char* text) {
    FontScope f(fonts().heading, 1.12f);
    ImGui::PushStyleColor(ImGuiCol_Text, col::kBrass);
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool toggleButton(const char* label, bool active, const ImVec2& size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.42f, 0.34f, 0.18f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.52f, 0.42f, 0.22f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.60f, 0.48f, 0.26f, 1.0f});
    }
    bool r = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor(3);
    return r;
}

void statusBar(const char* label, float f, const ImVec4& color, const char* overlay, float width) {
    f = std::clamp(f, 0.0f, 1.0f);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    char buf[32];
    if (!overlay) {
        std::snprintf(buf, sizeof(buf), "%d %%", int(std::lround(f * 100.0f)));
        overlay = buf;
    }
    ImGui::ProgressBar(f, {width, ImGui::GetFontSize() * 0.95f}, overlay);
    ImGui::PopStyleColor();
}

bool compassInput(const char* id, double& ordered, double heading, float r) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size{r * 2.0f + 8.0f, r * 2.0f + 8.0f};
    ImGui::InvisibleButton("##compass", size);
    bool active = ImGui::IsItemActive();
    ImVec2 c{p.x + size.x * 0.5f, p.y + size.y * 0.5f};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, r, IM_COL32(14, 22, 28, 255), 64);
    dl->AddCircle(c, r, toU32(col::kBrassDim), 64, 2.0f);
    for (int d = 0; d < 360; d += 10) {
        float a = float(d * kDegToRad);
        float inner = d % 90 == 0 ? r * 0.78f : d % 30 == 0 ? r * 0.86f : r * 0.92f;
        ImVec2 dir{std::sin(a), -std::cos(a)};
        dl->AddLine({c.x + dir.x * inner, c.y + dir.y * inner}, {c.x + dir.x * r, c.y + dir.y * r}, toU32(col::kTextDim), d % 90 == 0 ? 2.0f : 1.0f);
        if (d % 90 == 0) {
            const char* lbl = d == 0 ? "N" : d == 90 ? "E" : d == 180 ? "S" : "W";
            if (loc::language() == "de" && d == 90) lbl = "O";
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            dl->AddText({c.x + dir.x * r * 0.62f - ts.x * 0.5f, c.y + dir.y * r * 0.62f - ts.y * 0.5f}, toU32(col::kText), lbl);
        }
    }
    // Ordered course marker.
    float ao = float(ordered * kDegToRad);
    ImVec2 od{std::sin(ao), -std::cos(ao)};
    dl->AddTriangleFilled({c.x + od.x * r, c.y + od.y * r}, {c.x + od.x * r * 0.8f + od.y * 6, c.y + od.y * r * 0.8f - od.x * 6},
                          {c.x + od.x * r * 0.8f - od.y * 6, c.y + od.y * r * 0.8f + od.x * 6}, toU32(col::kBrass));
    // Heading needle.
    float ah = float(heading * kDegToRad);
    ImVec2 hd{std::sin(ah), -std::cos(ah)};
    dl->AddLine(c, {c.x + hd.x * r * 0.7f, c.y + hd.y * r * 0.7f}, toU32(col::kAllies), 3.0f);
    dl->AddCircleFilled(c, 3.5f, toU32(col::kText));
    std::string txt = fmtBearing(ordered);
    ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
    dl->AddText({c.x - ts.x * 0.5f, c.y + r * 0.22f}, toU32(col::kBrass), txt.c_str());
    bool changed = false;
    if (active) {
        ImVec2 m = ImGui::GetIO().MousePos;
        double brg = wrap360(std::atan2(m.x - c.x, -(m.y - c.y)) * kRadToDeg);
        if (!ImGui::GetIO().KeyShift) brg = std::round(brg / 5.0) * 5.0;  // 5-degree steps unless Shift
        if (std::abs(angleDiff(ordered, brg)) > 0.01) {
            ordered = wrap360(brg);
            changed = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("ui.helm.compass_tip"));
    ImGui::PopID();
    return changed;
}

bool engineTelegraph(const char* id, double& speedKn, double maxKn, double currentKn) {
    struct Step {
        const char* key;
        double frac;
    };
    static const Step steps[] = {{"ui.helm.stop", 0.0}, {"ui.helm.slow", 0.25}, {"ui.helm.half", 0.5}, {"ui.helm.standard", 0.67},
                                 {"ui.helm.full", 0.85}, {"ui.helm.flank", 1.0}};
    bool changed = false;
    ImGui::PushID(id);
    for (const auto& s : steps) {
        double kn = std::round(maxKn * s.frac);
        bool active = std::abs(speedKn - kn) < 0.6;
        char label[64];
        std::snprintf(label, sizeof(label), "%s  %2.0f", tr(s.key), kn);
        if (toggleButton(label, active, {ImGui::GetFontSize() * 7.5f, 0})) {
            speedKn = kn;
            changed = true;
        }
    }
    float v = float(speedKn);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.5f);
    if (ImGui::SliderFloat("##kn", &v, 0.0f, float(maxKn), "%.0f kn")) {
        speedKn = v;
        changed = true;
    }
    ImGui::TextDisabled("%s %.1f kn", tr("ui.helm.actual"), currentKn);
    ImGui::PopID();
    return changed;
}

void drawShipSymbol(ImDrawList* dl, ImVec2 c, float s, float hr, ImU32 color, bool filled, float th) {
    ImVec2 f{std::sin(hr), -std::cos(hr)}, r{-f.y, f.x};
    auto P = [&](float a, float b) { return ImVec2{c.x + f.x * a + r.x * b, c.y + f.y * a + r.y * b}; };
    ImVec2 pts[5] = {P(s * 1.3f, 0), P(s * 0.3f, s * 0.5f), P(-s, s * 0.45f), P(-s, -s * 0.45f), P(s * 0.3f, -s * 0.5f)};
    if (filled) dl->AddConvexPolyFilled(pts, 5, color);
    dl->AddPolyline(pts, 5, filled ? IM_COL32(0, 0, 0, 160) : color, ImDrawFlags_Closed, th);
}

void drawContactSymbol(ImDrawList* dl, ImVec2 c, float s, bool hostile, bool lost, ImU32 color) {
    if (hostile) {
        ImVec2 pts[4] = {{c.x, c.y - s}, {c.x + s, c.y}, {c.x, c.y + s}, {c.x - s, c.y}};
        if (lost) dl->AddPolyline(pts, 4, color, ImDrawFlags_Closed, 1.5f);
        else {
            dl->AddConvexPolyFilled(pts, 4, (color & 0x00ffffff) | 0x60000000);
            dl->AddPolyline(pts, 4, color, ImDrawFlags_Closed, 2.0f);
        }
    } else {
        dl->AddCircle(c, s * 0.9f, color, 20, lost ? 1.2f : 2.0f);
        if (!lost) dl->AddCircleFilled(c, s * 0.9f, (color & 0x00ffffff) | 0x40000000, 20);
    }
}

void drawAircraftSymbol(ImDrawList* dl, ImVec2 c, float s, float h, ImU32 color, float th) {
    // Screen axes: x right, y down; heading 0 = up.
    auto at = [&](float fwd, float right) {
        return ImVec2{c.x + std::sin(h) * fwd * s + std::cos(h) * right * s, c.y - std::cos(h) * fwd * s + std::sin(h) * right * s};
    };
    dl->AddLine(at(1.0f, 0.0f), at(-1.0f, 0.0f), color, th);           // fuselage
    dl->AddLine(at(0.25f, -1.0f), at(0.25f, 1.0f), color, th);         // wings
    dl->AddLine(at(-0.85f, -0.42f), at(-0.85f, 0.42f), color, th);     // tailplane
}

void drawAirBaseSymbol(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    dl->AddRect({c.x - s, c.y - s}, {c.x + s, c.y + s}, color, 0.0f, 0, 1.5f);
    dl->AddLine({c.x - s * 0.7f, c.y + s * 0.5f}, {c.x + s * 0.7f, c.y - s * 0.5f}, color, 2.0f);
    dl->AddLine({c.x - s * 0.5f, c.y - s * 0.6f}, {c.x + s * 0.4f, c.y + s * 0.65f}, color, 1.5f);
}

void drawFlag(ImDrawList* dl, ImVec2 p, ImVec2 sz, sim::Nation n) {
    ImVec2 q{p.x + sz.x, p.y + sz.y};
    switch (n) {
        case sim::Nation::USN: {
            for (int i = 0; i < 7; ++i) {
                float y0 = p.y + sz.y * i / 7.0f, y1 = p.y + sz.y * (i + 1) / 7.0f;
                dl->AddRectFilled({p.x, y0}, {q.x, y1}, i % 2 ? IM_COL32(240, 240, 240, 255) : IM_COL32(178, 34, 52, 255));
            }
            dl->AddRectFilled(p, {p.x + sz.x * 0.42f, p.y + sz.y * 4.0f / 7.0f}, IM_COL32(60, 59, 110, 255));
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 2; ++j)
                    dl->AddCircleFilled({p.x + sz.x * (0.07f + i * 0.14f), p.y + sz.y * (0.14f + j * 0.28f)}, 0.9f, IM_COL32(255, 255, 255, 255));
            break;
        }
        case sim::Nation::IJN: {
            dl->AddRectFilled(p, q, IM_COL32(245, 245, 245, 255));
            ImVec2 c{p.x + sz.x * 0.42f, p.y + sz.y * 0.5f};
            dl->PushClipRect(p, q, true);
            for (int i = 0; i < 16; i += 2) {
                float a0 = float(i * kTwoPi / 16.0), a1 = float((i + 1) * kTwoPi / 16.0);
                float L = sz.x * 1.2f;
                dl->AddTriangleFilled(c, {c.x + std::cos(a0) * L, c.y + std::sin(a0) * L}, {c.x + std::cos(a1) * L, c.y + std::sin(a1) * L},
                                      IM_COL32(190, 0, 30, 255));
            }
            dl->AddCircleFilled(c, sz.y * 0.28f, IM_COL32(190, 0, 30, 255), 20);
            dl->PopClipRect();
            break;
        }
        case sim::Nation::RAN:
        case sim::Nation::RN: {
            bool white = n == sim::Nation::RN;
            dl->AddRectFilled(p, q, white ? IM_COL32(245, 245, 245, 255) : IM_COL32(1, 33, 105, 255));
            if (white) {
                dl->AddRectFilled({p.x + sz.x * 0.62f, p.y}, {p.x + sz.x * 0.70f, q.y}, IM_COL32(200, 16, 46, 255));
                dl->AddRectFilled({p.x, p.y + sz.y * 0.44f}, {q.x, p.y + sz.y * 0.56f}, IM_COL32(200, 16, 46, 255));
            }
            ImVec2 cq{p.x + sz.x * 0.5f, p.y + sz.y * 0.5f};
            dl->AddRectFilled(p, cq, IM_COL32(1, 33, 105, 255));
            dl->AddLine(p, cq, IM_COL32(255, 255, 255, 255), 2.0f);
            dl->AddLine({p.x, cq.y}, {cq.x, p.y}, IM_COL32(255, 255, 255, 255), 2.0f);
            dl->AddRectFilled({p.x + sz.x * 0.21f, p.y}, {p.x + sz.x * 0.29f, cq.y}, IM_COL32(200, 16, 46, 255));
            dl->AddRectFilled({p.x, p.y + sz.y * 0.21f}, {cq.x, p.y + sz.y * 0.29f}, IM_COL32(200, 16, 46, 255));
            if (!white) dl->AddCircleFilled({p.x + sz.x * 0.75f, p.y + sz.y * 0.62f}, 1.5f, IM_COL32(255, 255, 255, 255));
            break;
        }
        case sim::Nation::RM: {
            // Italian naval ensign: tricolour with the crowned shield of Savoy.
            float w3 = sz.x / 3.0f;
            dl->AddRectFilled(p, {p.x + w3, q.y}, IM_COL32(0, 140, 69, 255));
            dl->AddRectFilled({p.x + w3, p.y}, {p.x + 2 * w3, q.y}, IM_COL32(244, 245, 240, 255));
            dl->AddRectFilled({p.x + 2 * w3, p.y}, q, IM_COL32(205, 33, 42, 255));
            ImVec2 c{p.x + sz.x * 0.5f, p.y + sz.y * 0.55f};
            float r = sz.y * 0.2f;
            dl->AddRectFilled({c.x - r * 0.8f, c.y - r}, {c.x + r * 0.8f, c.y + r}, IM_COL32(30, 60, 150, 255));
            dl->AddRectFilled({c.x - r * 0.6f, c.y - r * 0.8f}, {c.x + r * 0.6f, c.y + r * 0.8f}, IM_COL32(205, 33, 42, 255));
            dl->AddRectFilled({c.x - r * 0.12f, c.y - r * 0.8f}, {c.x + r * 0.12f, c.y + r * 0.8f}, IM_COL32(244, 245, 240, 255));
            dl->AddRectFilled({c.x - r * 0.6f, c.y - r * 0.12f}, {c.x + r * 0.6f, c.y + r * 0.12f}, IM_COL32(244, 245, 240, 255));
            dl->AddRectFilled({c.x - r * 0.6f, c.y - r * 1.55f}, {c.x + r * 0.6f, c.y - r * 1.1f}, IM_COL32(200, 160, 40, 255));
            break;
        }
        case sim::Nation::KM: {
            // German war ensign as used in this game: red field, black cross edged in white, the Iron Cross in
            // the upper hoist (the emblem in the centre is deliberately left out).
            dl->AddRectFilled(p, q, IM_COL32(200, 16, 30, 255));
            ImVec2 c{p.x + sz.x * 0.45f, p.y + sz.y * 0.5f};
            float a = sz.y * 0.09f, b = sz.y * 0.05f;
            dl->AddRectFilled({p.x, c.y - a}, {q.x, c.y + a}, IM_COL32(245, 245, 245, 255));
            dl->AddRectFilled({c.x - a, p.y}, {c.x + a, q.y}, IM_COL32(245, 245, 245, 255));
            dl->AddRectFilled({p.x, c.y - b}, {q.x, c.y + b}, IM_COL32(15, 15, 15, 255));
            dl->AddRectFilled({c.x - b, p.y}, {c.x + b, q.y}, IM_COL32(15, 15, 15, 255));
            ImVec2 ic{p.x + sz.x * 0.2f, p.y + sz.y * 0.24f};
            float k = sz.y * 0.13f;
            dl->AddRectFilled({ic.x - k, ic.y - k * 0.3f}, {ic.x + k, ic.y + k * 0.3f}, IM_COL32(15, 15, 15, 255));
            dl->AddRectFilled({ic.x - k * 0.3f, ic.y - k}, {ic.x + k * 0.3f, ic.y + k}, IM_COL32(15, 15, 15, 255));
            break;
        }
        default: {
            dl->AddRectFilled(p, q, IM_COL32(90, 96, 100, 255));
            const char* t = n == sim::Nation::MN ? "MN" : "?";
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText({p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f}, IM_COL32(240, 240, 240, 255), t);
        }
    }
    dl->AddRect(p, q, IM_COL32(0, 0, 0, 160));
}

}  // namespace it::ui
