#pragma once

#include "geometry/ModelData.h"

#include <cstdint>
#include <filesystem>

namespace space {

// Logistics freighter / mobile port. Built along +Z from stern to bow:
//   engine block (nozzle cluster, cowling, RCS pods, optional radiators)
//   -> neck with pipe runs
//   -> main hull (armor belts, greebles, side panels with portholes and trace relief,
//      docking ports, bridge tower, underside bays)
//   -> cargo section (coaxial drums, container racks or tank rings on a truss spine)
//   -> bow (tapered prow or docking pod, antenna spars).
ModelData generateLogisticsFreighter(std::uint64_t seed, const std::filesystem::path& textureDir);

} // namespace space
