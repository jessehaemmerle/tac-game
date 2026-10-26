#pragma once
// glTF 2.0 import for replacement ship models (CC0 / CC-BY assets). Nodes named "mount_<id>" and
// "guns_<id>" become animated turret / barrel parts; everything else is static. Coordinates are
// converted from glTF (Y up, +Z forward, +X left) into the ship-local frame (x fwd, y stbd, z up).
#include <string>

#include "procgen/Mesh.h"
#include "sim/Database.h"

namespace it::procgen {

bool importGltf(const std::string& path, const sim::ShipClassDef& cls, double scale, Model& out, std::string* error = nullptr);

}  // namespace it::procgen
