#include "sim/Ship.h"

namespace it::sim {

int MountState::totalAmmo() const {
    int n = 0;
    for (auto& [k, v] : ammo) n += v;
    return n;
}

double DamageState::totalFireIntensity() const {
    double s = 0;
    for (auto& f : fires) s += f.intensity;
    return s;
}

void Ship::initFromClass(const ShipClassDef& c) {
    cls = &c;
    nation = c.nation;
    mounts.clear();
    for (auto& m : c.mounts) {
        MountState ms;
        ms.def = &m;
        ms.train = (m.arcMin > 0) ? 180.0 : 0.0;  // aft mounts stowed facing aft
        if (m.arcMin > 0 && m.arcMax < 180) ms.train = 90;
        if (m.arcMax < 0) ms.train = -90;
        for (auto& [k, v] : m.ammo) ms.ammo[k] = v * m.barrels;
        if (m.gun && !m.gun->shells.empty()) ms.loaded = m.gun->shells.front().kind;
        mounts.push_back(ms);
    }
    torpedoMounts.clear();
    for (auto& t : c.torpedoMounts) {
        TorpedoMountState ts;
        ts.def = &t;
        ts.loaded = t.tubes;
        ts.reloads = t.reloads;
        ts.train = t.mirrored ? 0.0 : 0.5 * (t.arcMin + t.arcMax);
        torpedoMounts.push_back(ts);
    }
    directors.clear();
    for (auto& d : c.directors) {
        DirectorState ds;
        ds.def = &d;
        directors.push_back(ds);
    }
    sensors.clear();
    for (auto& s : c.sensors) {
        SensorState ss;
        ss.def = &s;
        sensors.push_back(ss);
    }
    floatplanes = c.floatplanes;
    dmg = {};
}

double Ship::maxSpeedNow() const {
    if (!cls || dmg.sinking || dmg.sunk) return 0.0;
    double floodFrac = clamp(dmg.flooding / (displacement() * cls->reserveBuoyancy), 0.0, 1.0);
    double v = cls->maxSpeed() * clamp(dmg.propulsion, 0.0, 1.0) * (1.0 - 0.6 * floodFrac) * (dmg.hullIntegrity < 0.5 ? 0.85 : 1.0);
    if (dmg.propulsion < 0.08) v = 0;
    return std::max(0.0, v);
}

double Ship::effectiveness() const {
    if (!alive() || dmg.sinking) return 0.0;
    double fc = 0;
    for (auto& d : directors) fc = std::max(fc, d.health);
    double weapons = 0, total = 0;
    for (auto& m : mounts) {
        if (!m.def || !m.def->gun) continue;
        double w = m.def->barrels * m.def->gun->caliberMm;
        total += w;
        if (m.operational() && m.totalAmmo() > 0) weapons += w;
    }
    double weap = total > 0 ? weapons / total : 0.0;
    double torp = torpedoesTotal() > 0 ? double(torpedoesReady()) / torpedoesTotal() : 0.0;
    double arms = std::max(weap, 0.6 * torp);
    return clamp((1.0 - dmg.crewLoss) * (0.5 + 0.5 * dmg.bridge) * (0.4 + 0.6 * fc) * (0.2 + 0.8 * arms), 0.0, 1.0);
}

double Ship::flotation() const {
    if (!cls) return 1.0;
    if (dmg.sinking || dmg.sunk) return 0.0;
    double f = 1.0 - dmg.flooding / (displacement() * cls->reserveBuoyancy);
    return clamp(std::min(f, dmg.hullIntegrity), 0.0, 1.0);
}

int Ship::torpedoesReady() const {
    int n = 0;
    for (auto& t : torpedoMounts)
        if (t.operational()) n += t.loaded;
    return n;
}

int Ship::torpedoesTotal() const {
    int n = 0;
    for (auto& t : torpedoMounts) n += t.def ? t.def->tubes : 0;
    return n;
}

}  // namespace it::sim
