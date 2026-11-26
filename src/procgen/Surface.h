#pragma once

#include "geometry/MeshBuilder.h"

#include <glm/glm.hpp>

#include <cstddef>

namespace space {

// A material together with how its texture is scaled on the geometry.
struct Surface {
    std::size_t material = 0;
    glm::vec2 tile{4.0f}; // world units per repeat; <= 0 fits once across the face
};

inline void use(MeshBuilder& b, const Surface& s)
{
    b.setMaterial(s.material);
    b.setUvTile(s.tile);
}

inline void use(MeshBuilder& b, const Surface& s, const glm::vec2& tile)
{
    b.setMaterial(s.material);
    b.setUvTile(tile);
}

// A flat rectangular area of a hull to decorate. The frame's origin is the center of
// the area on the surface; local X/Y span it (u/v) and local +Z is the outward normal.
struct Patch {
    glm::mat4 frame{1.0f};
    glm::vec2 halfSize{0.0f};
};

// Axis-aligned rectangle in a patch's (u, v) coordinates.
struct UvRect {
    glm::vec2 min;
    glm::vec2 max;

    bool overlaps(const UvRect& o) const
    {
        return min.x < o.max.x && o.min.x < max.x && min.y < o.max.y && o.min.y < max.y;
    }
    static UvRect around(const glm::vec2& center, const glm::vec2& half) { return {center - half, center + half}; }
};

} // namespace space
