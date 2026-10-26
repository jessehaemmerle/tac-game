#pragma once
// Height field of islands and sea floor for a mission area, in the local tangent plane of the map
// centre (x east, y north). Provides land/sea queries, line of sight with earth curvature, distance
// to land (radar clutter, navigation) and coastline polylines for the tactical map.
#include <string>
#include <vector>

#include "core/Math.h"

namespace it::sim {

struct TerrainInfo {
    std::string name;
    double centerLat = 0, centerLon = 0;
    int width = 0, height = 0;  // samples
    double cellSize = 100;       // metres
    std::string attribution;
};

class Terrain {
public:
    /// Load "<base>.json" metadata and "<base>.r16" (int16 LE metres, row 0 = north edge).
    bool load(const std::string& jsonPath);
    /// Open ocean of constant depth (tests, open-sea scenarios).
    void createFlat(double sizeM, double depth = -1000.0);
    /// Programmatic height field (tests). Row 0 = north edge.
    void createFromSamples(int w, int h, double cellSize, std::vector<float> heights);

    bool empty() const { return heights_.empty(); }
    const TerrainInfo& info() const { return info_; }
    /// World bounds (x: west..east, y: south..north).
    Vec2 minCorner() const { return min_; }
    Vec2 maxCorner() const { return max_; }
    double heightAt(const Vec2& p) const;  // metres, negative below sea level; outside map = deep sea
    bool isLand(const Vec2& p) const { return heightAt(p) > 0.0; }
    /// Distance (m) from p to the nearest land, capped at `maxDist`.
    double distanceToLand(const Vec2& p, double maxDist = 20000.0) const;
    /// True if the straight line between two points (z = height above sea) clears terrain and the earth bulge.
    bool lineOfSight(const Vec3& a, const Vec3& b) const;
    /// Is there land behind `target` as seen from `observer`, within `depth` metres beyond the target?
    bool landBehind(const Vec2& observer, const Vec2& target, double depth) const;
    /// Ships drawing `draft` metres would run aground at p.
    bool shoal(const Vec2& p, double draft) const { return heightAt(p) > -draft; }

    const std::vector<std::vector<Vec2>>& coastlines() const { return coastlines_; }
    const std::vector<float>& samples() const { return heights_; }
    /// Geographic conversion helpers (equirectangular around map centre).
    Vec2 toLocal(double lat, double lon) const;
    void toGeo(const Vec2& p, double& lat, double& lon) const;

    /// Height of the earth's bulge (m) between two points separated by d metres, at the midpoint (k = 4/3).
    static double earthBulge(double d) { return d * d / (8.0 * 6371000.0 * 4.0 / 3.0); }

private:
    void buildDerived();
    double sample(int x, int y) const;

    TerrainInfo info_;
    std::vector<float> heights_;
    std::vector<float> landDistance_;  // coarse distance field (m), same grid / 4
    int ldW_ = 0, ldH_ = 0;
    double ldCell_ = 0;
    Vec2 min_, max_;
    std::vector<std::vector<Vec2>> coastlines_;
};

}  // namespace it::sim
