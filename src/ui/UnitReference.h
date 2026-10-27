#pragma once
// Unit reference ("Jane's" style): every ship class with a rotatable 3D model, recognition views, fire
// arcs, gun penetration tables against any other class, torpedoes, protection, sensors and history -
// and every aircraft type with its model in flight, performance, loads and history.
#include <string>

#include "ui/UiServices.h"

namespace it::sim {
struct ShipClassDef;
struct AircraftDef;
}

namespace it::ui {

class GameUi;

class UnitReference {
public:
    explicit UnitReference(GameUi& ui) : ui_(ui) {}
    void draw(bool overlay);
    void select(const std::string& classId) { selected_ = classId; }
    /// Model view state (automation: close-ups for visual checks).
    PreviewView& view() { return view_; }
    void setLargeView(bool on) { large_ = on; }

private:
    void drawList(float height);
    void drawAircraftList();
    void drawAircraft(const sim::AircraftDef& a, float width, float height);
    void drawPreview(const sim::ShipClassDef& c, float width, float height);
    void drawPreview(const std::string& id, float width, float height);
    void drawDetails(const sim::ShipClassDef& c);
    void drawFireArcs(const sim::ShipClassDef& c, float size);
    void drawPenetration(const sim::ShipClassDef& c);

    GameUi& ui_;
    std::string selected_, versus_;
    PreviewView view_;
    bool large_ = false;  // model view fills the page
    char filter_[64] = {};
    int nationFilter_ = 0;  // 0 all, 1 allies, 2 axis
    int category_ = 0;      // 0 ships, 1 aircraft
};

}  // namespace it::ui
