#pragma once

#include "geometry/ModelData.h"

#include <cstdint>
#include <filesystem>

namespace space {

// Wheel space station spinning around Z:
//   central hub  - the port of entry: front docking collar with a lit bay, radial docking
//                  ports and approach light masts; spoke collars; greebled prism hull
//   rear section - service module with radiators, mast with solar arrays and a dish
//   outer ring   - living quarters (one or two rings): box-section or toroidal habitat,
//                  bulkheads between modules, window rows, greenhouse glazing, greebles
//   spokes       - transit tubes between hub and ring with bulkheads and elevator cars
ModelData generateWheelStation(std::uint64_t seed, const std::filesystem::path& textureDir);

} // namespace space
