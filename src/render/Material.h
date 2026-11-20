#pragma once

#include "geometry/ModelData.h"
#include "render/Texture.h"

#include <memory>

namespace space {

// GPU-side material: the CPU description plus its resolved textures. Maps that a
// material doesn't use are bound to a 1x1 white texture so shaders can always sample.
struct Material {
    MaterialDesc desc;
    std::shared_ptr<Texture> diffuseMap;
    std::shared_ptr<Texture> emissiveMap;
};

} // namespace space
