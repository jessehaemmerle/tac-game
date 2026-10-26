#include "ai/SideView.h"

#include <algorithm>

namespace it::ai {

std::vector<sim::Ship*> SideView::ownShips() const {
    std::vector<sim::Ship*> out;
    for (auto& s : w_.ships())
        if (s->side == side_ && s->alive()) out.push_back(s.get());
    return out;
}

sim::Ship* SideView::own(sim::EntityId id) const {
    sim::Ship* s = w_.ship(id);
    return (s && s->side == side_) ? s : nullptr;
}

std::vector<sim::Group*> SideView::ownGroups() const {
    std::vector<sim::Group*> out;
    for (auto& g : w_.groups())
        if (g.side == side_) out.push_back(&g);
    return out;
}

bool SideView::known(const sim::Track& t, const std::vector<sim::Ship*>* group) const {
    double now = time();
    if (group)
        for (auto* s : *group)
            if (t.heldBy(s->id, now, 60.0)) return true;
    return now >= t.sharedAt;
}

int SideView::fireTorpedoes(sim::Ship& s, uint32_t track, int count, double spreadDeg, int speedSetting, std::string* why) {
    if (s.side != side_) return 0;
    return w_.fireTorpedoes(s, track, count, spreadDeg, speedSetting, why);
}

void SideView::setDirectorTarget(sim::Ship& s, int director, uint32_t track) {
    if (s.side == side_) w_.setDirectorTarget(s, director, track);
}

void SideView::setSearchlight(sim::Ship& s, bool on, uint32_t track) {
    if (s.side == side_ && (s.orders.searchlight != on || s.orders.searchlightTrack != track)) w_.setSearchlight(s, on, track);
}

bool SideView::launchAircraft(sim::Ship& s, bool flares, const Vec2& target) {
    if (s.side != side_) return false;
    return w_.launchAircraft(s, flares ? sim::AircraftMission::Kind::Flares : sim::AircraftMission::Kind::Recon, target);
}

void SideView::observe(const sim::Event& e) {
    using sim::EventType;
    switch (e.type) {
        case EventType::TorpedoSighted:
            if (e.side == side_) {
                TorpedoWarning tw;
                tw.pos = e.pos.xy();
                tw.heading = e.value;
                tw.speed = e.value2;
                tw.time = e.time;
                tw.observer = e.a;
                tw.key = e.b;
                torpedoWarnings_.push_back(tw);
            }
            break;
        case EventType::ShellHit:
            if (sim::Ship* s = own(e.b)) hits_.push_back({s->id, e.time, false, e.pos.xy()});
            break;
        case EventType::TorpedoHit:
        case EventType::TorpedoDud:
            if (sim::Ship* s = own(e.b)) hits_.push_back({s->id, e.time, true, e.pos.xy()});
            break;
        default: break;
    }
}

void SideView::prune() {
    double now = time();
    torpedoWarnings_.erase(std::remove_if(torpedoWarnings_.begin(), torpedoWarnings_.end(),
                                          [&](const TorpedoWarning& t) { return now - t.time > 240.0; }),
                           torpedoWarnings_.end());
    hits_.erase(std::remove_if(hits_.begin(), hits_.end(), [&](const HitReport& h) { return now - h.time > 300.0; }), hits_.end());
}

}  // namespace it::ai
