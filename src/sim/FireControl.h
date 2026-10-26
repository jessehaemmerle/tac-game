#pragma once
// Gunnery fire control: each director tracks its target with its own rangefinder / FC radar
// (alpha-beta filter), computes a firing solution, drives the mounts and fires salvoes. Fall of
// shot is spotted (visually or by radar) and corrections are applied — "walking" the salvoes in.
#include "core/Math.h"
#include "sim/Types.h"

namespace it::sim {
class World;
class Ship;
struct DirectorState;
struct MountState;
struct Salvo;

namespace firecontrol {

void update(World& w, Ship& s, double dt);
/// Spotting once every shell of a salvo has landed.
void salvoLanded(World& w, Salvo& salvo);
/// Predicted target position (director's estimate) at time t.
Vec2 predictedTarget(const DirectorState& d, double t);
/// A director of the ship that has held the given track long enough to know its course and speed
/// (torpedo attacks use it in preference to the plot), or null.
const DirectorState* solutionFor(const Ship& s, uint32_t track, double minTrackTime = 30.0);
/// Choose the best shell for a target of the given (believed) size.
ShellKind chooseShell(const MountState& m, SizeClass targetSize);

}  // namespace firecontrol
}  // namespace it::sim
