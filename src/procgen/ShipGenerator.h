#pragma once

#include "geometry/ModelData.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace space {

enum class ShipType {
    LogisticsFreighter, // long cargo hauler / mobile port: engine block, hull, bridge, cargo spine
    WheelStation,       // spinning wheel station: docking hub, spokes, habitat ring(s)
};

struct ShipParams {
    ShipType type = ShipType::LogisticsFreighter;
    std::uint64_t seed = 1;
    std::filesystem::path textureDir; // directory holding the ship textures (assets/textures/ship)
};

// Generates a spacecraft model. The same parameters always produce the same model.
// The result is centered on its bounding box, with the bow pointing along +Z and up +Y.
ModelData generateShip(const ShipParams& params);

std::string_view toString(ShipType type);
std::optional<ShipType> shipTypeFromString(std::string_view name);

// All ship types, in declaration order.
inline constexpr ShipType kShipTypes[] = {ShipType::LogisticsFreighter, ShipType::WheelStation};

} // namespace space
