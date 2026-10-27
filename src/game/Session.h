#pragma once
// A running battle: world + AI for both sides + player commands + objectives and scoring.
// Runs headless (tools, tests) or under the game's main loop with time compression.
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ai/AiController.h"
#include "game/Scenario.h"
#include "sim/World.h"

namespace it::game {

struct SessionOptions {
    sim::Side player = sim::Side::Neutral;  // Neutral: AI vs AI
    double aiSkill = 0.7;                   // enemy AI skill (reaction, evasion) — never extra information
    int enemyCrewShift = 0;                 // -1..+1 crew quality shift for the AI side (difficulty)
    uint64_t seed = 1;
    bool autoEvade = true;                  // player ships comb torpedo tracks on their own
};

struct SideResult {
    int shipsLost = 0, shipsSunkOfEnemy = 0;
    int aircraftLost = 0, aircraftDowned = 0;   // own aircraft lost; enemy aircraft shot down
    double tonnageLost = 0, tonnageSunk = 0;
    double damageTaken = 0;  // tonnage-weighted damage fraction
    double points = 0;
    std::vector<std::string> objectivesMet, objectivesFailed;
};

class Session {
public:
    Session(const sim::Database& db, const ai::DoctrineLibrary& lib);
    bool start(const Scenario& sc, const SessionOptions& opt, std::string* error = nullptr);

    /// Advance by real time (applies pause / time compression). Returns simulated steps executed.
    int update(double realDt);
    void step();

    sim::World& world() { return *world_; }
    const sim::World& world() const { return *world_; }
    ai::AiController* ai(sim::Side s) { return ai_[sim::sideIndex(s)].get(); }
    const Scenario& scenario() const { return scenario_; }
    const SessionOptions& options() const { return options_; }
    sim::Side player() const { return options_.player; }

    // Time control
    double timeScale = 1.0;
    bool paused = false;

    /// Events produced since the last call (presentation layers drain this every frame).
    std::vector<sim::Event> drainEvents();

    // Outcome
    bool finished() const { return finished_; }
    const std::string& endReason() const { return endReason_; }
    SideResult result(sim::Side s) const;
    /// Groups of the scenario mapped to world groups.
    int worldGroup(const std::string& scenarioGroupId) const;
    /// Air groups of the scenario mapped to world flights (-1: not yet in play).
    int worldFlight(const std::string& scenarioAirGroupId) const;

    // ---------------------------------------------------------------- player commands
    void orderGroupCourse(int group, double course, double speedKn);
    void orderGroupFormation(int group, sim::Formation f, double spacing = -1);
    void orderShipCourse(sim::EntityId ship, double course, double speedKn);
    void rejoinGroup(sim::EntityId ship);
    void orderTarget(sim::EntityId ship, uint32_t track);
    void orderWeapons(sim::EntityId ship, sim::WeaponsStatus ws);
    int orderTorpedoes(sim::EntityId ship, uint32_t track, int count, double spreadDeg, std::string* why = nullptr);
    void orderSearchlight(sim::EntityId ship, bool on, uint32_t track);
    void orderStarshells(sim::EntityId ship, bool on);
    bool orderFloatplane(sim::EntityId ship, bool flares, const Vec2& target);
    /// Waypoints for a group's guide or a detached ship: replace the route or append to it.
    void orderGroupRoute(int group, const std::vector<Vec2>& points, bool append);
    void orderShipRoute(sim::EntityId ship, const std::vector<Vec2>& points, bool append);
    /// Hand a group to its AI commander (true) or take it back under the player's command (false).
    void delegateGroup(int group, bool toAi);
    /// Order a player flight (launches it if at its base).
    bool orderFlight(int flight, const sim::AirOrder& o, std::string* why = nullptr);
    /// Take an own flight under the player's command (true) or leave it to the side's AI (false).
    void commandFlight(int flight, bool player);
    sim::Group* playerGroup(int group);
    sim::Flight* playerFlight(int flight);
    sim::Ship* playerShip(sim::EntityId id);

private:
    void spawnGroup(size_t index);
    void spawnAirGroup(size_t index);
    void checkEnd();
    void trackObjectives(double dt);
    /// Steer player groups and detached ships along their waypoints.
    void steerRoutes();
    void reportRouteDone(sim::EntityId ship, const std::string& name);
    /// The bridge repeats a player order (voice reply, engine telegraph).
    void acknowledge(sim::EntityId ship, const char* reply);

    const sim::Database& db_;
    const ai::DoctrineLibrary& lib_;
    Scenario scenario_;
    SessionOptions options_;
    std::unique_ptr<sim::World> world_;
    std::array<std::unique_ptr<ai::AiController>, sim::kSideCount> ai_;
    std::vector<int> groupMap_;  // scenario group index -> world group id (-1 = not yet spawned)
    std::vector<int> airBaseMap_;  // scenario air base index -> world base id
    std::vector<int> flightMap_;   // scenario air group index -> world flight id (-1 = not yet in play)
    std::vector<sim::Event> pending_;
    std::vector<std::unique_ptr<sim::SensorMountDef>> sensorPool_;
    double accumulator_ = 0;
    double quietSince_ = 0;
    std::vector<double> objectiveTime_;  // seconds spent in the area, per (bombardment) objective
    bool finished_ = false;
    std::string endReason_;
};

}  // namespace it::game
