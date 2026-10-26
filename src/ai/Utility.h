#pragma once
// Utility-based decision making (Infinite Axis Utility System style): each option multiplies the
// scores of its considerations, each consideration maps a normalised input through a response curve.
// All considerations are kept for the decision log so every AI choice can be explained.
#include <cmath>
#include <string>
#include <vector>

#include "core/Math.h"

namespace it::ai {

enum class Curve : uint8_t { Linear, Quadratic, Sqrt, Logistic, Bell };

/// Response curve on x in [0,1]. `invert` mirrors the output (1 - y).
double respond(Curve c, double x, bool invert = false, double steep = 10.0, double mid = 0.5);

struct Consideration {
    std::string name;  // localisation key "ai.c.<name>"
    double input = 0;  // raw normalised input
    double score = 1;  // after response curve
};

struct OptionScore {
    std::string option;  // localisation key "ai.o.<name>"
    double weight = 1.0;
    double score = 0;
    std::vector<Consideration> considerations;

    OptionScore(std::string name, double w = 1.0) : option(std::move(name)), weight(w) {}
    OptionScore& add(const char* name, double input, double score);
    /// Final score with IAUS compensation for the number of considerations.
    double finalize();
};

/// Pick the best option, applying a hysteresis bonus to the current choice.
int choose(std::vector<OptionScore>& options, const std::string& current, double inertia = 0.15);

}  // namespace it::ai
