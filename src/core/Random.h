#pragma once
// Deterministic PCG32 random number generator. Every World owns one so that simulations replay
// identically for a given seed (headless AI evaluation, bug reproduction).
#include <cmath>
#include <cstdint>

namespace it {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) { reseed(seed, stream); }

    void reseed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0u;
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }
    /// Uniform in [0, 1).
    double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    int range(int lo, int hiInclusive) { return lo + int(next() % uint32_t(hiInclusive - lo + 1)); }
    bool chance(double p) { return uniform() < p; }
    /// Standard normal via Box-Muller.
    double normal() {
        if (hasSpare_) { hasSpare_ = false; return spare_; }
        double u, v, s;
        do {
            u = uniform() * 2.0 - 1.0;
            v = uniform() * 2.0 - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        s = std::sqrt(-2.0 * std::log(s) / s);
        spare_ = v * s;
        hasSpare_ = true;
        return u * s;
    }
    double normal(double mean, double sigma) { return mean + sigma * normal(); }
    /// Derive an independent child generator (e.g. per subsystem) without disturbing determinism.
    Rng fork(uint64_t salt) { return Rng(uint64_t(next()) << 32 ^ next() ^ salt, salt * 2 + 1); }

private:
    uint64_t state_ = 0, inc_ = 0;
    double spare_ = 0.0;
    bool hasSpare_ = false;
};

}  // namespace it
