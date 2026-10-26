#include "sim/Terrain.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"

namespace it::sim {

bool Terrain::load(const std::string& jsonPath) {
    auto j = loadJsonFile(jsonPath);
    if (!j) return false;
    info_.name = jstr(*j, "name");
    info_.centerLat = jnum(*j, "center_lat");
    info_.centerLon = jnum(*j, "center_lon");
    info_.width = jint(*j, "width");
    info_.height = jint(*j, "height");
    info_.cellSize = jnum(*j, "cell_size_m", 100);
    info_.attribution = jstr(*j, "attribution");
    std::filesystem::path p(jsonPath);
    auto raw = fs::readBinaryFile(p.parent_path() / jstr(*j, "heights", p.stem().string() + ".r16"));
    if (!raw || raw->size() < size_t(info_.width) * info_.height * 2) {
        IT_LOG_ERROR("Terrain heights missing or truncated for %s", jsonPath.c_str());
        return false;
    }
    heights_.resize(size_t(info_.width) * info_.height);
    for (size_t i = 0; i < heights_.size(); ++i) {
        int16_t v;
        std::memcpy(&v, raw->data() + i * 2, 2);
        heights_[i] = float(v);
    }
    buildDerived();
    IT_LOG_INFO("Terrain '%s': %dx%d @ %.0f m, %zu coastlines", info_.name.c_str(), info_.width, info_.height,
                info_.cellSize, coastlines_.size());
    return true;
}

void Terrain::createFlat(double sizeM, double depth) {
    int n = 16;
    info_ = {};
    info_.name = "open_sea";
    info_.width = info_.height = n;
    info_.cellSize = sizeM / (n - 1);
    heights_.assign(size_t(n) * n, float(depth));
    buildDerived();
}

void Terrain::createFromSamples(int w, int h, double cellSize, std::vector<float> heights) {
    info_ = {};
    info_.name = "custom";
    info_.width = w;
    info_.height = h;
    info_.cellSize = cellSize;
    heights_ = std::move(heights);
    buildDerived();
}

double Terrain::sample(int x, int y) const {
    x = clamp(x, 0, info_.width - 1);
    y = clamp(y, 0, info_.height - 1);
    return heights_[size_t(y) * info_.width + x];
}

double Terrain::heightAt(const Vec2& p) const {
    if (heights_.empty()) return -1000.0;
    if (p.x < min_.x || p.x > max_.x || p.y < min_.y || p.y > max_.y) return -1000.0;
    double fx = (p.x - min_.x) / info_.cellSize;
    double fy = (max_.y - p.y) / info_.cellSize;  // row 0 = north
    int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    double tx = fx - x0, ty = fy - y0;
    double a = sample(x0, y0), b = sample(x0 + 1, y0), c = sample(x0, y0 + 1), d = sample(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
}

double Terrain::distanceToLand(const Vec2& p, double maxDist) const {
    if (landDistance_.empty()) return maxDist;
    // Outside the map the distance to the edge is added to the nearest edge cell's value.
    double dx = std::max({min_.x - p.x, 0.0, p.x - max_.x}), dy = std::max({min_.y - p.y, 0.0, p.y - max_.y});
    double outside = std::sqrt(dx * dx + dy * dy);
    int x = clamp(int((p.x - min_.x) / ldCell_), 0, ldW_ - 1);
    int y = clamp(int((max_.y - p.y) / ldCell_), 0, ldH_ - 1);
    return std::min(maxDist, outside + double(landDistance_[size_t(y) * ldW_ + x]));
}

bool Terrain::lineOfSight(const Vec3& a, const Vec3& b) const {
    double d = distance(a.xy(), b.xy());
    if (d < 1.0) return true;
    // Earth curvature: the straight chord sinks below each point's local horizontal by s*(d-s)/(2Rk).
    constexpr double R = 6371000.0 * 4.0 / 3.0;
    bool anyLand = !coastlines_.empty();
    int steps = anyLand ? std::clamp(int(d / (info_.cellSize * 0.75)), 8, 600) : 24;
    for (int i = 1; i < steps; ++i) {
        double t = double(i) / steps;
        double s = t * d;
        double lineZ = lerp(a.z, b.z, t) - s * (d - s) / (2.0 * R);
        Vec2 p = lerp(a.xy(), b.xy(), t);
        double ground = anyLand ? std::max(0.0, heightAt(p)) : 0.0;
        if (lineZ < ground) return false;
    }
    return true;
}

bool Terrain::landBehind(const Vec2& observer, const Vec2& target, double depth) const {
    if (coastlines_.empty()) return false;
    Vec2 dir = normalize(target - observer);
    for (double s = 200.0; s <= depth; s += 400.0)
        if (isLand(target + dir * s)) return true;
    return false;
}

Vec2 Terrain::toLocal(double lat, double lon) const {
    return {(lon - info_.centerLon) * 111320.0 * std::cos(info_.centerLat * kDegToRad), (lat - info_.centerLat) * 110574.0};
}

void Terrain::toGeo(const Vec2& p, double& lat, double& lon) const {
    lat = info_.centerLat + p.y / 110574.0;
    lon = info_.centerLon + p.x / (111320.0 * std::cos(info_.centerLat * kDegToRad));
}

void Terrain::buildDerived() {
    double w = (info_.width - 1) * info_.cellSize, h = (info_.height - 1) * info_.cellSize;
    min_ = {-w * 0.5, -h * 0.5};
    max_ = {w * 0.5, h * 0.5};

    // --- Coastlines: marching squares on the 0 m contour, segments chained into polylines.
    coastlines_.clear();
    struct Key {
        int64_t x, y;
        bool operator<(const Key& o) const { return x != o.x ? x < o.x : y < o.y; }
    };
    auto quant = [](const Vec2& p) { return Key{int64_t(std::llround(p.x * 4)), int64_t(std::llround(p.y * 4))}; };
    std::vector<std::pair<Vec2, Vec2>> segs;
    auto pos = [&](double gx, double gy) { return Vec2{min_.x + gx * info_.cellSize, max_.y - gy * info_.cellSize}; };
    for (int y = 0; y + 1 < info_.height; ++y)
        for (int x = 0; x + 1 < info_.width; ++x) {
            double v[4] = {sample(x, y), sample(x + 1, y), sample(x + 1, y + 1), sample(x, y + 1)};
            int code = (v[0] > 0) | ((v[1] > 0) << 1) | ((v[2] > 0) << 2) | ((v[3] > 0) << 3);
            if (code == 0 || code == 15) continue;
            auto edge = [&](int e) {
                static const int a[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
                static const double cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
                int i0 = a[e][0], i1 = a[e][1];
                double t = v[i0] / (v[i0] - v[i1]);
                return pos(x + lerp(cx[i0], cx[i1], t), y + lerp(cy[i0], cy[i1], t));
            };
            static const int table[16][4] = {{-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1},
                                             {1, 2, -1, -1},   {3, 0, 1, 2},   {0, 2, -1, -1}, {3, 2, -1, -1},
                                             {2, 3, -1, -1},   {2, 0, -1, -1}, {0, 1, 2, 3},   {2, 1, -1, -1},
                                             {1, 3, -1, -1},   {1, 0, -1, -1}, {0, 3, -1, -1}, {-1, -1, -1, -1}};
            for (int k = 0; k < 4 && table[code][k] >= 0; k += 2) segs.push_back({edge(table[code][k]), edge(table[code][k + 1])});
        }
    std::multimap<Key, size_t> byPoint;
    for (size_t i = 0; i < segs.size(); ++i) {
        byPoint.insert({quant(segs[i].first), i});
        byPoint.insert({quant(segs[i].second), i});
    }
    std::vector<bool> used(segs.size(), false);
    auto takeNext = [&](const Vec2& p) -> std::optional<Vec2> {
        auto range = byPoint.equal_range(quant(p));
        for (auto it = range.first; it != range.second; ++it) {
            size_t s = it->second;
            if (used[s]) continue;
            used[s] = true;
            return quant(segs[s].first) < quant(p) || quant(p) < quant(segs[s].first) ? segs[s].first : segs[s].second;
        }
        return std::nullopt;
    };
    for (size_t i = 0; i < segs.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        std::vector<Vec2> line{segs[i].first, segs[i].second};
        while (auto n = takeNext(line.back())) line.push_back(*n);
        std::reverse(line.begin(), line.end());
        while (auto n = takeNext(line.back())) line.push_back(*n);
        if (line.size() >= 4) coastlines_.push_back(std::move(line));
    }

    // --- Coarse distance-to-land field (two-pass chamfer transform).
    int f = std::max(1, int(std::ceil(250.0 / info_.cellSize)));
    ldCell_ = info_.cellSize * f;
    ldW_ = std::max(1, info_.width / f);
    ldH_ = std::max(1, info_.height / f);
    const float inf = 1e9f;
    landDistance_.assign(size_t(ldW_) * ldH_, inf);
    for (int y = 0; y < ldH_; ++y)
        for (int x = 0; x < ldW_; ++x) {
            bool land = false;
            for (int dy = 0; dy < f && !land; ++dy)
                for (int dx = 0; dx < f && !land; ++dx) land = sample(x * f + dx, y * f + dy) > 0;
            if (land) landDistance_[size_t(y) * ldW_ + x] = 0.0f;
        }
    const float c1 = float(ldCell_), c2 = float(ldCell_ * std::sqrt(2.0));
    auto at = [&](int x, int y) -> float& { return landDistance_[size_t(y) * ldW_ + x]; };
    for (int y = 0; y < ldH_; ++y)
        for (int x = 0; x < ldW_; ++x) {
            float& d = at(x, y);
            if (x > 0) d = std::min(d, at(x - 1, y) + c1);
            if (y > 0) d = std::min(d, at(x, y - 1) + c1);
            if (x > 0 && y > 0) d = std::min(d, at(x - 1, y - 1) + c2);
            if (x + 1 < ldW_ && y > 0) d = std::min(d, at(x + 1, y - 1) + c2);
        }
    for (int y = ldH_ - 1; y >= 0; --y)
        for (int x = ldW_ - 1; x >= 0; --x) {
            float& d = at(x, y);
            if (x + 1 < ldW_) d = std::min(d, at(x + 1, y) + c1);
            if (y + 1 < ldH_) d = std::min(d, at(x, y + 1) + c1);
            if (x + 1 < ldW_ && y + 1 < ldH_) d = std::min(d, at(x + 1, y + 1) + c2);
            if (x > 0 && y + 1 < ldH_) d = std::min(d, at(x - 1, y + 1) + c2);
        }
}

}  // namespace it::sim
