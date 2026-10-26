#pragma once
// Reusable interface widgets: compass/course input, engine telegraph, status bars, ship symbols, flags.
#include <string>

#include "imgui.h"
#include "sim/Types.h"

namespace it::ui {

/// Compass rose: shows the current heading (needle) and ordered course (brass marker); click/drag to
/// set a new course. Returns true while the course is being changed.
bool compassInput(const char* id, double& orderedCourse, double currentHeading, float radius);
/// Engine order telegraph: Stop .. Flank. Returns true when a new speed was ordered.
bool engineTelegraph(const char* id, double& speedKn, double maxKn, double currentKn);
/// Labelled horizontal bar (0..1).
void statusBar(const char* label, float fraction, const ImVec4& color, const char* overlay = nullptr, float width = -1.0f);
void sectionHeader(const char* text);
void helpMarker(const char* text);
void textColored(const ImVec4& c, const char* fmt, ...);
/// NTDS-like symbols adapted to the period plot: own ships (filled hull), contacts by identification level.
void drawShipSymbol(ImDrawList* dl, ImVec2 c, float size, float headingRad, ImU32 color, bool filled, float thickness = 1.5f);
void drawContactSymbol(ImDrawList* dl, ImVec2 c, float size, bool hostile, bool lost, ImU32 color);
void drawFlag(ImDrawList* dl, ImVec2 pos, ImVec2 size, sim::Nation n);
/// Button that looks like a tab-strip toggle.
bool toggleButton(const char* label, bool active, const ImVec2& size = {0, 0});
ImU32 toU32(const ImVec4& c, float alphaMul = 1.0f);

}  // namespace it::ui
