#pragma once
// The contact picture of one side: everything that side believes about enemy ships, built only
// from its own sensors (lookouts, radar, gun flashes, aircraft, radio reports). AI and the player's
// tactical map read exclusively from here — this is what makes the AI "fair".
#include <string>
#include <vector>

#include "core/Math.h"
#include "sim/Types.h"

namespace it::sim {

enum class ClassLevel : uint8_t { Detected = 0, SizeKnown = 1, TypeKnown = 2, ClassKnown = 3, Identified = 4 };

enum SensorBits : uint32_t {
    kSenseVisual = 1u << 0,
    kSenseRadar = 1u << 1,
    kSenseFlash = 1u << 2,
    kSenseAircraft = 1u << 3,
    kSenseReport = 1u << 4,
    kSenseHydrophone = 1u << 5,
};

struct Measurement {
    EntityId observer = kNoEntity;
    EntityId truth = kNoEntity;  // internal association key, never exposed to AI/UI
    Vec2 pos;
    double sigma = 500;          // position 1-sigma (m)
    uint32_t sense = kSenseVisual;
    ClassLevel level = ClassLevel::Detected;
    SizeClass size = SizeClass::Unknown;
    ShipType typeGuess = ShipType::Unknown;
    std::string classGuess;      // ship class id guess (may be wrong)
    bool hasVelocity = false;
    Vec2 velocity;               // estimated from aspect / wake
    double velSigma = 10;
    bool hostileAct = false;     // target fired at us
};

struct Holder {
    EntityId ship = kNoEntity;
    double lastSeen = -1e9;
    uint32_t sense = 0;
};

struct Track {
    uint32_t id = 0;
    Vec2 pos, vel;
    double posSigma = 1000, velSigma = 15;
    double firstSeen = 0, lastSeen = 0, lastUpdate = 0;
    ClassLevel level = ClassLevel::Detected;
    SizeClass size = SizeClass::Unknown;
    ShipType typeGuess = ShipType::Unknown;
    std::string classGuess;
    bool hostile = false;      // confirmed enemy (identified or fired upon us)
    bool firedOnUs = false;
    bool lost = false;         // no current contact (last known position)
    uint32_t sense = 0;        // senses that contributed within the last minute
    std::vector<Holder> holders;
    // Observations the side has made: did the contact fire torpedoes / guns recently?
    double lastFlash = -1e9;

    double age(double now) const { return now - lastSeen; }
    double courseEstimate() const { return bearingOf(vel); }
    double speedEstimateKn() const { return length(vel) / 0.514444; }
    bool heldBy(EntityId ship, double now, double maxAge = 20.0) const;
    uint32_t senseOf(EntityId ship, double now, double maxAge = 20.0) const;
    Vec2 predict(double now) const { return pos + vel * (now - lastUpdate); }

private:
    friend class ContactPicture;
    friend class World;
    EntityId truth_ = kNoEntity;
};

class ContactPicture {
public:
    explicit ContactPicture(Side owner = Side::Neutral) : owner_(owner) {}
    Side owner() const { return owner_; }

    /// Integrate a sensor measurement. Returns the track and whether it was newly created.
    Track* report(const Measurement& m, double now, bool* created = nullptr);
    /// Propagate tracks, age holders and mark/drop lost tracks. Returns ids of tracks that became lost.
    std::vector<uint32_t> update(double now);

    Track* find(uint32_t id);
    const Track* find(uint32_t id) const;
    const std::vector<Track>& tracks() const { return tracks_; }
    std::vector<Track>& tracks() { return tracks_; }

    // Internal (sim only): association by true entity.
    Track* byTruth(EntityId truth);
    EntityId truthOf(const Track& t) const { return t.truth_; }
    void forget(EntityId truth);

private:
    Side owner_;
    std::vector<Track> tracks_;
    uint32_t nextId_ = 1;
};

}  // namespace it::sim
