#pragma once
// Every AI decision with its alternatives and considerations, for the in-game "AI insight" window
// and the after-action debrief.
#include <deque>
#include <string>
#include <vector>

#include "ai/Utility.h"
#include "sim/Types.h"

namespace it::ai {

enum class DecisionLevel : uint8_t { Commander, Group, Captain };

struct DecisionRecord {
    double time = 0;
    sim::Side side = sim::Side::Neutral;
    DecisionLevel level = DecisionLevel::Group;
    std::string actor;     // group or ship name
    std::string decision;  // localisation key
    std::string detail;    // free text / loc key with context (target, course...)
    std::vector<std::string> args;
    double score = 0;
    std::vector<OptionScore> alternatives;  // includes the winner
};

class DecisionLog {
public:
    void add(DecisionRecord r) {
        records_.push_back(std::move(r));
        while (records_.size() > kMax) records_.pop_front();
    }
    const std::deque<DecisionRecord>& records() const { return records_; }
    void clear() { records_.clear(); }

private:
    static constexpr size_t kMax = 4000;
    std::deque<DecisionRecord> records_;
};

}  // namespace it::ai
