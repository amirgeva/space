#pragma once

#include <cstdint>
#include <vector>

namespace space {

// Small deterministic RNG (xoshiro128**). Unlike <random> distributions, results are
// identical on every compiler/platform, so a seed always generates the same ship.
class Rng {
public:
    explicit Rng(std::uint64_t seed)
    {
        // Expand the seed with splitmix64.
        for (std::uint32_t& s : m_state) {
            seed += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            s = static_cast<std::uint32_t>((z ^ (z >> 31)) >> 16);
        }
    }

    std::uint32_t next()
    {
        const std::uint32_t result = rotl(m_state[1] * 5, 7) * 9;
        const std::uint32_t t = m_state[1] << 9;
        m_state[2] ^= m_state[0];
        m_state[3] ^= m_state[1];
        m_state[1] ^= m_state[2];
        m_state[0] ^= m_state[3];
        m_state[2] ^= t;
        m_state[3] = rotl(m_state[3], 11);
        return result;
    }

    // Uniform in [0, 1).
    float unit() { return float(next() >> 8) * (1.0f / 16777216.0f); }
    // Uniform in [lo, hi).
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
    // Uniform integer in [lo, hi] (inclusive).
    int range(int lo, int hi) { return lo + int(next() % std::uint32_t(hi - lo + 1)); }
    bool chance(float probability) { return unit() < probability; }
    float sign() { return chance(0.5f) ? 1.0f : -1.0f; }

    template <typename T>
    const T& pick(const std::vector<T>& items)
    {
        return items[std::size_t(range(0, int(items.size()) - 1))];
    }

    // Independent child generator, so adding detail to one part of a ship doesn't
    // reshuffle every part generated after it.
    Rng fork() { return Rng((std::uint64_t(next()) << 32) | next()); }

private:
    static std::uint32_t rotl(std::uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

    std::uint32_t m_state[4];
};

} // namespace space
