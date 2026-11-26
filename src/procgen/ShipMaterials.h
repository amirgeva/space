#pragma once

#include "procgen/Rng.h"
#include "procgen/Surface.h"

#include <filesystem>
#include <string>
#include <vector>

namespace space {

// The material palette shared by the ship generators. Colors (the "livery") are
// picked from the RNG; textures come from assets/textures/ship.
struct ShipMaterials {
    Surface hull;         // main plating
    Surface hullDark;     // darker plating: necks, collars, frames
    Surface accent;       // livery stripes and trim
    Surface greeble;      // machinery detail
    Surface truss;        // structural beams
    Surface nozzle;       // engine bell interior
    Surface engineGlow;   // main engine exhaust (emissive)
    Surface thrusterGlow; // small thrusters (emissive)
    Surface windows;      // lit window bands (emissive map)
    Surface porthole;     // dark glass
    Surface lamp;         // warm lit glass
    Surface navRed;
    Surface navGreen;
    Surface navWhite;
    Surface solar;        // photovoltaic panels
    Surface greenhouse;   // lit agricultural glazing (emissive map)
    std::vector<Surface> cargo; // tinted container / tank variants

    glm::vec3 hullTint{1.0f};   // livery colors, for materials a ship type adds on its own
    glm::vec3 accentTint{1.0f};

    static ShipMaterials create(MeshBuilder& b, const std::filesystem::path& textureDir, Rng& rng);
};

// Phong material with a diffuse texture, tinted by `tint`.
MaterialDesc texturedMaterial(const std::string& name, const std::filesystem::path& texture, const glm::vec3& tint,
                              float specular, float shininess);

} // namespace space
