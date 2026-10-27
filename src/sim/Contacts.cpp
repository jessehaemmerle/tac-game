#include "sim/Contacts.h"

#include <algorithm>

namespace it::sim {

bool Track::heldBy(EntityId ship, double now, double maxAge) const {
    for (auto& h : holders)
        if (h.ship == ship && now - h.lastSeen <= maxAge) return true;
    return false;
}

uint32_t Track::senseOf(EntityId ship, double now, double maxAge) const {
    for (auto& h : holders)
        if (h.ship == ship && now - h.lastSeen <= maxAge) return h.sense;
    return 0;
}

bool AirTrack::heldBy(EntityId ship, double now, double maxAge) const {
    for (auto& h : holders)
        if (h.ship == ship && now - h.lastSeen <= maxAge) return true;
    return false;
}

double ContactPicture::reportDelay() {
    if (!rng_) return reportMin_;
    double d = rng_->uniform(reportMin_, reportMax_);
    // Lost or garbled reports: the information reaches the flag much later, through other channels.
    if (!rng_->chance(reportReliability_)) d += rng_->uniform(300.0, 900.0);
    return d;
}

Track* ContactPicture::report(const Measurement& meas, double now, bool* created) {
    Measurement m = meas;
    if (auto it = intel_.find(m.truth); it != intel_.end() && m.level < ClassLevel::Identified) {
        // Known from intelligence: the plot puts a name to the contact straight away.
        m.level = ClassLevel::Identified;
        m.size = it->second.size;
        m.typeGuess = it->second.type;
        m.classGuess = it->second.classId;
    }
    Track* t = byTruth(m.truth);
    if (created) *created = (t == nullptr);
    if (!t) {
        Track nt;
        nt.id = nextId_++;
        nt.truth_ = m.truth;
        nt.pos = m.pos;
        nt.vel = m.hasVelocity ? m.velocity : Vec2{};
        nt.posSigma = m.sigma;
        nt.velSigma = m.hasVelocity ? m.velSigma : 12.0;
        nt.firstSeen = nt.lastSeen = nt.lastUpdate = now;
        // Gun flashes are seen by everybody nearby; radio reports from aircraft and ships take time.
        nt.sharedAt = now + ((m.sense & kSenseFlash) ? 15.0 : reportDelay());
        tracks_.push_back(nt);
        t = &tracks_.back();
        changes_.push_back({t->id, false, (m.sense & kSenseRadar) != 0});
    } else {
        double dt = std::max(0.0, now - t->lastUpdate);
        Vec2 pred = t->pos + t->vel * dt;
        double sp2 = sqr(t->posSigma) + sqr(t->velSigma * dt);
        double sm2 = sqr(m.sigma);
        double k = sp2 / (sp2 + sm2);
        Vec2 innov = m.pos - pred;
        t->pos = pred + innov * k;
        t->posSigma = std::sqrt(sp2 * sm2 / (sp2 + sm2));
        if (dt > 0.5) {
            // Velocity from the innovation; long baselines give the most information.
            double base = std::max(dt, 4.0);
            double mv2 = sqr(m.sigma / base);
            double kv = clamp(sqr(t->velSigma) / (sqr(t->velSigma) + mv2), 0.03, 0.6) * k;
            t->vel += innov / base * kv;
            t->velSigma = std::max(0.4, std::sqrt(sqr(t->velSigma) * (1.0 - kv)));
        }
        if (m.hasVelocity) {
            double kv = sqr(t->velSigma) / (sqr(t->velSigma) + sqr(m.velSigma));
            t->vel = t->vel + (m.velocity - t->vel) * kv;
            t->velSigma = std::max(0.4, std::sqrt(sqr(t->velSigma) * sqr(m.velSigma) / (sqr(t->velSigma) + sqr(m.velSigma))));
        }
        if (t->lost) t->sharedAt = std::max(t->sharedAt, now + reportDelay() * 0.5);  // re-acquired: new report
        t->lastUpdate = t->lastSeen = now;
    }
    if (m.level > t->level || (m.level == t->level && m.level >= ClassLevel::TypeKnown && !m.classGuess.empty())) {
        if (t->level < ClassLevel::ClassKnown && m.level >= ClassLevel::ClassKnown && !m.classGuess.empty())
            changes_.push_back({t->id, true, false});
        t->level = std::max(t->level, m.level);
        if (m.size != SizeClass::Unknown) t->size = m.size;
        if (m.typeGuess != ShipType::Unknown) t->typeGuess = m.typeGuess;
        if (!m.classGuess.empty()) t->classGuess = m.classGuess;
    } else if (t->size == SizeClass::Unknown && m.size != SizeClass::Unknown) {
        t->size = m.size;
    }
    if (m.level >= ClassLevel::Identified) t->hostile = true;
    if (m.hostileAct) {
        t->hostile = true;
        t->firedOnUs = true;
    }
    if (m.sense & kSenseFlash) t->lastFlash = now;
    t->sense |= m.sense;
    t->lost = false;
    bool found = false;
    for (auto& h : t->holders)
        if (h.ship == m.observer) {
            h.lastSeen = now;
            h.sense |= m.sense;
            found = true;
        }
    if (!found && m.observer != kNoEntity) t->holders.push_back({m.observer, now, m.sense});
    return t;
}

AirTrack* ContactPicture::reportAir(const AirMeasurement& m, double now, bool* created) {
    AirTrack* t = airByTruth(m.truth);
    if (created) *created = (t == nullptr);
    if (!t) {
        AirTrack nt;
        nt.id = nextId_++;
        nt.truth_ = m.truth;
        nt.pos = m.pos;
        nt.vel = m.hasVelocity ? m.velocity : Vec2{};
        nt.posSigma = m.sigma;
        nt.firstSeen = nt.lastSeen = nt.lastUpdate = now;
        // Air raid warnings go out at once by voice radio and TBS; aircraft reports take longer.
        nt.sharedAt = now + (m.observer != kNoEntity ? 10.0 : reportDelay());
        airTracks_.push_back(nt);
        t = &airTracks_.back();
        changes_.push_back({t->id, false, (m.sense & kSenseRadar) != 0, true});
    } else {
        double dt = std::max(0.0, now - t->lastUpdate);
        Vec2 pred = t->pos + t->vel * dt;
        double sp2 = sqr(t->posSigma) + sqr(25.0 * dt);  // aircraft manoeuvre: large process noise
        double sm2 = sqr(m.sigma);
        double k = sp2 / (sp2 + sm2);
        Vec2 innov = m.pos - pred;
        t->pos = pred + innov * k;
        t->posSigma = std::sqrt(sp2 * sm2 / (sp2 + sm2));
        if (dt > 1.0) t->vel += innov / std::max(dt, 5.0) * clamp(k, 0.1, 0.6);
        if (m.hasVelocity) t->vel = t->vel + (m.velocity - t->vel) * 0.5;
        if (t->lost) t->sharedAt = std::max(t->sharedAt, now + 10.0);
        t->lastUpdate = t->lastSeen = now;
    }
    if (m.altitude >= 0) t->altitude = t->altitude < 0 ? m.altitude : lerp(t->altitude, m.altitude, 0.5);
    if (m.count > 0) t->count = m.count;
    if (m.roleKnown) {
        t->roleKnown = true;
        t->role = m.role;
    }
    if (m.attacking) {
        t->attacking = true;
        t->attackSeen = now;
    } else if (now - t->attackSeen > 60.0) {
        t->attacking = false;
    }
    t->sense |= m.sense;
    t->lost = false;
    bool found = false;
    for (auto& h : t->holders)
        if (h.ship == m.observer) {
            h.lastSeen = now;
            h.sense |= m.sense;
            found = true;
        }
    if (!found && m.observer != kNoEntity) t->holders.push_back({m.observer, now, m.sense});
    return t;
}

AirTrack* ContactPicture::findAir(uint32_t id) {
    for (auto& t : airTracks_)
        if (t.id == id) return &t;
    return nullptr;
}

const AirTrack* ContactPicture::findAir(uint32_t id) const {
    for (auto& t : airTracks_)
        if (t.id == id) return &t;
    return nullptr;
}

AirTrack* ContactPicture::airByTruth(EntityId truth) {
    for (auto& t : airTracks_)
        if (t.truth_ == truth) return &t;
    return nullptr;
}

std::vector<uint32_t> ContactPicture::update(double now) {
    std::vector<uint32_t> lostNow;
    for (auto& t : tracks_) {
        t.holders.erase(std::remove_if(t.holders.begin(), t.holders.end(), [&](const Holder& h) { return now - h.lastSeen > 90.0; }),
                        t.holders.end());
        uint32_t s = 0;
        for (auto& h : t.holders)
            if (now - h.lastSeen < 60.0) s |= h.sense;
        t.sense = s;
        if (!t.lost && now - t.lastSeen > 120.0) {
            t.lost = true;
            lostNow.push_back(t.id);
        }
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& t) { return t.lost && now - t.lastSeen > 1200.0; }),
                  tracks_.end());
    for (auto& t : airTracks_) {
        t.holders.erase(std::remove_if(t.holders.begin(), t.holders.end(), [&](const Holder& h) { return now - h.lastSeen > 60.0; }),
                        t.holders.end());
        uint32_t s = 0;
        for (auto& h : t.holders)
            if (now - h.lastSeen < 30.0) s |= h.sense;
        t.sense = s;
        if (!t.lost && now - t.lastSeen > 60.0) {
            t.lost = true;
            lostAir_.push_back(t.id);
        }
    }
    airTracks_.erase(std::remove_if(airTracks_.begin(), airTracks_.end(), [&](const AirTrack& t) { return t.lost && now - t.lastSeen > 600.0; }),
                     airTracks_.end());
    return lostNow;
}

Track* ContactPicture::find(uint32_t id) {
    for (auto& t : tracks_)
        if (t.id == id) return &t;
    return nullptr;
}

const Track* ContactPicture::find(uint32_t id) const {
    for (auto& t : tracks_)
        if (t.id == id) return &t;
    return nullptr;
}

Track* ContactPicture::byTruth(EntityId truth) {
    for (auto& t : tracks_)
        if (t.truth_ == truth) return &t;
    return nullptr;
}

void ContactPicture::forget(EntityId truth) {
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& t) { return t.truth_ == truth; }), tracks_.end());
}

}  // namespace it::sim
