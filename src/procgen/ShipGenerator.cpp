#include "procgen/ShipGenerator.h"

#include "procgen/ships/Fighter.h"
#include "procgen/ships/LogisticsFreighter.h"
#include "procgen/ships/WheelStation.h"

#include <limits>

namespace space {

namespace {

void recenter(ModelData& model)
{
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const auto& sub : model.subMeshes)
        for (const Vertex& v : sub.mesh.vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
    if (lo.x > hi.x)
        return;
    const glm::vec3 center = (lo + hi) * 0.5f;
    for (auto& sub : model.subMeshes)
        for (Vertex& v : sub.mesh.vertices)
            v.position -= center;
}

} // namespace

ModelData generateShip(const ShipParams& params)
{
    ModelData model;
    switch (params.type) {
    case ShipType::LogisticsFreighter:
        model = generateLogisticsFreighter(params.seed, params.textureDir);
        break;
    case ShipType::WheelStation:
        model = generateWheelStation(params.seed, params.textureDir);
        break;
    case ShipType::Fighter:
        model = generateFighter(params.seed, params.textureDir);
        break;
    }
    recenter(model);
    return model;
}

std::string_view toString(ShipType type)
{
    switch (type) {
    case ShipType::LogisticsFreighter:
        return "freighter";
    case ShipType::WheelStation:
        return "station";
    case ShipType::Fighter:
        return "fighter";
    }
    return "unknown";
}

std::optional<ShipType> shipTypeFromString(std::string_view name)
{
    if (name == "freighter" || name == "logistics" || name == "port")
        return ShipType::LogisticsFreighter;
    if (name == "station" || name == "wheel")
        return ShipType::WheelStation;
    if (name == "fighter")
        return ShipType::Fighter;
    return std::nullopt;
}

} // namespace space
