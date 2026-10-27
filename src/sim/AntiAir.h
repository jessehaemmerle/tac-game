#pragma once
// Ship anti-aircraft fire: light AA mounts and dual-purpose guns engage the aircraft their crews can see.
// Effectiveness follows wartime experience as "rounds per aircraft destroyed", modified by range, height,
// crossing speed, light, crew, fire control and the ship's own manoeuvring.
namespace it::sim {
class World;
class Ship;
namespace aa {
/// Engage aircraft; call at a fixed interval dt (the battery tick).
void step(World& w, Ship& s, double dt);
}  // namespace aa
}  // namespace it::sim
