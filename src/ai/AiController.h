#pragma once
// Hierarchical AI for one side:
//   Commander  — per group, chooses a posture by utility (mission, engage, torpedo attack, withdraw ...)
//   Group      — executes the posture: formation, manoeuvre, fire discipline, target distribution
//   Captain    — per ship: weapons, illumination, torpedo release, evasion, damage response
// Every layer reasons only on the side's own picture (SideView) and reacts with human delays.
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ai/Assessment.h"
#include "ai/DecisionLog.h"
#include "ai/Doctrine.h"
#include "ai/Navigation.h"
#include "ai/SideView.h"

namespace it::ai {

enum class MissionType : uint8_t { Patrol, Attack, Transit, Escort, Picket, Hold, Intercept };
MissionType missionFromString(const std::string& s);
const char* missionKey(MissionType m);

struct Mission {
    MissionType type = MissionType::Patrol;
    std::vector<Vec2> waypoints;
    bool loop = false;
    Vec2 objective;
    double radius = 6000;
    int escortGroup = -1;
    std::vector<Vec2> exitRoute;     // route to leave the battle area
    double withdrawAtHour = -1;       // local clock hour to break off (e.g. Mikawa at Savo)
    double speedKn = 15, attackSpeedKn = 30;
};

enum class Posture : uint8_t { Mission, Investigate, Engage, TorpedoAttack, Shadow, Withdraw, Regroup };
const char* postureKey(Posture p);
enum class TorpPhase : uint8_t { Approach, Launch, TurnAway, Wait };

struct GroupBrain {
    int groupId = -1;
    Mission mission;
    const Doctrine* doctrine = nullptr;
    Posture posture = Posture::Mission;
    double postureSince = 0;
    Posture pendingPosture = Posture::Mission;
    double pendingAt = -1;  // command delay: posture takes effect at this time
    size_t waypoint = 0;
    bool missionDone = false, exited = false;
    uint32_t primary = 0;
    // torpedo attack plan
    TorpPhase phase = TorpPhase::Approach;
    double phaseSince = 0;
    uint32_t torpTarget = 0;
    double launchRange = 0;  // range at which the group turned to launch; captains fire out to it
    double impactTime = -1;
    double turnAwayCourse = 0;
    int launches = 0;
    // fire discipline & illumination
    bool holdFire = false;
    bool flaresLaunched = false;
    // target distribution: ship -> per-director track
    std::map<sim::EntityId, std::array<uint32_t, 3>> assignment;
    double nextThink = 0, nextAssign = 0;
    Assessment last;
};

struct CaptainBrain {
    sim::EntityId ship = sim::kNoEntity;
    double nextThink = 0;
    enum class Override : uint8_t { None, Evading, Retiring } override = Override::None;
    double overrideUntil = 0;
    double evadeCourse = 0;
    double zigOffset = 0, nextZig = 0;
    std::set<uint32_t> handledWarnings;
    std::map<uint32_t, double> torpedoedTracks;
    double lastLaunch = -1e9;
    double searchlightSince = -1;
    int lastHitCount = 0;
    double lastHitTime = -1e9;  // a ship being hit returns fire even while its group keeps the guns dark
};

struct AiSettings {
    double skill = 0.7;        // 0..1: scales reaction times and evasion (never information)
    bool assistMode = false;   // player side: only captains and station keeping
    bool autoEvade = true;     // assist: captains comb torpedo tracks on their own
    bool autoTorpedoes = false;
};

/// Would a torpedo fan (course, total spread) running out to its full range pass within reach of ship o,
/// given o's present course and speed?
bool torpedoFanEndangers(const Vec2& launcher, double course, double spreadDeg, double runM, double speedMs, const sim::Ship& o);

class AiController {
public:
    AiController(sim::World& w, sim::Side side, const DoctrineLibrary& lib, AiSettings settings = {});

    void setMission(int groupId, const Mission& m, const std::string& doctrine = "");
    void observe(const sim::Event& e);
    /// Call once per simulation step.
    void update();

    SideView& view() { return view_; }
    const SideView& view() const { return view_; }
    DecisionLog& log() { return log_; }
    const DecisionLog& log() const { return log_; }
    AiSettings& settings() { return settings_; }
    const GroupBrain* brain(int groupId) const;
    GroupBrain* brain(int groupId);
    const CaptainBrain* captain(sim::EntityId ship) const;
    const Doctrine& doctrineFor(const sim::Ship& s) const;

private:
    void thinkCommander(GroupBrain& g);
    void applyPosture(GroupBrain& g, Posture p, const std::vector<OptionScore>& options, int chosen);
    void runGroup(GroupBrain& g);
    void runMission(GroupBrain& g, sim::Group& grp, sim::Ship& guide);
    void runEngage(GroupBrain& g, sim::Group& grp, sim::Ship& guide);
    void runTorpedoAttack(GroupBrain& g, sim::Group& grp, sim::Ship& guide);
    void runWithdraw(GroupBrain& g, sim::Group& grp, sim::Ship& guide);
    void assignTargets(GroupBrain& g);
    void runCaptain(CaptainBrain& c);
    void captainWeapons(CaptainBrain& c, sim::Ship& s, GroupBrain* g, const Doctrine& d);
    void captainTorpedoes(CaptainBrain& c, sim::Ship& s, GroupBrain* g, const Doctrine& d);
    bool captainEvasion(CaptainBrain& c, sim::Ship& s, const Doctrine& d, double& course);
    void captainMovement(CaptainBrain& c, sim::Ship& s, GroupBrain* g, const Doctrine& d);
    double reactionTime(const sim::Ship& s, const Doctrine& d) const;
    std::vector<sim::Ship*> members(const sim::Group& grp) const;
    int slotOf(const sim::Group& grp, const sim::Ship& s) const;
    GroupBrain* brainOf(const sim::Ship& s);
    void record(DecisionLevel level, const std::string& actor, const std::string& decision, const std::string& detail,
                std::vector<std::string> args = {}, std::vector<OptionScore> alts = {}, double score = 0);
    int torpedoSetting(const sim::Ship& s, double runDistance) const;
    bool friendlyInTorpedoPath(const sim::Ship& s, double course, double spreadDeg, int setting) const;

    sim::World& w_;
    SideView view_;
    const DoctrineLibrary& lib_;
    AiSettings settings_;
    DecisionLog log_;
    std::map<int, GroupBrain> groups_;
    std::map<sim::EntityId, CaptainBrain> captains_;
};

}  // namespace it::ai
