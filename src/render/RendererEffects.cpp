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
            ShipPose pose = shipPose(s, w, visualTime);
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
    for (auto& [d2, i] : order) {
        const Particle& p = P.particles[i];
        GpuParticle g{};
        g.pos = rel(p.pos);
        g.size = p.size;
        g.color = p.color;
        g.axis = rel(p.end);
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
            double armLen = s.cls->length * (0.5 + speedK);
            double tanK = std::tan(19.5 * kDegToRad);
            for (int side = -1; side <= 1; side += 2) {
                Vec2 end = bow - f * armLen + r * (side * armLen * tanK + side * s.cls->beam * 0.5);
                Vec2 mid = bow - f * (s.cls->length * 0.3) + r * (side * s.cls->beam * 0.55);
                Vec2 w0 = r * (side * 2.5);
                tri(bow, float(0.9 * speedK), mid + w0, float(0.7 * speedK), mid, float(0.8 * speedK));
                tri(mid, float(0.8 * speedK), mid + w0, float(0.6 * speedK), end, 0.0f);
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
