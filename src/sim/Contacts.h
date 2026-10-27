#pragma once
// The contact picture of one side: everything that side believes about enemy ships, built only
// from its own sensors (lookouts, radar, gun flashes, aircraft, radio reports). AI and the player's
// tactical map read exclusively from here — this is what makes the AI "fair".
#include <map>
#include <string>
#include <vector>

#include "core/Math.h"
#include "core/Random.h"
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
    /// When the flag (side level) learns of this track via radio / signal reports. Ships holding the
    /// contact know it immediately; everyone else only after the report gets through.
    double sharedAt = 0;

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

/// A sighting or radar return of an aircraft formation.
struct AirMeasurement {
    EntityId observer = kNoEntity;  // ship (kNoEntity: reported by aircraft)
    EntityId truth = kNoEntity;     // internal association key (the formation)
    Vec2 pos;
    double sigma = 1000;
    double altitude = -1;           // estimate (m); < 0: unknown (radar without height finding)
    int count = 0;                  // estimated number of aircraft (0: unknown)
    bool roleKnown = false;
    AirRole role = AirRole::Recon;
    bool attacking = false;         // seen diving, running in low or releasing
    uint32_t sense = kSenseVisual;
    bool hasVelocity = false;
    Vec2 velocity;
};

/// The side's estimate of an aircraft formation ("raid").
struct AirTrack {
    uint32_t id = 0;
    Vec2 pos, vel;
    double posSigma = 2000;
    double altitude = -1;
    int count = 0;
    bool roleKnown = false;
    AirRole role = AirRole::Recon;
    bool attacking = false;
    double attackSeen = -1e9;
    double firstSeen = 0, lastSeen = 0, lastUpdate = 0;
    bool lost = false;
    uint32_t sense = 0;
    double sharedAt = 0;
    std::vector<Holder> holders;
    double age(double now) const { return now - lastSeen; }
    Vec2 predict(double now) const { return pos + vel * (now - lastUpdate); }
    bool heldBy(EntityId ship, double now, double maxAge = 20.0) const;

private:
    friend class ContactPicture;
    EntityId truth_ = kNoEntity;
};

class ContactPicture {
public:
    explicit ContactPicture(Side owner = Side::Neutral) : owner_(owner) {}
    Side owner() const { return owner_; }
    /// Command & control quality: delay range (s) and probability that a contact report gets through.
    void setReporting(double minDelay, double maxDelay, double reliability, Rng* rng) {
        reportMin_ = minDelay;
        reportMax_ = maxDelay;
        reportReliability_ = reliability;
        rng_ = rng;
    }

    /// Integrate a sensor measurement. Returns the track and whether it was newly created.
    Track* report(const Measurement& m, double now, bool* created = nullptr);
    /// New tracks and first class identifications since the last call (World turns them into events).
    struct Change {
        uint32_t track = 0;
        bool identified = false;  // false: new contact
        bool radar = false;       // new contact made by radar
        bool air = false;         // new air contact
    };
    std::vector<Change> takeChanges() {
        std::vector<Change> out;
        out.swap(changes_);
        return out;
    }
    /// Propagate tracks, age holders and mark/drop lost tracks. Returns ids of tracks that became lost.
    std::vector<uint32_t> update(double now);

    Track* find(uint32_t id);
    const Track* find(uint32_t id) const;
    const std::vector<Track>& tracks() const { return tracks_; }
    std::vector<Track>& tracks() { return tracks_; }

    /// Air picture: aircraft formations seen by radar, lookouts and own aircraft.
    AirTrack* reportAir(const AirMeasurement& m, double now, bool* created = nullptr);
    const std::vector<AirTrack>& airTracks() const { return airTracks_; }
    AirTrack* findAir(uint32_t id);
    const AirTrack* findAir(uint32_t id) const;
    /// Air tracks that became lost in the last update (World turns them into events).
    std::vector<uint32_t> takeLostAir() {
        std::vector<uint32_t> out;
        out.swap(lostAir_);
        return out;
    }

    /// Prior intelligence (decrypts, earlier sightings): a track on this ship is identified as soon as it
    /// is first detected. Sim/scenario use only.
    void addPriorIntel(EntityId truth, ShipType type, SizeClass size, const std::string& classId) { intel_[truth] = {type, size, classId}; }

    // Internal (sim only): association by true entity.
    Track* byTruth(EntityId truth);
    EntityId truthOf(const Track& t) const { return t.truth_; }
    AirTrack* airByTruth(EntityId truth);
    EntityId truthOf(const AirTrack& t) const { return t.truth_; }
    void forget(EntityId truth);

private:
    double reportDelay();
    Side owner_;
    double reportMin_ = 20, reportMax_ = 60, reportReliability_ = 0.9;
    Rng* rng_ = nullptr;
    std::vector<Track> tracks_;
    std::vector<AirTrack> airTracks_;
    std::vector<uint32_t> lostAir_;
    std::vector<Change> changes_;
    struct Intel {
        ShipType type;
        SizeClass size;
        std::string classId;
    };
    std::map<EntityId, Intel> intel_;
    uint32_t nextId_ = 1;
};

}  // namespace it::sim
