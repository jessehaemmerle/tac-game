#include "game/Scenario.h"

#include <algorithm>
#include <tuple>

#include "core/FileSystem.h"
#include "sim/Terrain.h"

namespace it::game {
namespace {

Vec2 readPoint(const Json& j, const sim::Terrain* terrain, double lat0, double lon0) {
    if (j.is_array() && j.size() >= 2) return {j[0].get<double>(), j[1].get<double>()};
    if (j.is_object()) {
        if (j.contains("lat")) {
            double lat = jnum(j, "lat"), lon = jnum(j, "lon");
            if (terrain && !terrain->empty() && terrain->info().centerLat != 0) return terrain->toLocal(lat, lon);
            return {(lon - lon0) * 111320.0 * std::cos(lat0 * kDegToRad), (lat - lat0) * 110574.0};
        }
        return {jnum(j, "x"), jnum(j, "y")};
    }
    return {};
}

std::vector<Vec2> readPoints(const Json& arr, const sim::Terrain* t, double lat0, double lon0) {
    std::vector<Vec2> out;
    if (arr.is_array())
        for (auto& p : arr) out.push_back(readPoint(p, t, lat0, lon0));
    return out;
}

Json point(const Vec2& p) { return Json::array({std::round(p.x), std::round(p.y)}); }

ObjectiveType objectiveFromString(const std::string& s) {
    if (s == "destroy_group") return ObjectiveType::DestroyGroup;
    if (s == "protect_group") return ObjectiveType::ProtectGroup;
    if (s == "preserve") return ObjectiveType::Preserve;
    if (s == "exit_area") return ObjectiveType::ExitArea;
    if (s == "survive") return ObjectiveType::Survive;
    return ObjectiveType::DestroyTonnage;
}

const char* objectiveId(ObjectiveType t) {
    static const char* k[] = {"destroy_tonnage", "destroy_group", "protect_group", "preserve", "exit_area", "survive"};
    return k[int(t)];
}

const char* sideId(sim::Side s) { return s == sim::Side::Allies ? "allies" : (s == sim::Side::Axis ? "axis" : "neutral"); }

}  // namespace

const ScenarioGroup* Scenario::group(const std::string& gid) const {
    for (auto& g : groups)
        if (g.id == gid) return &g;
    return nullptr;
}

bool loadScenario(const std::string& path, Scenario& sc, std::string* error) {
    auto jo = loadJsonFile(path, error);
    if (!jo) return false;
    const Json& j = *jo;
    sc = {};
    sc.sourceFile = path;
    sc.id = jstr(j, "id");
    sc.title = jstr(j, "title", "scenario." + sc.id + ".title");
    sc.hidden = jbool(j, "hidden", false);
    sc.briefing = jstr(j, "briefing", "scenario." + sc.id + ".briefing");
    sc.history = jstr(j, "history", "scenario." + sc.id + ".history");
    sc.theater = jstr(j, "theater", "pacific");
    const Json& loc = jobj(j, "location");
    sc.lat = jnum(loc, "lat", sc.lat);
    sc.lon = jnum(loc, "lon", sc.lon);
    sc.terrain = jstr(j, "terrain");
    sim::Environment::parse(j, sc.date, sc.weather);
    sc.timeLimit = jnum(j, "time_limit_s", 7200);
    if (j.contains("player_sides")) {
        sc.playableSides.clear();
        for (auto& s : j["player_sides"]) sc.playableSides.push_back(sim::sideFromString(s.get<std::string>()));
    }
    sc.defaultPlayer = sim::sideFromString(jstr(j, "default_player", "allies"));

    sim::Terrain terrain;
    const sim::Terrain* tp = nullptr;
    if (!sc.terrain.empty() && terrain.load(fs::dataPath(sc.terrain).string())) tp = &terrain;

    for (auto& gj : jarr(j, "groups")) {
        ScenarioGroup g;
        g.id = jstr(gj, "id");
        g.name = jstr(gj, "name", g.id);
        g.side = sim::sideFromString(jstr(gj, "side", "allies"));
        g.formation = sim::formationFromString(jstr(gj, "formation", "column"));
        g.spacing = jnum(gj, "spacing", 900);
        g.doctrine = jstr(gj, "doctrine");
        if (gj.contains("position")) {
            const Json& p = gj["position"];
            g.geo = p.is_object() && p.contains("lat");
            if (g.geo) {
                g.lat = jnum(p, "lat");
                g.lon = jnum(p, "lon");
            }
            g.pos = readPoint(p, tp, sc.lat, sc.lon);
        }
        g.course = jnum(gj, "course", 0);
        g.speedKn = jnum(gj, "speed_kn", 15);
        g.crew = sim::crewFromString(jstr(gj, "crew", "regular"));
        g.readiness = jnum(gj, "readiness", 1.0);
        g.playable = jbool(gj, "playable", true);
        g.appearTime = jnum(gj, "appear_s", 0);
        for (auto& sj : jarr(gj, "ships")) {
            ScenarioShip s;
            s.cls = jstr(sj, "class");
            s.name = jstr(sj, "name");
            if (sj.contains("crew")) s.crew = sim::crewFromString(jstr(sj, "crew"));
            for (auto& sen : jarr(sj, "sensors")) s.sensors.push_back(sen.get<std::string>());
            s.fcRadar = jbool(sj, "fc_radar", true);
            g.ships.push_back(s);
        }
        const Json& mj = jobj(gj, "mission");
        g.mission.type = ai::missionFromString(jstr(mj, "type", "patrol"));
        g.mission.waypoints = readPoints(jarr(mj, "waypoints"), tp, sc.lat, sc.lon);
        g.mission.loop = jbool(mj, "loop", g.mission.type == ai::MissionType::Patrol || g.mission.type == ai::MissionType::Picket);
        if (mj.contains("objective")) g.mission.objective = readPoint(mj["objective"], tp, sc.lat, sc.lon);
        g.mission.radius = jnum(mj, "radius", 6000);
        g.mission.exitRoute = readPoints(jarr(mj, "exit"), tp, sc.lat, sc.lon);
        g.mission.withdrawAtHour = jnum(mj, "withdraw_at_hour", -1);
        g.mission.speedKn = jnum(mj, "speed_kn", g.speedKn);
        g.mission.attackSpeedKn = jnum(mj, "attack_speed_kn", std::max(g.speedKn, 28.0));
        std::string esc = jstr(mj, "escort");
        if (!esc.empty())
            for (size_t k = 0; k < sc.groups.size(); ++k)
                if (sc.groups[k].id == esc) g.mission.escortGroup = int(k);
        sc.groups.push_back(g);
    }
    for (auto& oj : jarr(j, "objectives")) {
        Objective o;
        o.id = jstr(oj, "id");
        o.side = sim::sideFromString(jstr(oj, "side", "allies"));
        o.type = objectiveFromString(jstr(oj, "type"));
        o.value = jnum(oj, "value", 0);
        o.group = jstr(oj, "group");
        if (oj.contains("area")) o.area = readPoint(oj["area"], tp, sc.lat, sc.lon);
        o.radius = jnum(oj, "radius", 5000);
        o.points = jnum(oj, "points", 100);
        o.text = jstr(oj, "text", "objective." + sc.id + "." + o.id);
        sc.objectives.push_back(o);
    }
    return true;
}

Json scenarioToJson(const Scenario& sc) {
    Json j;
    j["id"] = sc.id;
    j["title"] = sc.title;
    if (sc.hidden) j["hidden"] = true;
    j["briefing"] = sc.briefing;
    j["history"] = sc.history;
    j["theater"] = sc.theater;
    j["location"] = {{"lat", sc.lat}, {"lon", sc.lon}};
    if (!sc.terrain.empty()) j["terrain"] = sc.terrain;
    j["date"] = {{"year", sc.date.year}, {"month", sc.date.month}, {"day", sc.date.day}, {"hour", sc.date.hour}, {"utc_offset", sc.date.utcOffset}};
    j["weather"] = {{"cloud_cover", sc.weather.cloudCover}, {"visibility_km", sc.weather.visibilityKm}, {"sea_state", sc.weather.seaState},
                    {"wind_dir", sc.weather.windDirDeg},    {"wind_kn", sc.weather.windSpeedKn},       {"squalls", sc.weather.squalls},
                    {"squall_intensity", sc.weather.squallIntensity}};
    j["time_limit_s"] = sc.timeLimit;
    Json ps = Json::array();
    for (auto s : sc.playableSides) ps.push_back(sideId(s));
    j["player_sides"] = ps;
    j["default_player"] = sideId(sc.defaultPlayer);
    Json groups = Json::array();
    for (auto& g : sc.groups) {
        Json gj;
        gj["id"] = g.id;
        gj["name"] = g.name;
        gj["side"] = sideId(g.side);
        gj["formation"] = std::string(sim::formationKey(g.formation)).substr(10);
        gj["spacing"] = g.spacing;
        if (!g.doctrine.empty()) gj["doctrine"] = g.doctrine;
        gj["position"] = point(g.pos);
        gj["course"] = g.course;
        gj["speed_kn"] = g.speedKn;
        static const char* crews[] = {"green", "regular", "veteran", "elite"};
        gj["crew"] = crews[int(g.crew)];
        if (g.readiness < 1.0) gj["readiness"] = g.readiness;
        if (!g.playable) gj["playable"] = false;
        if (g.appearTime > 0) gj["appear_s"] = g.appearTime;
        Json ships = Json::array();
        for (auto& s : g.ships) {
            Json sj{{"class", s.cls}, {"name", s.name}};
            if (s.crew) sj["crew"] = crews[int(*s.crew)];
            if (!s.sensors.empty()) sj["sensors"] = s.sensors;
            if (!s.fcRadar) sj["fc_radar"] = false;
            ships.push_back(sj);
        }
        gj["ships"] = ships;
        static const char* mt[] = {"patrol", "attack", "transit", "escort", "picket", "hold", "intercept"};
        Json mj;
        mj["type"] = mt[int(g.mission.type)];
        Json wps = Json::array();
        for (auto& p : g.mission.waypoints) wps.push_back(point(p));
        mj["waypoints"] = wps;
        mj["loop"] = g.mission.loop;
        mj["objective"] = point(g.mission.objective);
        mj["radius"] = g.mission.radius;
        Json ex = Json::array();
        for (auto& p : g.mission.exitRoute) ex.push_back(point(p));
        mj["exit"] = ex;
        if (g.mission.withdrawAtHour >= 0) mj["withdraw_at_hour"] = g.mission.withdrawAtHour;
        mj["speed_kn"] = g.mission.speedKn;
        mj["attack_speed_kn"] = g.mission.attackSpeedKn;
        if (g.mission.escortGroup >= 0 && g.mission.escortGroup < int(sc.groups.size())) mj["escort"] = sc.groups[size_t(g.mission.escortGroup)].id;
        gj["mission"] = mj;
        groups.push_back(gj);
    }
    j["groups"] = groups;
    Json objs = Json::array();
    for (auto& o : sc.objectives) {
        Json oj{{"id", o.id}, {"side", sideId(o.side)}, {"type", objectiveId(o.type)}, {"value", o.value}, {"points", o.points}, {"text", o.text}};
        if (!o.group.empty()) oj["group"] = o.group;
        if (o.type == ObjectiveType::ExitArea) {
            oj["area"] = point(o.area);
            oj["radius"] = o.radius;
        }
        objs.push_back(oj);
    }
    j["objectives"] = objs;
    return j;
}

bool saveScenario(const std::string& path, const Scenario& sc) { return saveJsonFile(path, scenarioToJson(sc)); }

std::vector<Scenario> listScenarios(const std::string& dir) {
    std::vector<Scenario> out;
    for (auto& p : fs::listFiles(dir, ".json")) {
        Scenario sc;
        if (loadScenario(p.string(), sc)) out.push_back(std::move(sc));
    }
    std::sort(out.begin(), out.end(), [](const Scenario& a, const Scenario& b) {
        return std::tie(a.date.year, a.date.month, a.date.day) < std::tie(b.date.year, b.date.month, b.date.day);
    });
    return out;
}

}  // namespace it::game
