#pragma once

#include "geometry/ModelData.h"

#include <cstdint>
#include <filesystem>

namespace space {

// Single-seat space fighter. Built along +Z from tail to nose:
//   engines (one or two internal nozzles with side intakes, or two wing-root nacelles)
//   -> faceted fuselage (engine section, mid section with canopy and dorsal fairing,
//      tapered forward fuselage, nose cone with sensor probe)
//   -> wings (swept, delta or forward-swept plates with optional dihedral), optional
//      tail planes, single or twin canted fins and ventral fins
//   -> weapons (wing cannons, under-wing missiles, wingtip rails) and nav lights.
ModelData generateFighter(std::uint64_t seed, const std::filesystem::path& textureDir);

} // namespace space
