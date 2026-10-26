#pragma once
// Simulation → presentation event stream (renderer effects, audio, message log, AI triggers, debrief).
#include <string>
#include <vector>

#include "core/Math.h"
#include "sim/Types.h"

namespace it::sim {

enum class EventType : uint8_t {
    GunFired,         // a = ship, value = calibre mm, value2 = mount index, pos = muzzle
    ShellSplash,      // a = firing ship, pos = impact, value = calibre mm
    ShellHit,         // a = firing ship, b = target, pos, value = calibre, value2 = 1 if penetrated
    Straddle,         // a = firing ship, b = target
    StarshellBurst,   // a = firing ship, pos
    FlareDropped,     // a = ship (aircraft parent), pos
    TorpedoLaunched,  // a = ship, value = count
    TorpedoSighted,   // a = observer ship, b = torpedo id, pos
    TorpedoHit,       // a = firing ship, b = target, pos
    TorpedoDud,       // a = firing ship, b = target, pos
    TorpedoEnded,     // b = torpedo id
    SearchlightOn,    // a = ship, b = target ship (if any)
    SearchlightOff,   // a = ship
    FireStarted,      // a = ship, pos
    FireOut,          // a = ship
    Explosion,        // a = ship, pos, value = size (kg TNT)
    MagazineExplosion,// a = ship
    ShipCrippled,     // a = ship (dead in the water / out of action)
    ShipSunk,         // a = ship, b = credited enemy ship
    ShipGrounded,     // a = ship
    Collision,        // a, b ships
    SystemDamaged,    // a = ship, text = system key
    ContactNew,       // side, track, pos
    ContactLost,      // side, track
    ContactIdentified,// side, track, b = (hidden) true entity, text = class id
    UnderFire,        // a = ship being shot at (splashes nearby)
    Report,           // generic bridge report: text = loc key, args
    ScenarioMessage,  // text = loc key / literal
};

struct Event {
    EventType type = EventType::Report;
    double time = 0;
    EntityId a = kNoEntity, b = kNoEntity;
    Side side = Side::Neutral;
    Vec3 pos;
    double value = 0, value2 = 0;
    uint32_t track = 0;
    std::string text;
    std::vector<std::string> args;
};

}  // namespace it::sim
