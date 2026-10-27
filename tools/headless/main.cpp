// Headless battle runner: plays a scenario AI-vs-AI (or with a passive player side) many times and
// prints statistics and the AI decision log. Used to tune doctrine and to check historical plausibility.
//
//   irontide_headless --scenario savo_island --runs 20 --seed 1 [--log] [--events] [--trace 60] [--skill 0.7]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "ai/Doctrine.h"
#include "core/FileSystem.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "game/Session.h"

using namespace it;

namespace {
const char* arg(int argc, char** argv, const char* name, const char* def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path exe = std::filesystem::absolute(argv[0]).parent_path();
    if (const char* d = arg(argc, argv, "--data", nullptr))
        fs::setDataRoot(d);
    else if (!fs::locateDataRoot(exe)) {
        std::fprintf(stderr, "data directory not found (use --data)\n");
        return 1;
    }
    setLogLevel(flag(argc, argv, "--verbose") ? LogLevel::Info : LogLevel::Warn);
    loc::load("en");
    sim::Database db;
    std::string err;
    if (!db.loadFromDirectory(fs::dataPath("db").string(), &err)) {
        std::fprintf(stderr, "database: %s\n", err.c_str());
        return 1;
    }
    ai::DoctrineLibrary lib;
    lib.load(fs::dataPath("doctrine").string());
    std::string scen = arg(argc, argv, "--scenario", "savo_island");
    std::string path = scen.find(".json") != std::string::npos ? scen : fs::dataPath("scenarios/" + scen + ".json").string();
    game::Scenario sc;
    if (!game::loadScenario(path, sc, &err)) {
        std::fprintf(stderr, "scenario: %s\n", err.c_str());
        return 1;
    }
    int runs = std::atoi(arg(argc, argv, "--runs", "10"));
    uint64_t seed = std::strtoull(arg(argc, argv, "--seed", "1"), nullptr, 10);
    double skill = std::atof(arg(argc, argv, "--skill", "0.7"));
    double trace = std::atof(arg(argc, argv, "--trace", "0"));
    bool showLog = flag(argc, argv, "--log");
    bool showEvents = flag(argc, argv, "--events");  // contacts, first salvo per ship, torpedoes, hits

    struct Agg {
        double lost = 0, sunk = 0, points = 0;
        int ships = 0;
        std::map<std::string, int> sunkShips;
    } agg[2];
    std::map<std::string, int> decisions[2], objectivesMet[2];
    double totalTime = 0;
    int torpHits[2] = {0, 0}, torpFired[2] = {0, 0}, shellHits[2] = {0, 0}, shells[2] = {0, 0};
    // Main-battery gunnery (calibre >= 200 mm) and the range of its hits, kept apart from the light guns.
    int heavyFired[2] = {0, 0}, heavyHits[2] = {0, 0};
    double heavyHitRange[2] = {0, 0};

    for (int r = 0; r < runs; ++r) {
        game::Session session(db, lib);
        game::SessionOptions opt;
        opt.player = sim::Side::Neutral;
        opt.seed = seed + uint64_t(r);
        opt.aiSkill = skill;
        if (!session.start(sc, opt, &err)) {
            std::fprintf(stderr, "start: %s\n", err.c_str());
            return 1;
        }
        double nextTrace = 0;
        std::set<sim::EntityId> openedFire;
        while (!session.finished()) {
            session.step();
            auto events = session.drainEvents();
            auto& w = session.world();
            if (trace > 0 && w.time() >= nextTrace) {
                nextTrace += trace;
                std::printf("  t=%5.0fs  %s\n", w.time(), "");
                for (auto& s : w.ships()) {
                    if (!s->alive()) continue;
                    double lat, lon;
                    w.terrain().toGeo(s->pos, lat, lon);
                    std::printf("     %-16s %-5s %7.3f %7.3f hdg %3.0f %4.1fkn flot %.2f hull %.2f prop %.2f power %.2f fires %zu (%.1f) hits %d ord %3.0f/%4.1f %s\n",
                                s->name.c_str(), s->side == sim::Side::Allies ? "ALL" : "AXIS", lat, lon, s->heading, s->speedKn(), s->flotation(),
                                s->dmg.hullIntegrity, s->dmg.propulsion, s->dmg.power, s->dmg.fires.size(), s->dmg.totalFireIntensity(),
                                s->dmg.hitsTaken, s->orders.course, s->orders.speedKn, s->dmg.sinking ? "SINKING" : "");
                }
            }
            for (auto& e : events) {
                if (int si = sim::sideIndex(e.side); si < 2 && e.value >= 200.0) {
                    if (e.type == sim::EventType::GunFired) {
                        const sim::Ship* f = w.ship(e.a);
                        size_t mi = size_t(e.value2);
                        heavyFired[si] += f && mi < f->mounts.size() ? f->mounts[mi].def->barrels : 1;
                    } else if (e.type == sim::EventType::ShellHit) {
                        const sim::Ship *f = w.ship(e.a), *t = w.ship(e.b);
                        ++heavyHits[si];
                        if (f && t) heavyHitRange[si] += distance(f->pos, t->pos);
                    }
                }
                if (showEvents) {
                    auto name = [&](sim::EntityId id) { const sim::Ship* s = w.ship(id); return s ? s->name.c_str() : "?"; };
                    const char* side = e.side == sim::Side::Allies ? "ALL" : "AXIS";
                    switch (e.type) {
                        case sim::EventType::ContactNew:
                            std::printf("  [%5.0fs] %s new contact T%u%s\n", e.time, side, e.track, e.value2 > 0.5 ? " (radar)" : "");
                            break;
                        case sim::EventType::ContactIdentified: std::printf("  [%5.0fs] %s identified T%u as %s\n", e.time, side, e.track, e.text.c_str()); break;
                        case sim::EventType::GunFired:
                            if (openedFire.insert(e.a).second) std::printf("  [%5.0fs] %s opens fire\n", e.time, name(e.a));
                            break;
                        case sim::EventType::TorpedoLaunched: std::printf("  [%5.0fs] %s launches %.0f torpedoes\n", e.time, name(e.a), e.value); break;
                        case sim::EventType::TorpedoHit: std::printf("  [%5.0fs] torpedo from %s hits %s\n", e.time, name(e.a), name(e.b)); break;
                        case sim::EventType::ShellHit: {
                            const sim::Ship *f = w.ship(e.a), *t = w.ship(e.b);
                            double range = f && t ? distance(f->pos, t->pos) : 0.0;
                            double land = t ? w.terrain().distanceToLand(t->pos, 20000.0) : 0.0;
                            std::printf("  [%5.0fs] %s hits %s (%.0f mm%s) at %.1f km, target %.1f km off land, light %.2f, fire %.2f\n", e.time, name(e.a),
                                        name(e.b), e.value, e.value2 > 0.5 ? ", pen" : "", range / 1000.0, land / 1000.0, t ? w.illuminationOn(*t) : 0.0,
                                        t ? t->dmg.totalFireIntensity() : 0.0);
                            break;
                        }
                        default: break;
                    }
                }
                if (e.type == sim::EventType::ShipSunk || e.type == sim::EventType::ShipCrippled) {
                    if (const sim::Ship* s = w.ship(e.a); s && e.type == sim::EventType::ShipCrippled && runs <= 3)
                        std::printf("  [%5.0fs] %s out of action (%s)\n", e.time, s->name.c_str(), e.text.c_str());
                }
            }
        }
        auto& w = session.world();
        totalTime += w.time();
        for (int si = 0; si < 2; ++si) {
            auto res = session.result(sim::Side(si));
            agg[si].lost += res.tonnageLost;
            agg[si].sunk += res.tonnageSunk;
            agg[si].points += res.points;
            if (auto* a = session.ai(sim::Side(si)))
                for (auto& d : a->log().records()) decisions[si][d.decision]++;
        }
        for (auto& s : w.ships()) {
            int si = sim::sideIndex(s->side);
            if (si > 1) continue;
            torpFired[si] += s->stats.torpedoesFired;
            torpHits[si] += s->stats.torpedoHits;
            shells[si] += s->stats.shellsFired;
            shellHits[si] += s->stats.hitsScored;
            if (s->dmg.sinking || s->dmg.sunk) agg[si].sunkShips[s->name]++;
        }
        auto ra = session.result(sim::Side::Allies), rx = session.result(sim::Side::Axis);
        std::printf("run %2d seed %llu: %5.0f s (%s) | Allies lost %2d ships %6.0f t | Axis lost %2d ships %6.0f t\n", r,
                    (unsigned long long)opt.seed, w.time(), session.endReason().c_str(), ra.shipsLost, ra.tonnageLost, rx.shipsLost,
                    rx.tonnageLost);
        for (auto* res : {&ra, &rx})
            for (auto& o : res->objectivesMet) objectivesMet[res == &ra ? 0 : 1][o]++;
        if (showLog)
            for (int si = 0; si < 2; ++si)
                if (auto* a = session.ai(sim::Side(si)))
                    for (auto& d : a->log().records()) {
                        std::printf("    %6.0fs %-5s %-9s %-14s %s", d.time, si == 0 ? "ALL" : "AXIS",
                                    d.level == ai::DecisionLevel::Commander ? "commander" : (d.level == ai::DecisionLevel::Group ? "group" : "captain"),
                                    d.actor.c_str(), d.decision.c_str());
                        if (!d.detail.empty()) std::printf(" [%s]", d.detail.c_str());
                        if (d.level == ai::DecisionLevel::Commander) {
                            std::printf("  {");
                            for (auto& o : d.alternatives) std::printf(" %s=%.2f", o.option.c_str() + 5, o.score);
                            std::printf(" }");
                        }
                        std::printf("\n");
                    }
    }
    std::printf("\n=== %s: %d runs, avg %.0f s ===\n", sc.id.c_str(), runs, totalTime / runs);
    const char* names[2] = {"Allies", "Axis"};
    for (int si = 0; si < 2; ++si) {
        std::printf("%-6s avg tonnage lost %7.0f, sunk %7.0f, points %6.1f | torpedoes %d fired / %d hits | shells %d / %d hits (%.1f%%)\n",
                    names[si], agg[si].lost / runs, agg[si].sunk / runs, agg[si].points / runs, torpFired[si], torpHits[si], shells[si],
                    shellHits[si], shells[si] ? 100.0 * shellHits[si] / shells[si] : 0.0);
        if (heavyFired[si])
            std::printf("       heavy guns: %d fired / %d hits (%.1f%%), mean hit range %.1f km\n", heavyFired[si], heavyHits[si],
                        100.0 * heavyHits[si] / heavyFired[si], heavyHits[si] ? heavyHitRange[si] / heavyHits[si] / 1000.0 : 0.0);
        std::printf("       objectives met (runs):");
        for (auto& [o, c] : objectivesMet[si]) std::printf(" %s:%d", o.c_str() + o.rfind('.') + 1, c);
        std::printf("\n       ships lost (count over runs):");
        for (auto& [n, c] : agg[si].sunkShips) std::printf(" %s:%d", n.c_str(), c);
        std::printf("\n       decisions:");
        for (auto& [k, c] : decisions[si]) std::printf(" %s:%d", k.c_str(), c);
        std::printf("\n");
    }
    return 0;
}
