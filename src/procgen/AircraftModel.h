#pragma once
// Procedural aircraft models from the aircraft database: fuselage, wings (low, mid, parasol, inverted
// gull, biplane), engine nacelles with spinning propellers, tail, canopies and gun positions, floats,
// fixed undercarriage, the weapon load and national markings, in two-tone camouflage.
#include "procgen/Mesh.h"
#include "sim/Database.h"

namespace it::procgen {

/// Model space: x forward, y starboard, z up (metres), origin at the aircraft's centre of gravity.
Model buildAircraftModel(const sim::AircraftDef& def);

}  // namespace it::procgen
