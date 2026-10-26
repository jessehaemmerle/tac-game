#include "procgen/ShipModel.h"

#include "procgen/ShipModelInternal.h"

namespace it::procgen {
namespace detail {

Vec3f srgb(int r, int g, int b) {
    auto lin = [](int c) {
        float v = c / 255.0f;
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return {lin(r), lin(g), lin(b)};
}

Material mat(Vec3f albedo, float rough, float metal, uint8_t id, float emissive) {
    Material m;
    m.albedo = albedo;
    m.roughness = rough;
    m.metalness = metal;
    m.id = id;
    m.emissive = emissive;
    return m;
}

namespace {

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : s) h = (h ^ ch) * 16777619u;
    return h;
}

double wlHalf(const sim::ShipClassDef& c, double u) {
    double B = c.beam * 0.5;
    double entrance = clamp(0.30 + (0.55 - c.model.fineness) * 0.6, 0.18, 0.42);
    double run = 0.36;
    if (u < entrance) {
        double k = u / entrance;
        return B * std::pow(1.0 - std::pow(1.0 - k, 2.2), 0.8);
    }
    if (u > 1.0 - run) {
        double k = (u - (1.0 - run)) / run;
        if (c.model.sternShape > 0.5) return B * (1.0 - 0.38 * k * k);
        return B * std::pow(std::max(0.0, 1.0 - std::pow(k, 2.4)), 0.55);
    }
    return B;
}

}  // namespace

Ctx::Ctx(const sim::ShipClassDef& cls, const ShipModelOptions& o)
    : c(cls),
      opt(o),
      paint(paintScheme(cls.model.hullPaint, cls.model.deckPaint, cls.type != sim::ShipType::DD && cls.type != sim::ShipType::DE)),
      L(cls.length),
      B(cls.beam),
      T(cls.draft),
      usn(cls.nation == sim::Nation::USN),
      ijn(cls.nation == sim::Nation::IJN),
      rn(cls.nation == sim::Nation::RN || cls.nation == sim::Nation::RAN),
      destroyer(cls.type == sim::ShipType::DD || cls.type == sim::ShipType::DE),
      cruiser(cls.type == sim::ShipType::CA || cls.type == sim::ShipType::CL),
      rng(fnv1a(cls.id)) {
    model.parts.reserve(4096);
    hull = part("hull", PartKind::Static, 0);
    fittings = part("fittings", PartKind::Static, 1);
    fine = part("fine", PartKind::Static, 2);
}

int Ctx::part(const std::string& name, PartKind kind, uint8_t lod, int parent) {
    if (model.parts.size() >= model.parts.capacity()) throw std::length_error("ship model: too many parts");
    model.parts.push_back({});
    MeshPart& p = model.parts.back();
    p.name = name;
    p.kind = kind;
    p.lod = lod;
    p.parent = parent;
    return int(model.parts.size()) - 1;
}

double Ctx::deckZ(double xx, double yy) const {
    double u = uOf(xx);
    double z = deck(u);
    double hw = std::max(0.5, hullHalfWidth(c, u, z));
    double r = clamp(yy / hw, -1.0, 1.0);
    return z + std::min(0.35, B * 0.015) * (1.0 - r * r);
}

double Ctx::surfaceZ(double xx, double yy, double below) const {
    double z = deckZ(xx, yy);
    for (const auto& r : roofs)
        if (xx >= r.x0 && xx <= r.x1 && yy >= r.y0 && yy <= r.y1 && r.z <= below + 1e-3) z = std::max(z, double(r.z));
    return z;
}

void Ctx::addRoof(const std::vector<Vec2f>& outline, float z) {
    Roof r{1e9f, -1e9f, 1e9f, -1e9f, z};
    for (auto& p : outline) {
        r.x0 = std::min(r.x0, p.x);
        r.x1 = std::max(r.x1, p.x);
        r.y0 = std::min(r.y0, p.y);
        r.y1 = std::max(r.y1, p.y);
    }
    roofs.push_back(r);
}

namespace {
// Fraction u at which the plating at height z lies at model x (inverts the stem rake near the bow).
double uAtX(const Ctx& cx, double xx, double z) {
    double u = cx.uOf(xx);
    if (u > 0.08) return u;
    for (int it = 0; it < 12; ++it) {
        double xr = cx.x(u) + bowRakeOffset(cx, u, z);
        u = clamp(u + (xr - xx) / cx.L, 0.0, 1.0);
    }
    return u;
}
}  // namespace

double Ctx::edgeY(double xx) const {
    double u = uAtX(*this, xx, deckX(xx));
    if (xx > x(0.0) + bowRakeOffset(*this, 0.0, deck(0.0))) return 0.0;
    return hullHalfWidth(c, u, deck(u));
}

double Ctx::sideY(double xx, double z) const {
    double u = uAtX(*this, xx, z);
    if (xx > x(0.0) + bowRakeOffset(*this, 0.0, z)) return 0.0;
    return hullHalfWidth(c, u, z);
}

bool Ctx::isFree(double xx, double yy, double r) const {
    for (const auto& f : taken)
        if (xx + r > f.x0 && xx - r < f.x1 && yy + r > f.y0 && yy - r < f.y1) return false;
    return true;
}

}  // namespace detail

using namespace detail;

PaintScheme paintScheme(const std::string& hullPaint, const std::string& deckPaint, bool plankedDeck) {
    PaintScheme p;
    Vec3f hull = srgb(110, 112, 112), bottom = srgb(110, 40, 32);
    if (hullPaint == "usn_ms21") hull = srgb(66, 74, 86);          // Navy Blue 5-N (Measure 21)
    else if (hullPaint == "usn_ms1") hull = srgb(80, 84, 88);       // Dark Gray 5-D
    else if (hullPaint == "ijn_kure_grey") hull = srgb(112, 114, 114);
    else if (hullPaint == "ijn_sasebo_grey") hull = srgb(98, 100, 98);
    else if (hullPaint == "rn_507a") hull = srgb(92, 95, 98);
    Vec3f deck = srgb(150, 120, 85);
    if (deckPaint == "usn_deck_blue") deck = srgb(56, 62, 72);  // Deck Blue 20-B
    else if (deckPaint == "ijn_linoleum") deck = srgb(104, 70, 54);
    p.hull = mat(hull, 0.62f, 0.05f, kHull);
    p.superstructure = mat(hull * 1.05f, 0.6f, 0.05f, kSuper);
    p.bottom = mat(bottom, 0.8f, 0.0f, kBottom);
    p.boot = mat(srgb(28, 28, 30), 0.7f, 0.0f, kBoot);
    // Cruisers kept their wooden planking under the Deck Blue paint; destroyers had bare steel decks.
    // The IJN laid linoleum on steel, held down by brass strips.
    uint8_t deckId = kDeck;
    if (deckPaint == "ijn_linoleum") deckId = kDeckLino;
    else if (!plankedDeck) deckId = kDeckSteel;
    p.deck = mat(deck, deckId == kDeck && deckPaint == "teak" ? 0.8f : 0.85f, 0.0f, deckId);
    // Gun barrels and fittings carry the hull paint; bare steel only where worn.
    p.metal = mat(hull * 0.82f, 0.48f, 0.35f, kMetal);
    p.glass = mat(srgb(18, 24, 30), 0.12f, 0.0f, kGlass);
    p.canvas = mat(srgb(168, 166, 156), 0.9f, 0.0f, kCanvas);
    p.lens = mat(srgb(235, 232, 210), 0.2f, 0.0f, kLens);
    p.aircraft = mat(hullPaint.rfind("ijn", 0) == 0 ? srgb(150, 155, 140) : srgb(120, 130, 145), 0.55f, 0.05f, kAircraft);
    p.linoleumStrip = mat(srgb(170, 140, 70), 0.4f, 0.7f, kStrip);
    p.wood = mat(srgb(150, 112, 72), 0.75f, 0.0f, kWood);
    p.dark = mat(srgb(10, 11, 12), 0.85f, 0.0f, kPlain);
    p.black = mat(srgb(22, 22, 23), 0.6f, 0.3f, kMetal);
    p.brass = mat(srgb(180, 140, 70), 0.35f, 0.9f, kPlain);
    p.rope = mat(srgb(40, 40, 38), 0.8f, 0.2f, kPlain);
    p.raft = mat(hullPaint.rfind("usn", 0) == 0 ? srgb(84, 90, 98) : srgb(170, 160, 130), 0.85f, 0.0f, kCanvas);
    return p;
}

double keelDepth(const sim::ShipClassDef& c, double u) {
    return c.draft * (0.15 + 0.85 * smoothstep(0.0, 0.14, u)) * (1.0 - 0.55 * smoothstep(0.82, 1.0, u));
}

double hullHalfWidth(const sim::ShipClassDef& c, double u, double z) {
    double w = wlHalf(c, u);
    double keel = keelDepth(c, u);
    double mid = 1.0 - std::fabs(u - 0.47) / 0.47;
    if (z <= 0.0) {
        double t = std::min(1.0, -z / std::max(0.1, keel));
        double m = lerp(1.6, 3.6, clamp(mid * 1.3, 0.0, 1.0));
        return w * std::pow(std::max(0.0, 1.0 - std::pow(t, m)), 1.0 / m);
    }
    double fb = std::max(0.5, c.model.deckHeightAt(u));
    double zr = clamp(z / fb, 0.0, 1.5);
    double bowness = 1.0 - smoothstep(0.0, 0.35, u);
    double y = w * (1.0 + c.model.flare * bowness * zr);
    // Flared bow: width at deck level near the stem even though the waterline narrows to nothing.
    if (u < 0.15) y += c.model.flare * c.beam * 0.3 * zr * std::sqrt(u / 0.15) * (1.0 - u / 0.15);
    y *= 1.0 - c.model.tumblehome * zr;
    return y;
}

Model buildShipModel(const sim::ShipClassDef& cls, const ShipModelOptions& opt) {
    Ctx cx(cls, opt);
    cx.model.name = cls.id;
    buildHull(cx);
    // Superstructure before the masts, sensors and weapons that stand on it; all footprints are known
    // before the deck fittings fill the free deck at the end.
    static const char* order[] = {"deckhouse", "superstructure", "hangar", "bridge", "pagoda", "funnel", "tower", "tripod", "mast",
                                  "director", "radar", "searchlight", "crane", "catapult", "aircraft", "boat", "raft", "aa"};
    for (const char* type : order)
        for (const auto& pt : cls.model.parts) {
            if (pt.type != type) continue;
            const std::string& t = pt.type;
            if (t == "deckhouse" || t == "superstructure") deckhouse(cx, pt, false);
            else if (t == "hangar") deckhouse(cx, pt, true);
            else if (t == "bridge") bridge(cx, pt);
            else if (t == "pagoda") pagoda(cx, pt);
            else if (t == "funnel") funnel(cx, pt);
            else if (t == "tower") tower(cx, pt);
            else if (t == "tripod") mast(cx, pt, true);
            else if (t == "mast") mast(cx, pt, false);
            else if (t == "director") director(cx, pt);
            else if (t == "radar" && opt.details) radar(cx, pt);
            else if (t == "searchlight") searchlight(cx, pt);
            else if (t == "crane") crane(cx, pt);
            else if (t == "catapult") catapult(cx, pt);
            else if (t == "aircraft" && opt.details) aircraft(cx, pt);
            else if (t == "boat" && opt.details) boat(cx, pt);
            else if (t == "raft" && opt.details) rafts(cx, pt);
            else if (t == "aa" && opt.details) lightAA(cx, pt);
        }
    for (size_t i = 0; i < cls.mounts.size(); ++i) buildMount(cx, i);
    for (size_t i = 0; i < cls.torpedoMounts.size(); ++i) buildTorpedoMount(cx, i);
    buildHullFittings(cx);
    buildUnderwater(cx);
    if (opt.details) {
        rigging(cx);
        depthCharges(cx);
        deckFittings(cx);
        railings(cx);
        ensign(cx);
    }
    cx.model.computeBounds();
    return std::move(cx.model);
}

}  // namespace it::procgen
