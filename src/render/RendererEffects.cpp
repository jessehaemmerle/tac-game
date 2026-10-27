// Visual effects driven by simulation events and state: muzzle flashes, splashes, hits, fires, smoke,
// starshells, searchlights, ship and torpedo wakes, foam rings. Purely presentational (no gameplay effect).
#include <algorithm>

#include "render/RendererImpl.h"

namespace it::render {

namespace {

constexpr size_t kMaxParticles = 14000;
constexpr size_t kMaxLights = 96;

Vec3 toR(const Vec3& sim) { return {sim.x, sim.z, sim.y}; }
Vec3 toR(const Vec2& sim, double up) { return {sim.x, up, sim.y}; }

double frand(Rng& r, double lo, double hi) { return r.uniform(lo, hi); }

void add(Renderer::Impl& P, const Particle& p) {
    if (P.particles.size() < kMaxParticles) P.particles.push_back(p);
}

/// Rain and snow have a budget of their own on top of the effects (a fleet's funnel smoke must not
/// stop the weather).
constexpr size_t kMaxWeatherParticles = 6000;
void addWeather(Renderer::Impl& P, const Particle& p) {
    if (P.particles.size() < kMaxParticles + kMaxWeatherParticles) P.particles.push_back(p);
}

/// Winter at high latitudes: the squalls bring snow (the Arctic convoys, the North Cape).
bool snowing(const sim::World& w) {
    int m = w.env().now().month;
    double lat = w.env().latitude();
    return (lat > 50.0 && (m >= 11 || m <= 3)) || (lat < -50.0 && m >= 5 && m <= 9);
}

/// How hard it rains (snows) where the camera is: 0 outside the squalls.
double precipitationAt(const sim::World& w, const Vec2& p) {
    double k = 0.0;
    for (const auto& sq : w.env().squalls()) {
        double d = distance(sq.pos, p);
        k = std::max(k, sq.intensity * (1.0 - smoothstep(sq.radius * 0.8, sq.radius * 1.1, d)));
    }
    return k;
}

Vec3 windDrift(const sim::World& w) {
    const auto& wx = w.env().weather();
    double to = (wx.windDirDeg + 180.0) * kDegToRad;
    double v = wx.windSpeedKn * 0.514444 * 0.6;
    return {std::sin(to) * v, 0.0, std::cos(to) * v};
}

void flash(Renderer::Impl& P, const Vec3& pos, const Vec3f& color, float radius, float life) {
    FlashLight f;
    f.pos = pos;
    f.color = color;
    f.radius = radius;
    f.maxLife = life;
    P.flashes.push_back(f);
}

void glow(Renderer::Impl& P, const Vec3& pos, float size, const Vec4f& color, float emissive, float life, int type = kGlow) {
    Particle g;
    g.pos = pos;
    g.size = size;
    g.color = color;
    g.emissive = emissive;
    g.maxLife = life;
    g.type = type;
    g.seed = float(P.rng.uniform());
    g.drag = 0;
    add(P, g);
}

void smokePuff(Renderer::Impl& P, const Vec3& pos, const Vec3& vel, float size, float growth, const Vec4f& color, float life, float buoyancy = 1.0f,
               float emissive = 0.0f) {
    Particle s;
    s.pos = pos;
    s.vel = vel;
    s.size = size;
    s.growth = growth;
    s.color = color;
    s.maxLife = life;
    s.type = kSmoke;
    s.seed = float(P.rng.uniform());
    s.drag = 0.6f;
    s.buoyancy = buoyancy;
    s.emissive = emissive;
    add(P, s);
}

void column(Renderer::Impl& P, const Vec3& pos, float halfWidth, float height, float life, const Vec4f& color) {
    Particle c;
    c.pos = {pos.x, 0.0, pos.z};
    c.size = halfWidth;
    c.stretch = height / std::max(0.5f, halfWidth);
    c.color = color;
    c.maxLife = life;
    c.type = kColumn;
    c.seed = float(P.rng.uniform());
    c.drag = 0;
    add(P, c);
}

void sparks(Renderer::Impl& P, const Vec3& pos, int n, float speed, const Vec4f& color, float emissive) {
    for (int i = 0; i < n; ++i) {
        Particle s;
        s.pos = pos;
        double a = P.rng.uniform(0, kTwoPi), up = P.rng.uniform(0.2, 1.0);
        s.vel = Vec3{std::cos(a) * (1 - up * 0.5), up, std::sin(a) * (1 - up * 0.5)} * double(speed * P.rng.uniform(0.4, 1.0));
        s.size = float(P.rng.uniform(0.6, 1.4));
        s.color = color;
        s.emissive = emissive;
        s.maxLife = float(P.rng.uniform(0.5, 1.6));
        s.type = kSpark;
        s.drag = 0.4f;
        s.buoyancy = -9.81f;
        s.seed = float(P.rng.uniform());
        add(P, s);
    }
}

/// A tracer round from `from` towards `to` (render space) at `speed`, slightly dispersed.
void tracer(Renderer::Impl& P, const Vec3& from, const Vec3& to, double speed, double spreadDeg, const Vec4f& color, float width) {
    Vec3 d = to - from;
    double dist = length(d);
    if (dist < 1.0) return;
    Vec3 dir = d / dist;
    Vec3 jitter{P.rng.normal(), P.rng.normal(), P.rng.normal()};
    dir = normalize(dir + jitter * (spreadDeg * kDegToRad));
    Particle t;
    t.pos = from;
    t.vel = dir * speed;
    t.size = width;
    t.color = color;
    t.emissive = 14.0f;
    t.maxLife = float(std::min(4.0, dist / speed + 0.4));
    t.type = kTracer;
    t.drag = 0.0f;
    t.buoyancy = -2.0f;  // a slight drop over a few seconds of flight
    t.seed = float(P.rng.uniform());
    add(P, t);
}

/// Tracer colours by navy (US red, Japanese pale green-white, British orange, German yellow).
Vec4f tracerColor(sim::Nation n) {
    switch (n) {
        case sim::Nation::USN: return {1.0f, 0.32f, 0.18f, 1.0f};
        case sim::Nation::IJN: return {0.85f, 1.0f, 0.75f, 1.0f};
        case sim::Nation::KM: return {1.0f, 0.85f, 0.35f, 1.0f};
        case sim::Nation::RM: return {1.0f, 0.45f, 0.3f, 1.0f};
        default: return {1.0f, 0.55f, 0.25f, 1.0f};
    }
}

void foamRing(Renderer::Impl& P, const Vec3& pos, float radius, float strength, float life, double now) {
    if (P.foam.size() > 600) P.foam.erase(P.foam.begin());
    P.foam.push_back({Vec2{pos.x, pos.z}, now, radius, strength, life});
}

}  // namespace

// -------------------------------------------------------------------------------------------- events
void effectsEvents(Renderer::Impl& P, const sim::World& w, const std::vector<sim::Event>& events) {
    using sim::EventType;
    const double now = w.time();
    Vec3 drift = windDrift(w);
    for (const auto& e : events) {
        Vec3 pos = toR(e.pos);
        switch (e.type) {
            case EventType::GunFired: {
                double cal = std::max(20.0, e.value);
                double k = cal / 203.0;
                const sim::Ship* s = w.ship(e.a);
                Vec3 dir{0, 0, 0};
                if (s && size_t(e.value2) < s->mounts.size()) {
                    double brg = s->heading + s->mounts[size_t(e.value2)].train;
                    dir = {std::sin(brg * kDegToRad), 0.15, std::cos(brg * kDegToRad)};
                }
                Vec3 muzzle = pos + dir * (cal * 0.06);
                // At the barrel tips of the model, when it is loaded: one flash per barrel.
                if (s && s->cls) {
                    auto it = P.meshes.find(s->cls->id);
                    if (it != P.meshes.end()) {
                        ShipPose pose = shipPose(P, *s, w, nullptr, now);
                        Mat4f m = shipWorldMatrix(*s, Vec3{0, 0, 0}, now, w.env().weather().seaState);
                        auto tips = muzzles(*it->second, pose, int(e.value2));
                        for (size_t b = 1; b < tips.size(); ++b) {
                            Vec3f tp = m.transformPoint(tips[b].pos);
                            glow(P, Vec3{tp.x, tp.y, tp.z} + dir * (cal * 0.02), float(2.0 + 4.0 * k), {1.0f, 0.7f, 0.35f, 1.0f}, float(25.0 * std::sqrt(k)), 0.12f);
                        }
                        if (!tips.empty()) {
                            Vec3f tp = m.transformPoint(tips[0].pos);
                            Vec3f d = m.transformDir(tips[0].dir);
                            dir = Vec3{d.x, d.y, d.z};
                            muzzle = Vec3{tp.x, tp.y, tp.z};
                        }
                    }
                }
                glow(P, muzzle + dir * (cal * 0.03), float(3.0 + 6.0 * k), {1.0f, 0.72f, 0.38f, 1.0f}, float(40.0 * std::sqrt(k)), 0.14f);
                glow(P, muzzle, float(8.0 + 12.0 * k), {1.0f, 0.55f, 0.25f, 1.0f}, float(3.0 * std::sqrt(k)), 0.25f);
                flash(P, muzzle + Vec3{0, 4, 0}, Vec3f{1.0f, 0.68f, 0.38f} * float(22.0 * k), float(700.0 + 1200.0 * k), 0.2f);
                P.flashGlow = std::min(1.5, P.flashGlow + 0.06 * k);
                int puffs = 2 + int(k * 3);
                for (int i = 0; i < puffs; ++i)
                    smokePuff(P, muzzle + dir * (cal * 0.05 * i), dir * (20.0 * k + 6.0) + drift + Vec3{0, 1.5, 0}, float(3.0 + 3.0 * k),
                              float(2.0 + 2.0 * k), {0.42f, 0.40f, 0.37f, 0.55f}, float(frand(P.rng, 7.0, 13.0)), 0.6f);
                break;
            }
            case EventType::ShellSplash: {
                double cal = std::max(20.0, e.value);
                double k = cal / 203.0;
                float height = float(14.0 + 46.0 * k * (0.8 + 0.4 * P.rng.uniform()));
                column(P, pos, float(2.0 + 4.0 * k), height, float(5.0 + 3.0 * k), {0.82f, 0.86f, 0.88f, 0.92f});
                smokePuff(P, pos + Vec3{0, height * 0.3, 0}, drift + Vec3{0, 1.0, 0}, float(4.0 + 6.0 * k), float(3.0 + 3.0 * k),
                          {0.75f, 0.78f, 0.80f, 0.35f}, 7.0f, 0.3f);
                foamRing(P, pos, float(10.0 + 30.0 * k), 1.0f, 35.0f, now);
                break;
            }
            case EventType::ShellHit: {
                double cal = std::max(20.0, e.value);
                double k = cal / 203.0;
                bool pen = e.value2 > 0.5;
                glow(P, pos, float(6.0 + 14.0 * k), {1.0f, 0.62f, 0.28f, 1.0f}, float((pen ? 18.0 : 45.0) * std::sqrt(k)), 0.28f);
                flash(P, pos + Vec3{0, 3, 0}, Vec3f{1.0f, 0.55f, 0.25f} * float((pen ? 20.0 : 45.0) * k), float(500.0 + 800.0 * k), 0.35f);
                sparks(P, pos, int(8 + 14 * k), float(25.0 + 25.0 * k), {1.0f, 0.7f, 0.35f, 1.0f}, 30.0f);
                for (int i = 0; i < 3; ++i)
                    smokePuff(P, pos, drift + Vec3{frand(P.rng, -3, 3), frand(P.rng, 3, 8), frand(P.rng, -3, 3)}, float(3.0 + 4.0 * k),
                              float(3.0 + 3.0 * k), {0.16f, 0.15f, 0.14f, 0.75f}, float(frand(P.rng, 10, 20)), 1.5f, 0.8f);
                P.flashGlow = std::min(1.5, P.flashGlow + 0.1 * k);
                break;
            }
            case EventType::StarshellBurst:
            case EventType::FlareDropped:
                glow(P, pos, 18.0f, {1.0f, 0.95f, 0.85f, 1.0f}, 30.0f, 0.4f);
                sparks(P, pos, 10, 18.0f, {1.0f, 0.9f, 0.7f, 1.0f}, 25.0f);
                break;
            case EventType::TorpedoLaunched: {
                if (const sim::Ship* s = w.ship(e.a)) {
                    Vec3 side = toR(s->pos + s->right() * (s->cls ? s->cls->beam * 0.7 : 6.0), 0.0);
                    column(P, side, 2.0f, 6.0f, 3.0f, {0.85f, 0.88f, 0.9f, 0.8f});
                    foamRing(P, side, 8.0f, 0.8f, 15.0f, now);
                }
                break;
            }
            case EventType::TorpedoHit: {
                column(P, pos, 14.0f, float(70.0 + 40.0 * P.rng.uniform()), 11.0f, {0.86f, 0.89f, 0.92f, 0.95f});
                column(P, pos + Vec3{6, 0, 4}, 9.0f, 55.0f, 9.0f, {0.8f, 0.84f, 0.88f, 0.9f});
                glow(P, pos + Vec3{0, 4, 0}, 40.0f, {1.0f, 0.6f, 0.3f, 1.0f}, 25.0f, 0.4f);
                flash(P, pos + Vec3{0, 6, 0}, Vec3f{1.0f, 0.6f, 0.3f} * 120.0f, 2500.0f, 0.5f);
                for (int i = 0; i < 6; ++i)
                    smokePuff(P, pos, drift + Vec3{frand(P.rng, -4, 4), frand(P.rng, 4, 12), frand(P.rng, -4, 4)}, 10.0f, 5.0f,
                              {0.2f, 0.19f, 0.18f, 0.7f}, float(frand(P.rng, 20, 35)), 2.0f);
                foamRing(P, pos, 90.0f, 1.0f, 60.0f, now);
                P.flashGlow = std::min(1.5, P.flashGlow + 0.4);
                break;
            }
            case EventType::TorpedoDud:
                column(P, pos, 3.0f, 8.0f, 3.0f, {0.85f, 0.88f, 0.9f, 0.7f});
                break;
            case EventType::TorpedoDropped:
                column(P, pos, 2.0f, 9.0f, 3.0f, {0.85f, 0.88f, 0.9f, 0.85f});
                foamRing(P, pos, 10.0f, 0.8f, 20.0f, now);
                break;
            case EventType::AaFire: {
                // Heavy AA: a muzzle flash, then a burst near the aircraft after the shell's time of flight.
                double cal = std::max(75.0, e.value);
                glow(P, pos, float(4.0 + cal * 0.03), {1.0f, 0.7f, 0.4f, 1.0f}, 18.0f, 0.12f);
                smokePuff(P, pos, drift + Vec3{0, 6, 0}, 2.5f, 2.0f, {0.45f, 0.43f, 0.4f, 0.45f}, 6.0f, 0.6f);
                if (const sim::Aircraft* a = w.plane(e.b)) {
                    Vec3 ap = toR(a->pos);
                    double tof = length(ap - pos) / 700.0;
                    Vec3 lead = ap + toR(bearingVector(a->heading) * (a->speed * tof), 0.0);
                    double miss = 25.0 + 0.012 * length(ap - pos);
                    Vec3 burst = lead + Vec3{P.rng.normal(), P.rng.normal() * 0.6, P.rng.normal()} * miss;
                    P.flak.push_back({burst, now + tof, float(4.0 + cal * 0.035), w.env().ambientLight() < 0.3});
                }
                break;
            }
            case EventType::AircraftHit:
                sparks(P, pos, 6, 20.0f, {1.0f, 0.75f, 0.4f, 1.0f}, 20.0f);
                break;
            case EventType::AircraftShotDown:
                glow(P, pos, 10.0f, {1.0f, 0.55f, 0.2f, 1.0f}, 25.0f, 0.5f);
                sparks(P, pos, 18, 30.0f, {1.0f, 0.7f, 0.35f, 1.0f}, 25.0f);
                flash(P, pos, Vec3f{1.0f, 0.55f, 0.25f} * 20.0f, 600.0f, 0.4f);
                break;
            case EventType::AircraftCrashed: {
                Vec3 p = {pos.x, 0.0, pos.z};
                column(P, p, 5.0f, 28.0f, 5.0f, {0.86f, 0.89f, 0.92f, 0.9f});
                glow(P, p + Vec3{0, 3, 0}, 14.0f, {1.0f, 0.55f, 0.2f, 1.0f}, 20.0f, 1.2f);
                for (int i = 0; i < 4; ++i)
                    smokePuff(P, p + Vec3{0, 3, 0}, drift + Vec3{frand(P.rng, -2, 2), frand(P.rng, 3, 7), frand(P.rng, -2, 2)}, 5.0f, 3.0f,
                              {0.08f, 0.075f, 0.07f, 0.8f}, float(frand(P.rng, 20, 40)), 1.5f, 0.6f);
                foamRing(P, p, 25.0f, 1.0f, 40.0f, now);
                break;
            }
            case EventType::Explosion: {
                double size = std::cbrt(std::max(1.0, e.value)) * 2.5;
                glow(P, pos, float(size * 2.0), {1.0f, 0.55f, 0.22f, 1.0f}, 30.0f, 0.6f);
                flash(P, pos + Vec3{0, 5, 0}, Vec3f{1.0f, 0.55f, 0.25f} * float(10.0 * size), float(400.0 * size), 0.7f);
                sparks(P, pos, 24, 35.0f, {1.0f, 0.65f, 0.3f, 1.0f}, 25.0f);
                for (int i = 0; i < 5; ++i) {
                    Particle f;
                    f.pos = pos + Vec3{frand(P.rng, -2, 2), frand(P.rng, 0, 3), frand(P.rng, -2, 2)};
                    f.vel = Vec3{frand(P.rng, -3, 3), frand(P.rng, 3, 9), frand(P.rng, -3, 3)};
                    f.size = float(size * 0.8);
                    f.growth = float(size * 0.6);
                    f.color = {1, 1, 1, 1};
                    f.emissive = 12.0f;
                    f.maxLife = float(frand(P.rng, 1.0, 2.0));
                    f.type = kFire;
                    f.seed = float(P.rng.uniform());
                    add(P, f);
                }
                for (int i = 0; i < 6; ++i)
                    smokePuff(P, pos, drift + Vec3{frand(P.rng, -4, 4), frand(P.rng, 5, 12), frand(P.rng, -4, 4)}, float(size), float(size * 0.4),
                              {0.08f, 0.075f, 0.07f, 0.85f}, float(frand(P.rng, 25, 45)), 2.5f);
                P.flashGlow = std::min(1.5, P.flashGlow + 0.3);
                break;
            }
            case EventType::MagazineExplosion: {
                const sim::Ship* s = w.ship(e.a);
                Vec3 p = s ? toR(s->pos, 10.0) : pos;
                glow(P, p + Vec3{0, 60, 0}, 220.0f, {1.0f, 0.6f, 0.25f, 1.0f}, 30.0f, 1.6f);
                glow(P, p + Vec3{0, 20, 0}, 90.0f, {1.0f, 0.8f, 0.5f, 1.0f}, 80.0f, 0.5f);
                flash(P, p + Vec3{0, 80, 0}, Vec3f{1.0f, 0.6f, 0.3f} * 1500.0f, 12000.0f, 2.5f);
                sparks(P, p + Vec3{0, 10, 0}, 120, 90.0f, {1.0f, 0.7f, 0.35f, 1.0f}, 30.0f);
                for (int i = 0; i < 26; ++i) {
                    double h = frand(P.rng, 0, 500);
                    smokePuff(P, p + Vec3{frand(P.rng, -30, 30), h, frand(P.rng, -30, 30)},
                              drift + Vec3{frand(P.rng, -6, 6), 25.0 - h * 0.03, frand(P.rng, -6, 6)}, float(30 + h * 0.15), 6.0f,
                              {0.12f, 0.1f, 0.09f, 0.9f}, float(frand(P.rng, 60, 120)), 0.5f, i < 8 ? 2.0f : 0.0f);
                }
                column(P, p, 40.0f, 180.0f, 12.0f, {0.8f, 0.82f, 0.85f, 0.9f});
                foamRing(P, p, 200.0f, 1.0f, 120.0f, now);
                P.flashGlow = 1.5;
                break;
            }
            case EventType::ShipSunk: {
                if (const sim::Ship* s = w.ship(e.a)) {
                    Vec3 p = toR(s->pos, 2.0);
                    for (int i = 0; i < 10; ++i)
                        smokePuff(P, p + Vec3{frand(P.rng, -40, 40), 0, frand(P.rng, -40, 40)}, drift + Vec3{0, frand(P.rng, 2, 6), 0}, 14.0f,
                                  3.0f, {0.75f, 0.77f, 0.8f, 0.45f}, float(frand(P.rng, 20, 40)), 1.0f);
                    foamRing(P, p, float(s->cls ? s->cls->length : 100.0), 1.0f, 180.0f, now);
                }
                break;
            }
            default: break;
        }
    }
}

// --------------------------------------------------------------------------------------------- update
void effectsUpdate(Renderer::Impl& P, const sim::World& w, double realDt, double simDt, double visualTime) {
    (void)realDt;
    const double dt = std::min(simDt, 0.5);
    P.flashGlow *= std::exp(-dt * 5.0);
    if (dt <= 0.0) return;
    for (auto& p : P.particles) {
        p.life += float(dt);
        p.pos += p.vel * dt;
        p.vel *= std::exp(-double(p.drag) * dt);
        p.vel.y += p.buoyancy * dt;
        p.size += p.growth * float(dt);
        if (p.type == kSpark && p.pos.y < 0.0) p.life = p.maxLife;
    }
    P.particles.erase(std::remove_if(P.particles.begin(), P.particles.end(), [](const Particle& p) { return p.life >= p.maxLife; }),
                      P.particles.end());
    for (auto& f : P.flashes) f.life += float(dt);
    P.flashes.erase(std::remove_if(P.flashes.begin(), P.flashes.end(), [](const FlashLight& f) { return f.life >= f.maxLife; }), P.flashes.end());
    P.foam.erase(std::remove_if(P.foam.begin(), P.foam.end(), [&](const FoamStamp& f) { return w.time() - f.born > f.life; }), P.foam.end());

    // Continuous emitters on ships: funnel smoke, smoke screens, fires, steam from sinking hulls.
    Vec3 drift = windDrift(w);
    const Vec3 cam = P.cam.pos;
    // Rain (or snow) falling around the camera inside a squall. The drops and flakes are drawn larger
    // than life so that they read on screen; only those within a few dozen metres are simulated.
    if (double precip = precipitationAt(w, {cam.x, cam.z}); precip > 0.02 && cam.y < 3000.0) {
        bool snow = snowing(w);
        int n = std::min(240, int(dt * (snow ? 800.0 : 1200.0) * precip + P.rng.uniform(0.0, 1.0)));
        for (int i = 0; i < n; ++i) {
            Particle r;
            // More of them close to the camera, where they are seen.
            double ang = frand(P.rng, 0.0, kTwoPi), dist = 6.0 + 30.0 * std::pow(frand(P.rng, 0.0, 1.0), 1.4);
            r.pos = {cam.x + std::sin(ang) * dist, std::max(0.5, cam.y + frand(P.rng, -8.0, 14.0)), cam.z + std::cos(ang) * dist};
            r.type = snow ? kSnow : kRain;
            r.drag = 0.0f;
            r.seed = float(frand(P.rng, 0.0, 1.0));
            if (snow) {
                r.vel = drift * 1.2 + Vec3{frand(P.rng, -0.5, 0.5), -frand(P.rng, 0.9, 1.6), frand(P.rng, -0.5, 0.5)};
                r.size = float(frand(P.rng, 0.07, 0.15));
                r.color = {1.0f, 1.0f, 1.0f, 0.95f};
                r.maxLife = float(std::min(6.0, r.pos.y / -r.vel.y));
            } else {
                r.vel = drift * 0.7 + Vec3{0.0, -frand(P.rng, 8.0, 10.0), 0.0};
                r.size = 0.03f;
                r.color = {0.88f, 0.9f, 0.94f, float(0.35 + 0.3 * precip)};
                r.maxLife = float(std::min(2.2, r.pos.y / -r.vel.y));
            }
            addWeather(P, r);
        }
    }
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.cls || s.dmg.sunk) continue;
        const auto& c = *s.cls;
        Vec3 shipR = toR(s.pos, 0.0);
        double dist = length(shipR - cam);
        double lod = dist < 4000 ? 1.0 : dist < 12000 ? 0.4 : 0.12;
        auto local = [&](double x, double y, double z) { return toR(s.localToWorld(x, y), z); };
        // Funnels.
        double speedK = clamp(s.speed / std::max(1.0, c.maxSpeedKn * 0.514444), 0.0, 1.0);
        bool smokeScreen = s.orders.makeSmoke && s.operational();
        for (const auto& part : c.model.parts) {
            if (part.type != "funnel") continue;
            double f = part.f2 >= 0 ? 0.5 * (part.f + part.f2) : part.f;
            double x = (0.5 - f) * c.length - part.h * std::tan(part.rake * kDegToRad);
            double z = c.model.deckHeightAt(f) + part.z + part.h;
            double rate = (0.5 + 1.5 * speedK) * (smokeScreen ? 6.0 : 1.0) * lod;
            if (P.rng.uniform() < rate * dt) {
                Vec4f col = smokeScreen ? Vec4f{0.05f, 0.05f, 0.05f, 0.9f} : Vec4f{0.28f, 0.27f, 0.26f, 0.35f + 0.25f * float(speedK)};
                smokePuff(P, local(x, 0, z), drift + Vec3{0, 3.0, 0} - toR(s.velocity(), 0.0) * 0.2,
                          float(std::max(1.5, part.wid * 0.35)), smokeScreen ? 5.0f : 2.2f, col, float(smokeScreen ? 120.0 : 40.0), 0.4f);
            }
        }
        // Fires.
        for (const auto& fire : s.dmg.fires) {
            double x = (0.5 - fire.f) * c.length;
            double z = c.model.deckHeightAt(fire.f) + 1.5;
            Vec3 fp = local(x, P.rng.uniform(-c.beam * 0.3, c.beam * 0.3), z);
            if (P.rng.uniform() < 8.0 * fire.intensity * dt * std::max(lod, 0.3)) {
                Particle f;
                f.pos = fp;
                f.vel = drift * 0.3 + Vec3{0, frand(P.rng, 2, 5), 0};
                f.size = float(2.0 + 5.0 * fire.intensity);
                f.growth = 1.5f;
                f.color = {1, 1, 1, 1};
                f.emissive = float(6.0 + 8.0 * fire.intensity);
                f.maxLife = float(frand(P.rng, 0.7, 1.4));
                f.type = kFire;
                f.drag = 0.3f;
                f.seed = float(P.rng.uniform());
                add(P, f);
            }
            if (P.rng.uniform() < 2.5 * fire.intensity * dt * lod)
                smokePuff(P, fp + Vec3{0, 4, 0}, drift + Vec3{0, 5, 0}, float(3.0 + 5.0 * fire.intensity), 4.0f, {0.07f, 0.065f, 0.06f, 0.85f},
                          float(frand(P.rng, 40, 70)), 1.2f, 1.2f);
        }
        if (s.dmg.sinking && P.rng.uniform() < 3.0 * dt * lod)
            smokePuff(P, local(P.rng.uniform(-0.4, 0.4) * c.length, 0, 2.0), drift + Vec3{0, 3, 0}, 6.0f, 3.0f, {0.7f, 0.72f, 0.75f, 0.4f}, 25.0f,
                      0.8f);
    }

    // Heavy AA bursts whose shells have arrived: a flash and a black (by night, lit) puff.
    for (auto& b : P.flak) {
        if (b.at > w.time()) continue;
        glow(P, b.pos, b.size * 1.4f, {1.0f, 0.6f, 0.25f, 1.0f}, b.night ? 30.0f : 10.0f, 0.15f);
        if (b.night) flash(P, b.pos, Vec3f{1.0f, 0.6f, 0.3f} * 8.0f, 800.0f, 0.15f);
        smokePuff(P, b.pos, drift * 0.5, b.size, 1.2f, {0.07f, 0.07f, 0.07f, 0.85f}, float(frand(P.rng, 18, 30)), 0.05f);
        b.at = -1.0;
    }
    P.flak.erase(std::remove_if(P.flak.begin(), P.flak.end(), [](const FlakBurst& b) { return b.at < 0.0; }), P.flak.end());
    // Light AA: streams of tracer from every battery that is firing.
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.cls || !s.operational() || length(toR(s.pos, 0.0) - cam) > 16000.0) continue;
        for (const auto& b : s.aa) {
            if (b.mount >= 0 || !b.def || b.firingUntil < w.time() || b.target == sim::kNoEntity) continue;
            const sim::Aircraft* a = w.plane(b.target);
            if (!a) continue;
            double rate = std::min(6.0, b.def->barrels * b.def->rofRpm / 60.0 * 0.22);  // every fourth or fifth round a tracer
            int n = int(rate * dt + P.rng.uniform());
            Vec3 gun = toR(s.localToWorld(b.local.x, b.local.y), b.local.z);
            Vec3 ap = toR(a->pos);
            double tof = length(ap - gun) / 850.0;
            Vec3 lead = ap + toR(bearingVector(a->heading) * (a->speed * tof * 0.8), 0.0);
            for (int k = 0; k < n; ++k)
                tracer(P, gun, lead, 850.0, 1.2 + b.def->caliberMm * 0.01, tracerColor(s.nation), b.def->caliberMm >= 37.0 ? 0.35f : 0.22f);
            if (n > 0) glow(P, gun, 1.5f, {1.0f, 0.7f, 0.4f, 1.0f}, 10.0f, 0.06f);
        }
    }
    // Aircraft: gunfire, smoke from damaged and burning aircraft.
    for (const auto& a : w.planes()) {
        bool falling = a.fate == sim::Aircraft::Fate::Falling;
        if (!a.flying() && !falling) continue;
        Vec3 ap = toR(a.pos);
        if (length(ap - cam) > 14000.0) continue;
        if (a.flying() && w.time() - a.lastGuns < 0.3) {
            Vec3 tgt;
            bool ok = false;
            if (const sim::Aircraft* t = w.plane(a.gunsAt)) tgt = toR(t->pos), ok = true;
            else if (const sim::Ship* sh = w.ship(a.gunsAt)) tgt = toR(sh->pos, 4.0), ok = true;
            if (ok && P.rng.uniform() < 8.0 * dt)
                tracer(P, ap, tgt, 900.0, 1.0, tracerColor(a.def ? a.def->nation : sim::Nation::Unknown), 0.18f);
        }
        if ((falling || a.smoke > 0.25) && P.rng.uniform() < (falling ? 12.0 : 5.0 * a.smoke) * dt) {
            bool fire = falling || a.smoke > 0.7;
            smokePuff(P, ap, drift * 0.3, float(fire ? 3.0 : 2.0), 1.8f, fire ? Vec4f{0.08f, 0.07f, 0.06f, 0.85f} : Vec4f{0.3f, 0.3f, 0.3f, 0.5f},
                      float(frand(P.rng, 10, 20)), 0.2f, fire ? 1.5f : 0.0f);
            if (fire) glow(P, ap, 3.0f, {1.0f, 0.5f, 0.15f, 1.0f}, 14.0f, 0.2f);
        }
    }

    // Wake trails: sample the stern every second of simulated time.
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.cls) continue;
        auto& tr = P.trails[s.id];
        if (!s.dmg.sunk && s.speed > 0.3) {
            Vec2 stern = s.pos - s.forward() * (s.cls->length * 0.45);
            if (tr.points.empty() || visualTime - tr.points.front().second >= 1.0) tr.points.push_front({stern, visualTime});
            tr.lastSeen = visualTime;
        }
        while (!tr.points.empty() && visualTime - tr.points.back().second > 160.0) tr.points.pop_back();
    }
    for (const auto& t : w.torpedoes()) {
        if (!t.alive || !t.def || t.def->wakeVisibility < 0.15) continue;
        auto& tr = P.torpedoTrails[t.id];
        if (tr.points.empty() || visualTime - tr.points.front().second >= 1.0) tr.points.push_front({t.pos, visualTime});
        tr.lastSeen = visualTime;
    }
    for (auto it = P.torpedoTrails.begin(); it != P.torpedoTrails.end();) {
        auto& pts = it->second.points;
        while (!pts.empty() && visualTime - pts.back().second > 60.0) pts.pop_back();
        it = pts.empty() ? P.torpedoTrails.erase(it) : std::next(it);
    }
}

// --------------------------------------------------------------------------------------------- gather
void effectsGather(Renderer::Impl& P, const sim::World& w, const Camera& cam, double visualTime) {
    const Vec3 camPos = cam.pos;
    auto rel = [&](const Vec3& p) { return Vec3f{float(p.x - camPos.x), float(p.y - camPos.y), float(p.z - camPos.z)}; };
    // ---- Lights
    struct Cand {
        GpuLight l;
        double score;
    };
    std::vector<Cand> cands;
    auto addLight = [&](const Vec3& pos, const Vec3f& color, float radius, int type = 0, const Vec3f& dir = {}, float cosAngle = 0.0f) {
        GpuLight l{};
        l.pos = rel(pos);
        l.radius = radius;
        l.color = color;
        l.type = float(type);
        l.dir = dir;
        l.cosAngle = cosAngle;
        double d = length(Vec3{l.pos.x, l.pos.y, l.pos.z});
        double brightness = std::max({color.x, color.y, color.z});
        if (d > radius * 4.0) return;
        cands.push_back({l, brightness * radius / (d + 50.0)});
    };
    for (const auto& f : P.flashes) {
        float k = 1.0f - f.life / f.maxLife;
        addLight(f.pos, f.color * (k * k), f.radius);
    }
    for (const auto& fl : w.flares()) {
        if (!fl.alive) continue;
        float fade = float(clamp(std::min(fl.life / 1.5, (fl.maxLife - fl.life) / 4.0), 0.0, 1.0));
        Vec3 p = toR(fl.pos);
        addLight(p, Vec3f{1.0f, 0.95f, 0.85f} * float(45.0 * fl.intensity * fade), 3200.0f);
        Particle g;  // the flare itself (transient)
        g.pos = p;
        g.size = 6.0f;
        g.color = {1.0f, 0.95f, 0.85f, 1.0f};
        g.emissive = 120.0f * fade;
        g.type = kGlow;
        g.maxLife = 1.0f;
        g.life = 0.5f;
        P.particles.push_back(g);
        P.particles.back().maxLife = -1.0f;  // marker: transient, removed after gather
    }
    std::vector<GpuParticle> beams;
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.cls || s.dmg.sunk) continue;
        const auto& c = *s.cls;
        for (const auto& fire : s.dmg.fires) {
            double x = (0.5 - fire.f) * c.length;
            Vec3 p = toR(s.localToWorld(x, 0), c.model.deckHeightAt(fire.f) + 4.0);
            float flicker = float(0.8 + 0.2 * std::sin(visualTime * 13.0 + fire.f * 50.0) * std::sin(visualTime * 7.3 + s.id));
            addLight(p, Vec3f{1.0f, 0.45f, 0.15f} * float(30.0 * fire.intensity * flicker), float(250.0 + 300.0 * fire.intensity));
        }
        // Searchlights: spot light plus a visible beam in the haze.
        bool on = s.operational() && s.orders.searchlight && s.dmg.searchlights >= 0.3;
        if (on) {
            ShipPose pose = shipPose(P, s, w, nullptr, visualTime);
            double brg = s.heading + pose.searchlightTrain;
            double h = c.superstructureHeight + 2.0;
            Vec3 origin = toR(s.pos + s.forward() * (c.length * 0.12), h);
            Vec3 target;
            if (const sim::Ship* t = w.ship(s.searchlightTarget)) target = toR(t->pos, t->cls ? t->cls->superstructureHeight * 0.5 : 5.0);
            else target = origin + Vec3{std::sin(brg * kDegToRad) * 5000.0, -h * 0.6, std::cos(brg * kDegToRad) * 5000.0};
            Vec3 d = target - origin;
            double len = length(d);
            Vec3 dirD = d / std::max(1.0, len);
            Vec3f dir{float(dirD.x), float(dirD.y), float(dirD.z)};
            addLight(origin, Vec3f{1.0f, 0.97f, 0.9f} * 2500.0f, float(std::min(len * 1.3, 9000.0)), 1, dir, std::cos(float(1.6 * kDegToRad)));
            GpuParticle b{};
            b.pos = rel(origin);
            b.axis = rel(origin + dirD * std::min(len, 7000.0));
            b.size = 0.8f;
            b.color = {0.9f, 0.92f, 1.0f, 1.0f};
            b.type = float(kBeam);
            b.emissive = 10.0f;
            b.seed = float(s.id % 13);
            beams.push_back(b);
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    P.lights.clear();
    for (size_t i = 0; i < cands.size() && i < kMaxLights; ++i) P.lights.push_back(cands[i].l);

    // ---- Particles (sorted back to front; additive types are order independent but share the pass)
    P.gpuParticles.clear();
    P.gpuParticles.reserve(P.particles.size() + beams.size());
    std::vector<std::pair<float, uint32_t>> order;
    order.reserve(P.particles.size());
    for (uint32_t i = 0; i < P.particles.size(); ++i) {
        const Particle& p = P.particles[i];
        Vec3f r = rel(p.pos);
        float d2 = r.x * r.x + r.y * r.y + r.z * r.z;
        if (d2 > 30000.0f * 30000.0f) continue;
        order.push_back({d2, i});
    }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& b : beams) P.gpuParticles.push_back(b);
    // Rain curtains hanging under the squalls, seen from outside: tall veils across the cell as the
    // camera sees it (inside, the fog and the falling rain take over).
    {
        const auto& wx = w.env().weather();
        double height = 1.4 * clamp(wx.cloudBaseM > 0 ? wx.cloudBaseM : 900.0, 400.0, 1600.0);
        bool snow = snowing(w);
        std::vector<std::pair<float, GpuParticle>> veils;
        for (size_t qi = 0; qi < w.env().squalls().size(); ++qi) {
            const auto& sq = w.env().squalls()[qi];
            Vec3 c = toR(sq.pos, 0.0);
            Vec3 toCam = Vec3{camPos.x - c.x, 0.0, camPos.z - c.z};
            double d = length(toCam);
            if (d < sq.radius * 0.9 || d > 45000.0) continue;
            Vec3 fwd = toCam / d, side{fwd.z, 0.0, -fwd.x};
            for (int k = -2; k <= 2; ++k) {
                // Staggered veils, the ones in the middle in front and widest.
                double off = k * sq.radius * 0.36;
                Vec3 p = c + side * off + fwd * (sq.radius * (0.55 - 0.12 * std::abs(k)) * (1.0 - 0.15 * ((qi + k) % 2)));
                GpuParticle g{};
                g.pos = rel(p);
                g.size = float(sq.radius * (0.42 - 0.05 * std::abs(k)));
                g.stretch = float(height / g.size);
                g.color = snow ? Vec4f{0.72f, 0.75f, 0.8f, float(0.75 * sq.intensity)} : Vec4f{0.3f, 0.33f, 0.37f, float(0.85 * sq.intensity)};
                g.type = float(kCurtain);
                g.seed = float((qi * 5 + size_t(k + 2)) % 17) / 17.0f;
                g.age = 0.5f;
                Vec3f r = g.pos;
                veils.push_back({r.x * r.x + r.z * r.z, g});
            }
        }
        std::sort(veils.begin(), veils.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& v : veils) P.gpuParticles.push_back(v.second);
    }
    for (auto& [d2, i] : order) {
        const Particle& p = P.particles[i];
        GpuParticle g{};
        g.pos = rel(p.pos);
        g.size = p.size;
        g.color = p.color;
        g.axis = p.type == kTracer ? rel(p.pos - p.vel * 0.035) : p.type == kRain ? rel(p.pos - p.vel * 0.07) : rel(p.end);
        g.type = float(p.type);
        g.age = p.maxLife > 0 ? clamp(p.life / p.maxLife, 0.0f, 1.0f) : 0.5f;
        g.seed = p.seed;
        g.emissive = p.emissive;
        g.stretch = p.stretch;
        // Smoke fades in quickly and out slowly.
        if (p.type == kSmoke && p.maxLife > 0) g.color.w *= std::min(1.0f, g.age * 8.0f) * (1.0f - g.age * g.age);
        if ((p.type == kGlow || p.type == kSpark) && p.maxLife > 0) g.emissive *= (1.0f - g.age) * (1.0f - g.age);
        P.gpuParticles.push_back(g);
    }
    P.particles.erase(std::remove_if(P.particles.begin(), P.particles.end(), [](const Particle& p) { return p.maxLife < 0.0f; }),
                      P.particles.end());

    // ---- Wake map geometry (render xz, foam)
    P.wakeVerts.clear();
    auto tri = [&](const Vec2& a, float fa, const Vec2& b, float fb, const Vec2& c, float fc) {
        P.wakeVerts.insert(P.wakeVerts.end(), {float(a.x), float(a.y), fa, float(b.x), float(b.y), fb, float(c.x), float(c.y), fc});
    };
    const Vec2 camXZ{camPos.x, camPos.z};
    const double reach = kWakeHalfSize * 1.5;
    auto strip = [&](const std::vector<std::pair<Vec2, double>>& pts, double baseWidth, double spread, double foam0, double decay) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec2 &a = pts[i].first, &b = pts[i + 1].first;
            if (distance(a, camXZ) > reach && distance(b, camXZ) > reach) continue;
            double ageA = visualTime - pts[i].second, ageB = visualTime - pts[i + 1].second;
            Vec2 d = b - a;
            double l = length(d);
            if (l < 0.01) continue;
            Vec2 n{-d.y / l, d.x / l};
            double wa = baseWidth + ageA * spread, wb = baseWidth + ageB * spread;
            float fa = float(foam0 * std::exp(-ageA / decay)), fb = float(foam0 * std::exp(-ageB / decay));
            Vec2 a0 = a + n * wa, a1 = a - n * wa, b0 = b + n * wb, b1 = b - n * wb;
            tri(a0, fa * 0.6f, a, fa, b0, fb * 0.6f);
            tri(a, fa, b, fb, b0, fb * 0.6f);
            tri(a, fa, a1, fa * 0.6f, b, fb);
            tri(a1, fa * 0.6f, b1, fb * 0.6f, b, fb);
        }
    };
    std::vector<std::pair<Vec2, double>> pts;
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.cls) continue;
        auto it = P.trails.find(s.id);
        pts.clear();
        bool live = !s.dmg.sunk && s.speed > 0.3;
        if (live) pts.push_back({s.pos - s.forward() * (s.cls->length * 0.45), visualTime});
        if (it != P.trails.end())
            for (auto& p : it->second.points) pts.push_back(p);
        // Trail points are sim positions (x east, y north), i.e. render (x, z).
        double speedK = clamp(s.speed / 15.0, 0.0, 1.0);
        strip(pts, s.cls->beam * 0.45, 0.35, 0.55 + 0.45 * speedK, 45.0);
        if (live && distance(s.pos, camXZ) < reach && s.speed > 1.5) {
            // Bow wave: two diverging arms (Kelvin angle) and white water along the hull.
            Vec2 f = s.forward(), r = s.right();
            Vec2 bow = s.pos + f * (s.cls->length * 0.48);
            double armLen = s.cls->length * (0.35 + 0.45 * speedK);
            double tanK = std::tan(19.5 * kDegToRad);
            for (int side = -1; side <= 1; side += 2) {
                Vec2 end = bow - f * armLen + r * (side * armLen * tanK + side * s.cls->beam * 0.5);
                Vec2 mid = bow - f * (s.cls->length * 0.3) + r * (side * s.cls->beam * 0.55);
                Vec2 w0 = r * (side * 2.5);
                tri(bow, float(0.8 * speedK), mid + w0, float(0.5 * speedK), mid, float(0.7 * speedK));
                tri(mid, float(0.45 * speedK), mid + w0, float(0.3 * speedK), end, 0.0f);
            }
        }
    }
    for (auto& [id, tr] : P.torpedoTrails) {
        pts.assign(tr.points.begin(), tr.points.end());
        double vis = 0.6;
        for (const auto& t : w.torpedoes())
            if (t.id == id && t.def) {
                vis = t.def->wakeVisibility;
                if (t.alive) pts.insert(pts.begin(), {t.pos, visualTime});
            }
        strip(pts, 1.0, 0.25, 0.7 * vis, 20.0);
    }
    const double now = w.time();
    for (const auto& f : P.foam) {
        if (distance(f.pos, camXZ) > reach + f.radius) continue;
        double age = now - f.born;
        float k = float(f.strength * clamp(1.0 - age / f.life, 0.0, 1.0));
        double r = f.radius * (0.6 + 0.4 * std::min(1.0, age / 6.0));
        const int seg = 18;
        for (int i = 0; i < seg; ++i) {
            double a0 = kTwoPi * i / seg, a1 = kTwoPi * (i + 1) / seg;
            tri(f.pos, k, f.pos + Vec2{std::cos(a1), std::sin(a1)} * r, k * 0.3f, f.pos + Vec2{std::cos(a0), std::sin(a0)} * r, k * 0.3f);
        }
    }
}

}  // namespace it::render
