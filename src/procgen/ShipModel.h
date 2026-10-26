#pragma once
// Procedural ship models generated from ShipClassDef data (dimensions, hull form, paint scheme,
// superstructure parts, mounts). Every class gets a recognisable silhouette for the 3D view, the
// unit reference and visual identification training.
#include "procgen/Mesh.h"
#include "sim/Database.h"

namespace it::procgen {

struct PaintScheme {
    Material hull, bottom, boot, deck, superstructure, metal, glass, canvas, lens, aircraft, linoleumStrip;
};
PaintScheme paintScheme(const std::string& hullPaint, const std::string& deckPaint);

struct ShipModelOptions {
    int stations = 56;        // hull loft resolution
    bool details = true;      // boats, rails, radars, aircraft
};

Model buildShipModel(const sim::ShipClassDef& cls, const ShipModelOptions& opt = {});

/// Hull half-width (m) at fraction u (0 bow .. 1 stern) and height z (m, waterline = 0).
double hullHalfWidth(const sim::ShipClassDef& cls, double u, double z);
double keelDepth(const sim::ShipClassDef& cls, double u);

}  // namespace it::procgen
