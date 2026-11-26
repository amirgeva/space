#pragma once

#include "geometry/ModelData.h"

#include <cstdint>
#include <filesystem>

namespace space {

// Wheel space station spinning around Z:
//   central hub  - the port of entry: a hollow prism holding a hangar whose inner faces are decks
//                  sized for landing pads, entered through a lit slot in the front wall with
//                  approach light masts and airlock ports; spoke collars; greebled hull with
//                  high-resolution plating, since it is seen from close by
//   rear section - service module with radiators, mast with solar arrays and a dish
//   outer ring   - living quarters (one or two rings): box-section or toroidal habitat,
//                  bulkheads between modules, window rows, greenhouse glazing, greebles
//   spokes       - transit tubes between hub and ring with bulkheads and elevator cars
ModelData generateWheelStation(std::uint64_t seed, const std::filesystem::path& textureDir);

} // namespace space
