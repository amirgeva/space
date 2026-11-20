#pragma once

#include "procgen/Rng.h"
#include "procgen/Surface.h"

#include <vector>

namespace space {

struct GreebleSettings {
    Surface plate;     // flat armor plates layered on the hull
    Surface block;     // hull-colored blocks
    Surface machinery; // textured machinery boxes, vents, knobs
    Surface pipe;      // pipes and antennas

    float density = 0.25f;   // items per square unit
    float plateRatio = 0.3f; // plates relative to other items
    float scale = 1.0f;      // size multiplier for all items
    float maxHeight = 1.2f;  // tallest regular item (before scale)
    std::vector<UvRect> keepOut;
};

// Scatters surface detail ("greebles") over a patch: layered plates, blocks, stacked
// boxes, vents, pipe runs, knobs, small tanks and antennas.
void greeble(MeshBuilder& b, Rng& rng, const Patch& patch, const GreebleSettings& settings);

// Raised Manhattan-style trace lines (the circuit-like relief seen on big hull sides).
void circuitTraces(MeshBuilder& b, Rng& rng, const Patch& patch, const Surface& surface, int count,
                   float width, const std::vector<UvRect>& keepOut = {});

} // namespace space
