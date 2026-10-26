#pragma once
// Unit reference ("Jane's" style): every ship class with a rotatable 3D model, recognition views, fire
// arcs, gun penetration tables against any other class, torpedoes, protection, sensors and history.
#include <string>

namespace it::sim {
struct ShipClassDef;
}

namespace it::ui {

class GameUi;

class UnitReference {
public:
    explicit UnitReference(GameUi& ui) : ui_(ui) {}
    void draw(bool overlay);
    void select(const std::string& classId) { selected_ = classId; }

private:
    void drawList(float height);
    void drawPreview(const sim::ShipClassDef& c, float width, float height);
    void drawDetails(const sim::ShipClassDef& c);
    void drawFireArcs(const sim::ShipClassDef& c, float size);
    void drawPenetration(const sim::ShipClassDef& c);

    GameUi& ui_;
    std::string selected_, versus_;
    float yaw_ = 135.0f, pitch_ = 10.0f;
    bool night_ = false;
    char filter_[64] = {};
    int nationFilter_ = 0;  // 0 all, 1 allies, 2 axis
};

}  // namespace it::ui
