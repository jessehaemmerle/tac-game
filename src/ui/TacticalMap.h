#pragma once
// Tactical plot: chart, coastlines, own ships, the side's contact picture (never ground truth for a
// playing side), sighted torpedo tracks, flares, squalls, range rings and a measuring tool. Also
// used by the scenario editor (draggable group markers) and the debrief (battle plot).
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "sim/World.h"

namespace it::ui {

struct Pick;  // hit-test candidate (TacticalMap.cpp)

struct MapMarker {
    int id = -1;
    Vec2 pos;
    double heading = 0;
    ImVec4 color{1, 1, 1, 1};
    std::string label;
    bool selected = false;
    bool draggable = true;
    std::vector<Vec2> route;  // optional waypoints (drawn dashed)
};

struct MapTrack {  // debrief battle plot
    std::vector<Vec2> points;
    ImVec4 color{1, 1, 1, 1};
    std::string label;
    bool sunk = false;
};

struct MapInput {
    const sim::World* world = nullptr;
    const sim::Terrain* terrain = nullptr;
    sim::Side side = sim::Side::Neutral;  // perspective; Neutral = umpire view (editor/debrief)
    sim::EntityId selectedShip = sim::kNoEntity;
    uint32_t selectedTrack = 0;
    int selectedGroup = -1;
    uint64_t chartTexture = 0;
    bool metric = false;
    bool compact = false;  // mini-map: no labels, no grid labels
    const std::vector<MapMarker>* markers = nullptr;
    const std::vector<MapTrack>* plot = nullptr;
    double torpedoRange = 0;  // selected ship's torpedo range ring (m), 0 = none
    double gunRange = 0;      // selected ship's main battery range ring (m)
    std::vector<Vec2> plannedCourse;  // line from the selected ship along the ordered course
    bool plannedIsRoute = false;      // plannedCourse is a waypoint route (vertices drawn)
    int selectedFlight = -1;
    uint32_t selectedAirTrack = 0;
    bool targeting = false;           // a map order is pending: crosshair cursor
};

struct MapOutput {
    bool hovered = false;
    Vec2 mouseWorld;
    sim::EntityId clickedShip = sim::kNoEntity;
    uint32_t clickedTrack = 0;
    int clickedMarker = -1;
    bool clickedEmpty = false;
    int clickedFlight = -1;
    uint32_t clickedAirTrack = 0;
    bool rightClicked = false;
    bool rightShift = false;          // shift held on the right click (append a waypoint)
    Vec2 rightClickPos;
    // What lay under the cursor on the right click.
    sim::EntityId rightShip = sim::kNoEntity;
    uint32_t rightTrack = 0, rightAirTrack = 0;
    int rightFlight = -1;
    int draggedMarker = -1;
    Vec2 dragPos;
    bool doubleClicked = false;
};

class TacticalMap {
public:
    Vec2 center;
    double mpp = 40.0;  // metres per pixel
    MapOutput draw(const char* id, ImVec2 size, const MapInput& in);
    void focus(const Vec2& p, double metresPerPixel = -1.0) {
        center = p;
        if (metresPerPixel > 0) mpp = metresPerPixel;
    }
    void fit(const Vec2& lo, const Vec2& hi, ImVec2 size);
    void resetHistory() {
        history_.clear();
        trackHistory_.clear();
        lastSample_ = -1e9;
    }

private:
    ImVec2 toScreen(const Vec2& w) const { return {float(origin_.x + (w.x - center.x) / mpp), float(origin_.y - (w.y - center.y) / mpp)}; }
    Vec2 toWorld(ImVec2 s) const { return {center.x + (s.x - origin_.x) * mpp, center.y - (s.y - origin_.y) * mpp}; }
    void sampleHistory(const MapInput& in);
    void drawGrid(ImDrawList* dl, const MapInput& in);
    void drawScale(ImDrawList* dl, const MapInput& in);
    /// Own flights (all in the umpire view), the side's air picture and air bases.
    void drawAir(ImDrawList* dl, const MapInput& in, std::vector<Pick>& picks);

    ImVec2 origin_, canvasMin_, canvasMax_;
    std::unordered_map<sim::EntityId, std::deque<Vec2>> history_;
    std::unordered_map<uint32_t, std::deque<Vec2>> trackHistory_;
    double lastSample_ = -1e9;
    bool panning_ = false, measuring_ = false;
    Vec2 measureStart_;
    int dragMarker_ = -1;
    ImVec2 pressPos_;
};

}  // namespace it::ui
