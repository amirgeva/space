#pragma once

#include "procgen/Greebles.h"
#include "procgen/Rng.h"
#include "procgen/ShipMaterials.h"

#include <vector>

namespace space {

// Classification of a flat hull face by its outward normal.
enum class Face { Top, Bottom, Side, Chamfer };

Face classifyFace(const glm::vec2& normal);

struct FacePatch {
    Patch patch;
    glm::vec2 normal; // outward normal in the profile plane
    Face face;
};

// One patch per flat side face of a straight (untapered) extrusion along local Z.
// Patch u runs along the profile edge, v along +Z (v = z - center z).
std::vector<FacePatch> facePatches(const glm::mat4& xf, const Profile& prof, float z0, float z1, float margin);

// A rect spanning the whole patch width (u) between two z values (converted to v).
UvRect zBand(const Patch& p, float zCenter, float z0, float z1, float pad = 0.3f);

// Reusable spacecraft components shared by the ship generators.
class ShipParts {
public:
    ShipParts(MeshBuilder& b, const ShipMaterials& m)
        : m_b(b), m_m(m)
    {
    }

    GreebleSettings greebleSettings(float density, float scale = 1.0f) const;

    // Small emissive cube (navigation / marker light).
    void light(const Surface& s, const glm::vec3& pos, float size = 0.35f);

    // Engine bell. `mount` sits on the hull surface with local +Z pointing out.
    void nozzle(const glm::mat4& mount, float r, const Surface& glow, bool glowingRim);

    // Docking port: flange, tube, collar and hatch. `f` has local +Z pointing outward.
    void dockingPort(const glm::mat4& f, float r, float len);

    // Small attitude-control pod: block with nozzles pointing out and sideways.
    void rcsPod(const glm::mat4& f);

    // Square lattice truss along local Z.
    void truss(const glm::mat4& xf, float z0, float z1, float half, float thickness, bool xBrace);

    // Groups of round windows along v at a fixed u.
    void portholeRow(Rng& rng, const Patch& p, float u, float vMin, float vMax, const std::vector<UvRect>& keepOut);

    // Parabolic-looking dish antenna facing local +Z, with feed horn and struts.
    void dish(const glm::mat4& f, float radius);

private:
    MeshBuilder& m_b;
    const ShipMaterials& m_m;
};

} // namespace space
