#pragma once
// A tactical group (division / task unit). Members[0] is the guide; the others keep station on it.
#include <string>
#include <vector>

#include "core/Math.h"
#include "sim/Types.h"

namespace it::sim {

enum class Formation : uint8_t { Column, LineAbreast, EchelonRight, EchelonLeft, Screen, Free };
const char* formationKey(Formation f);
Formation formationFromString(const std::string& s);

struct Group {
    int id = -1;
    std::string name;
    Side side = Side::Neutral;
    std::vector<EntityId> members;
    Formation formation = Formation::Column;
    double spacing = 900.0;   // m between ships
    double course = 0;        // ordered course for the guide (deg)
    double speedKn = 15;      // ordered formation speed
    std::vector<Vec2> trail;  // guide's track (breadcrumbs, newest last) for follow-the-leader
    bool playerControlled = false;
    std::string doctrine;
};

}  // namespace it::sim
