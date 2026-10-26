// Ship articulation: poses from the simulation (train, elevation, recoil per barrel, torpedo launches,
// light AA following aircraft, catapult launches, searchlights, propellers and rudders), the weapons
// drill of the unit reference, part transforms and muzzle positions, and floatplanes in flight.
#include <cmath>
#include <limits>

#include "procgen/ShipAnimation.h"
#include "render/RendererImpl.h"

namespace it::render {

namespace {

Mat4f rotZ(float deg) { return Mat4f::rotation(Quatf::axisAngle({0, 0, 1}, float(deg * kDegToRad))); }
Mat4f rotY(float deg) { return Mat4f::rotation(Quatf::axisAngle({0, 1, 0}, float(deg * kDegToRad))); }
Mat4f rotX(float deg) { return Mat4f::rotation(Quatf::axisAngle({1, 0, 0}, float(deg * kDegToRad))); }
Mat4f aboutPivot(const Vec3f& pivot, const Mat4f& r) { return Mat4f::translation(pivot) * r * Mat4f::translation(-pivot); }

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

float stowedTrain(const sim::MountDef& m) {
    if (m.arcMin > 0 && m.arcMax < 180) return 90.0f;
    if (m.arcMin > 0) return 180.0f;
    if (m.arcMax < 0) return -90.0f;
    return 0.0f;
}

/// Floatplane parts: their catapult ordinal (or -1).
std::vector<int> aircraftCatapults(const GpuMesh& mesh) {
    std::vector<int> out;
    for (const auto& p : mesh.parts)
        if (p.kind == procgen::PartKind::Aircraft) {
            int cat = -1;
            if (p.parent >= 0 && mesh.parts[size_t(p.parent)].kind == procgen::PartKind::Catapult) cat = mesh.parts[size_t(p.parent)].sub;
            if (p.sub >= 0) {
                if (out.size() <= size_t(p.sub)) out.resize(size_t(p.sub) + 1, -1);
                out[size_t(p.sub)] = cat;
            }
        }
    return out;
}

int countKind(const GpuMesh& mesh, procgen::PartKind k) {
    int n = 0;
    for (const auto& p : mesh.parts) n += p.kind == k;
    return n;
}

}  // namespace

ShipPose stowedPose(const sim::ShipClassDef& c) {
    ShipPose p;
    for (const auto& m : c.mounts) {
        p.train.push_back(stowedTrain(m));
        p.elev.push_back(m.gun && m.gun->caliberMm < 140 ? 5.0f : 2.0f);
    }
    for (const auto& t : c.torpedoMounts) p.torpTrain.push_back(t.mirrored ? 0.0f : float(0.5 * (t.arcMin + t.arcMax)));
    return p;
}

ShipPose shipPose(Renderer::Impl& P, const sim::Ship& s, const sim::World& w, const GpuMesh* mesh, double visualTime) {
    ShipPose p;
    const sim::ShipClassDef& c = *s.cls;
    double now = w.time();
    double lead = clamp(visualTime - now, 0.0, 0.25);  // render time between simulation steps
    double t = now + lead;
    auto it = P.anims.find(s.id);
    const ShipAnim* anim = it != P.anims.end() ? &it->second : nullptr;
    // Guns: train and elevation from the simulation; wrecked mounts droop; recoil after each salvo.
    for (size_t i = 0; i < s.mounts.size(); ++i) {
        const auto& m = s.mounts[i];
        p.train.push_back(float(m.train));
        float elev = float(m.elevation);
        if (!m.operational()) elev = -3.0f - float((s.id + i) % 4);
        p.elev.push_back(elev);
        std::vector<float> rec;
        if (anim && i < anim->fired.size() && m.def) {
            double cal = m.def->gun ? m.def->gun->caliberMm : 127.0;
            for (int b = 0; b < m.def->barrels; ++b) rec.push_back(procgen::anim::recoil(t - anim->fired[i] - procgen::anim::barrelDelay(b), cal));
        }
        p.recoil.push_back(std::move(rec));
    }
    // Torpedo mounts: loaded tubes hold their torpedoes, fired ones leave the tube and drop away.
    for (size_t i = 0; i < s.torpedoMounts.size(); ++i) {
        const auto& tm = s.torpedoMounts[i];
        p.torpTrain.push_back(float(tm.train));
        int tubes = tm.def ? tm.def->tubes : 0;
        std::vector<float> out(size_t(std::max(0, tubes)), -1.0f);
        for (int k = 0; k < tubes; ++k) {
            if (k < tm.loaded) out[size_t(k)] = 0.0f;
            else if (anim && i < anim->torpLaunch.size() && k >= anim->torpFrom[i] && k < anim->torpTo[i]) {
                float travel = procgen::anim::torpedoTravel(t - anim->torpLaunch[i] - procgen::anim::tubeDelay(k - anim->torpFrom[i]));
                out[size_t(k)] = procgen::anim::torpedoGone(travel) ? -1.0f : travel;
            }
        }
        p.torpedoOut.push_back(std::move(out));
    }
    // Searchlight: on its target, otherwise sweeping.
    p.searchlightOn = s.operational() && s.orders.searchlight && s.dmg.searchlights >= 0.3;
    if (p.searchlightOn) {
        if (const sim::Ship* tgt = w.ship(s.searchlightTarget)) {
            p.searchlightTrain = float(s.relativeBearing(tgt->pos));
            double d = std::max(50.0, distance(s.pos, tgt->pos));
            p.searchlightElev = float(std::atan2(-(c.superstructureHeight - 4.0), d) * kRadToDeg);
        } else {
            p.searchlightTrain = float(std::sin(visualTime * 0.25 + s.id) * 35.0);
            p.searchlightElev = float(1.0 + 1.5 * std::sin(visualTime * 0.4 + s.id));
        }
    }
    // Directors follow their own targets.
    for (const auto& d : s.directors) p.directorTrain.push_back(d.tracking ? float(s.relativeBearing(d.estPos)) : kNaN);
    // Search radars turn while they work (the SG faster than the SC).
    bool radarOn = false;
    for (const auto& sn : s.sensors)
        if (sn.def && sn.def->def && sn.def->def->kind == sim::SensorKind::Radar && sn.working(now)) radarOn = true;
    p.radarAngle = radarOn ? float(std::fmod(visualTime * 36.0 + s.id * 37.0, 360.0)) : 0.0f;
    // Propellers and rudder.
    double frac = s.cls->maxSpeed() > 0 ? s.speed / s.cls->maxSpeed() : 0.0;
    double rps = frac * (c.size() == sim::SizeClass::Small ? 6.0 : 4.5);
    p.propAngle = float(std::fmod(visualTime * rps * 360.0, 360.0));
    p.rudder = float(-s.rudder * 35.0);
    if (!mesh) return p;
    // Light AA: follow the nearest enemy aircraft overhead; at action stations the crews sweep their sectors.
    const sim::AircraftMission* threat = nullptr;
    double best = 9000.0;
    for (const auto& a : w.aircraft()) {
        if (a.side == s.side || a.state == sim::AircraftMission::State::Launching || a.state == sim::AircraftMission::State::Done) continue;
        double d = distance(a.pos, s.pos);
        if (d < best) {
            best = d;
            threat = &a;
        }
    }
    bool action = false;
    if (anim)
        for (double f : anim->fired) action |= now - f < 180.0;
    action |= p.searchlightOn;
    for (size_t i = 0; i < mesh->aa.size(); ++i) {
        const auto& a = mesh->aa[i];
        float tr = a.baseTrain, el = 0.0f;
        if (threat) {
            double rel = s.relativeBearing(threat->pos);
            if (std::fabs(wrap180(rel - a.baseTrain)) <= a.arc) {
                tr = float(rel + 3.0 * std::sin(visualTime * 1.3 + double(i)));
                el = float(std::atan2(threat->altitude, std::max(100.0, best)) * kRadToDeg + 2.0 * std::sin(visualTime * 0.9 + double(i)));
            }
        } else if (action && s.operational()) {
            tr = float(a.baseTrain + 30.0 * std::sin(visualTime * 0.18 + double(i) * 1.3));
            el = float(14.0 + 10.0 * std::sin(visualTime * 0.13 + double(i)));
        }
        p.aaTrain.push_back(tr);
        p.aaElev.push_back(el);
    }
    // Floatplanes on their catapults; the one being launched trains out and is shot off.
    std::vector<int> cats = aircraftCatapults(*mesh);
    int onCatapults = int(cats.size());
    p.aircraftTravel.assign(cats.size(), -1.0f);
    p.catapultTrain.assign(size_t(countKind(*mesh, procgen::PartKind::Catapult)), 0.0f);
    int remaining = std::min(onCatapults, s.floatplanes);
    for (int k = remaining; k < onCatapults; ++k) p.aircraftTravel[size_t(k)] = ShipPose::kGone;
    for (const auto& a : w.aircraft()) {
        if (a.parent != s.id || a.state != sim::AircraftMission::State::Launching) continue;
        if (remaining >= onCatapults) break;  // a spare aircraft from the hangar
        procgen::anim::Launch l = procgen::anim::catapultLaunch(a.stateTime + lead);
        p.aircraftTravel[size_t(remaining)] = l.gone ? ShipPose::kGone : l.travel;
        int cat = cats[size_t(remaining)];
        if (cat >= 0 && size_t(cat) < p.catapultTrain.size()) p.catapultTrain[size_t(cat)] = l.catapultTrain;
    }
    return p;
}

ShipPose drillPose(const sim::ShipClassDef& c, const GpuMesh& mesh, double t) {
    namespace an = procgen::anim;
    an::Drill d = an::drill(t);
    ShipPose p = stowedPose(c);
    for (size_t i = 0; i < c.mounts.size(); ++i) {
        const auto& m = c.mounts[i];
        float target = float(an::drillBearing(m.arcMin, m.arcMax, m.mirrored));
        float from = p.train[i];
        p.train[i] = from + float(wrap180(double(target - from))) * d.trainBlend;
        double maxEl = m.gun ? m.gun->maxElevationDeg : 40.0;
        p.elev[i] = std::max(p.elev[i] * (1.0f - d.trainBlend), std::min(float(maxEl), d.elevation));
        std::vector<float> rec;
        double cal = m.gun ? m.gun->caliberMm : 127.0;
        for (int b = 0; b < m.barrels; ++b) rec.push_back(an::recoil(d.sinceSalvo - 0.12 * double(i) - an::barrelDelay(b), cal));
        p.recoil.push_back(std::move(rec));
    }
    for (size_t i = 0; i < c.torpedoMounts.size(); ++i) {
        const auto& tm = c.torpedoMounts[i];
        float target = float(an::drillBearing(tm.arcMin, tm.arcMax, tm.mirrored));
        float from = p.torpTrain[i];
        p.torpTrain[i] = from + float(wrap180(double(target - from))) * d.torpTrainBlend;
        std::vector<float> out(size_t(tm.tubes), 0.0f);
        if (d.sinceTorpedoes >= 0.0)
            for (int k = 0; k < tm.tubes; ++k) {
                float travel = an::torpedoTravel(d.sinceTorpedoes - 0.6 * double(i) - an::tubeDelay(k));
                out[size_t(k)] = an::torpedoGone(travel) ? -1.0f : travel;
            }
        p.torpedoOut.push_back(std::move(out));
    }
    for (const auto& a : mesh.aa) {
        p.aaTrain.push_back(a.baseTrain + d.aaSweep);
        p.aaElev.push_back(d.aaElevation);
    }
    std::vector<int> cats = aircraftCatapults(mesh);
    p.aircraftTravel.assign(cats.size(), -1.0f);
    p.catapultTrain.assign(size_t(countKind(mesh, procgen::PartKind::Catapult)), 0.0f);
    if (!cats.empty()) {
        p.aircraftTravel[0] = d.launch.gone ? ShipPose::kGone : d.launch.travel;
        if (cats[0] >= 0) p.catapultTrain[size_t(cats[0])] = d.launch.catapultTrain;
    }
    p.directorTrain.assign(c.directors.size(), 0.0f);
    if (!c.mounts.empty()) {
        float target = float(an::drillBearing(c.mounts[0].arcMin, c.mounts[0].arcMax, c.mounts[0].mirrored));
        for (auto& dt : p.directorTrain) dt = target * d.trainBlend;
    }
    p.searchlightTrain = d.searchlightTrain;
    p.searchlightElev = d.searchlightElev;
    p.radarAngle = float(std::fmod(t * 36.0, 360.0));
    p.propAngle = float(std::fmod(double(d.propellerTurns) * 360.0, 360.0));
    p.rudder = d.rudder;
    return p;
}

void partTransforms(const GpuMesh& mesh, const ShipPose& pose, std::vector<Mat4f>& local, std::vector<uint8_t>& hidden) {
    local.assign(mesh.parts.size(), Mat4f{});
    hidden.assign(mesh.parts.size(), 0);
    auto at = [](const std::vector<float>& v, int i, float fallback) {
        return (i >= 0 && size_t(i) < v.size() && !std::isnan(v[size_t(i)])) ? v[size_t(i)] : fallback;
    };
    auto nested = [](const std::vector<std::vector<float>>& v, int i, int j, float fallback) {
        if (i < 0 || size_t(i) >= v.size() || j < 0 || size_t(j) >= v[size_t(i)].size()) return fallback;
        return v[size_t(i)][size_t(j)];
    };
    for (size_t i = 0; i < mesh.parts.size(); ++i) {
        const auto& part = mesh.parts[i];
        Mat4f m;
        bool hide = false;
        switch (part.kind) {
            case procgen::PartKind::Turret: m = aboutPivot(part.pivot, rotZ(at(pose.train, part.mount, 0.0f))); break;
            case procgen::PartKind::Barrel: m = aboutPivot(part.pivot, rotY(-at(pose.elev, part.mount, 0.0f))); break;
            case procgen::PartKind::Gun: m = Mat4f::translation(part.axis * -nested(pose.recoil, part.mount, part.sub, 0.0f)); break;
            case procgen::PartKind::TorpedoMount: m = aboutPivot(part.pivot, rotZ(at(pose.torpTrain, part.mount, 0.0f))); break;
            case procgen::PartKind::Torpedo: {
                float out = nested(pose.torpedoOut, part.mount, part.sub, 0.0f);
                hide = out < 0.0f;
                float travel = std::max(0.0f, out);
                m = Mat4f::translation(part.axis * travel + Vec3f{0, 0, procgen::anim::torpedoDrop(travel, part.reach)});
                break;
            }
            case procgen::PartKind::Searchlight: m = aboutPivot(part.pivot, rotZ(pose.searchlightTrain)); break;
            case procgen::PartKind::SearchlightLamp: m = aboutPivot(part.pivot, rotY(-pose.searchlightElev)); break;
            case procgen::PartKind::Director: {
                float stowed = part.pivot.x < 0.0f ? 180.0f : 0.0f;  // after directors face aft
                m = aboutPivot(part.pivot, rotZ(at(pose.directorTrain, part.sub, stowed)));
                break;
            }
            case procgen::PartKind::Radar: m = aboutPivot(part.pivot, rotZ(part.sub == 1 ? pose.radarAngle * 2.0f : pose.radarAngle)); break;
            case procgen::PartKind::AAMount: {
                float base = part.mount >= 0 && size_t(part.mount) < mesh.aa.size() ? mesh.aa[size_t(part.mount)].baseTrain : 0.0f;
                m = aboutPivot(part.pivot, rotZ(at(pose.aaTrain, part.mount, base)));
                break;
            }
            case procgen::PartKind::AAGuns: m = aboutPivot(part.pivot, rotY(-at(pose.aaElev, part.mount, 0.0f))); break;
            case procgen::PartKind::Catapult: {
                float out = part.mount == 1 ? 0.0f : at(pose.catapultTrain, part.sub, 0.0f);  // fixed catapults do not train
                m = aboutPivot(part.pivot, rotZ(part.pivot.y >= 0.0f ? out : -out));
                break;
            }
            case procgen::PartKind::Aircraft: {
                float travel = at(pose.aircraftTravel, part.sub, -1.0f);
                hide = travel >= ShipPose::kGone;
                if (travel > 0.0f && !hide) {
                    // Along the catapult, then climbing away from it.
                    float climb = std::max(0.0f, travel - 18.0f) * 0.12f;
                    m = Mat4f::translation({travel, 0, climb});
                }
                break;
            }
            case procgen::PartKind::Propeller: m = aboutPivot(part.pivot, rotX(part.sub == 1 ? -pose.propAngle : pose.propAngle)); break;
            case procgen::PartKind::Rudder: m = aboutPivot(part.pivot, rotZ(pose.rudder)); break;
            default: break;
        }
        if (part.parent >= 0 && size_t(part.parent) < i) {
            m = local[size_t(part.parent)] * m;
            hide = hide || hidden[size_t(part.parent)];
        }
        local[i] = m;
        hidden[i] = hide ? 1 : 0;
    }
}

std::vector<Muzzle> muzzles(const GpuMesh& mesh, const ShipPose& pose, int mount) {
    std::vector<Mat4f> local;
    std::vector<uint8_t> hidden;
    partTransforms(mesh, pose, local, hidden);
    std::vector<Muzzle> out;
    for (size_t i = 0; i < mesh.parts.size(); ++i) {
        const auto& part = mesh.parts[i];
        if (part.kind != procgen::PartKind::Gun || (mount >= 0 && part.mount != mount)) continue;
        Muzzle mz;
        mz.mount = part.mount;
        mz.barrel = part.sub;
        mz.pos = local[i].transformPoint(part.pivot + part.axis * part.reach);
        mz.dir = normalize(local[i].transformDir(part.axis));
        out.push_back(mz);
    }
    return out;
}

void recordShipEvents(Renderer::Impl& P, const sim::World& w, const std::vector<sim::Event>& events) {
    for (const auto& e : events) {
        if (e.type != sim::EventType::GunFired) continue;
        const sim::Ship* s = w.ship(e.a);
        if (!s) continue;
        ShipAnim& a = P.anims[e.a];
        if (a.fired.size() != s->mounts.size()) a.fired.assign(s->mounts.size(), -1e9);
        size_t mi = size_t(e.value2);
        if (mi < a.fired.size()) a.fired[mi] = e.time;
    }
}

void updateShipAnims(Renderer::Impl& P, const sim::World& w) {
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (s.torpedoMounts.empty()) continue;
        ShipAnim& a = P.anims[s.id];
        size_t n = s.torpedoMounts.size();
        if (a.torpLoaded.size() != n) {
            a.torpLoaded.resize(n);
            a.torpLaunch.assign(n, -1e9);
            a.torpFrom.assign(n, 0);
            a.torpTo.assign(n, 0);
            for (size_t i = 0; i < n; ++i) a.torpLoaded[i] = s.torpedoMounts[i].loaded;
        }
        for (size_t i = 0; i < n; ++i) {
            int now = s.torpedoMounts[i].loaded;
            if (now < a.torpLoaded[i] && s.torpedoMounts[i].health > 0.0 && !s.dmg.sunk) {
                a.torpLaunch[i] = w.time();
                a.torpFrom[i] = now;
                a.torpTo[i] = a.torpLoaded[i];
            }
            a.torpLoaded[i] = now;
        }
    }
}

void drawFlyingAircraft(Renderer::Impl& P, GpuDevice& dev, const sim::World& w, const Camera& cam, double visualTime) {
    auto* cmd = dev.cmd();
    double lead = clamp(visualTime - w.time(), 0.0, 0.25);
    for (const auto& a : w.aircraft()) {
        if (a.state == sim::AircraftMission::State::Launching || a.state == sim::AircraftMission::State::Done) continue;
        const sim::Ship* parent = w.ship(a.parent);
        if (!parent || !parent->cls) continue;
        auto it = P.meshes.find(parent->cls->id);
        if (it == P.meshes.end()) continue;
        const GpuMesh& mesh = *it->second;
        const GpuMesh::Part* plane = nullptr;
        for (const auto& part : mesh.parts)
            if (part.kind == procgen::PartKind::Aircraft && part.sub == 0) plane = &part;
        if (!plane || !plane->indexCount) continue;
        Vec2 pos = a.pos + bearingVector(a.heading) * (a.speed * lead);
        double alt = a.altitude;
        if (a.state == sim::AircraftMission::State::Outbound) alt = std::min(alt, 30.0 + a.stateTime * 6.0);  // climbing out
        Vec3 rel = Vec3{pos.x, alt, pos.y} - cam.pos;
        if (length(rel) > 15000.0) continue;
        float bank = a.state == sim::AircraftMission::State::OnStation ? 22.0f : 0.0f;
        Mat4f world = Mat4f::translation({float(rel.x), float(rel.y), float(rel.z)}) * modelToRenderBasis(a.heading) * rotX(-bank) *
                      Mat4f::translation(-plane->pivot);
        ObjectConstants oc{world, {0, 0, 0, 0}, {float(a.id % 97), -1e4f, 10.0f, 2.0f}};
        cmd->IASetVertexBuffers(0, 1, &mesh.vbv);
        cmd->IASetIndexBuffer(&mesh.ibv);
        cmd->SetGraphicsRootConstantBufferView(1, dev.constants(oc));
        cmd->DrawIndexedInstanced(plane->indexCount, 1, plane->firstIndex, 0, 0);
    }
}

}  // namespace it::render
