#include "ai/Utility.h"

namespace it::ai {

double respond(Curve c, double x, bool invert, double steep, double mid) {
    x = saturate(x);
    double y = x;
    switch (c) {
        case Curve::Linear: y = x; break;
        case Curve::Quadratic: y = x * x; break;
        case Curve::Sqrt: y = std::sqrt(x); break;
        case Curve::Logistic: y = 1.0 / (1.0 + std::exp(-steep * (x - mid))); break;
        case Curve::Bell: y = std::exp(-sqr((x - mid) * steep * 0.5)); break;
    }
    return invert ? 1.0 - y : y;
}

OptionScore& OptionScore::add(const char* name, double input, double s) {
    considerations.push_back({name, input, saturate(s)});
    return *this;
}

double OptionScore::finalize() {
    double s = weight;
    if (considerations.empty()) return score = s;
    double modification = 1.0 - 1.0 / double(considerations.size());
    for (auto& c : considerations) {
        double v = c.score;
        double makeUp = (1.0 - v) * modification;
        s *= v + makeUp * v;
    }
    return score = s;
}

int choose(std::vector<OptionScore>& options, const std::string& current, double inertia) {
    int best = -1;
    double bestScore = -1;
    for (size_t i = 0; i < options.size(); ++i) {
        double s = options[i].finalize();
        if (options[i].option == current) s *= 1.0 + inertia;
        if (s > bestScore) {
            bestScore = s;
            best = int(i);
        }
    }
    return best;
}

}  // namespace it::ai
