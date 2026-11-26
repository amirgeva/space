#pragma once

#include "geometry/ModelData.h"

#include <glm/glm.hpp>

#include <vector>

namespace space {

// A convex 2D outline in the XY plane, counter-clockwise when viewed from +Z.
using Profile = std::vector<glm::vec2>;

namespace profile {

Profile rect(float width, float height);
// Rectangle with its corners cut at 45 degrees (an octagon for chamfer > 0).
Profile chamferedRect(float width, float height, float chamfer);
Profile circle(float radius, int segments);
// Regular polygon; the first vertex sits at angle `phase` (radians) from +X.
Profile regular(int sides, float radius, float phase = 0.0f);
// Flat bottom at y = 0 with a half-ellipse roof of the given width and height.
Profile arch(float width, float height, int segments);

} // namespace profile

enum class Shading { Flat, Smooth };

struct ExtrudeOptions {
    glm::vec2 scale0{1.0f};  // profile scale at z0
    glm::vec2 scale1{1.0f};  // profile scale at z1 (taper)
    glm::vec2 offset1{0.0f}; // profile shift at z1 (shear)
    Shading shading = Shading::Flat;
    bool cap0 = true;
    bool cap1 = true;
    bool inward = false;     // faces point inside (e.g. the interior of a nozzle)
};

struct RevolveOptions {
    Shading around = Shading::Smooth; // across the angular segments
    Shading profile = Shading::Flat;  // across the profile's edges
    float angle0 = 0.0f;
    float angle1 = 6.28318530718f;
    bool caps = true; // close the ends of a partial revolution
};

// Rigid transform whose local +Z points along `dir`, positioned at `origin`.
glm::mat4 frameAlong(const glm::vec3& origin, const glm::vec3& dir,
                     const glm::vec3& upHint = glm::vec3(0.0f, 1.0f, 0.0f));
// Local +Z -> world +Y, local +X -> world +X, local +Y -> world -Z.
glm::mat4 upFrame(const glm::vec3& origin);
glm::mat4 at(const glm::vec3& origin);

// Accumulates procedural geometry into a ModelData, one submesh per material.
//
// Every primitive is built in a local frame and placed with a rigid transform `xf`
// (rotation + translation only; sizes are passed explicitly so texture coordinates
// can be computed in world units and stay consistent between parts).
class MeshBuilder {
public:
    explicit MeshBuilder(ModelData& model);

    std::size_t addMaterial(const MaterialDesc& material);

    void setMaterial(std::size_t material) { m_material = material; }
    std::size_t material() const { return m_material; }

    // World units per texture repeat along u / v. A value <= 0 fits the texture once
    // across the face in that direction.
    void setUvTile(const glm::vec2& tile) { m_uvTile = tile; }
    glm::vec2 uvTile() const { return m_uvTile; }

    // Extrudes `profile` along local Z from z0 to z1. Side u runs around the outline,
    // side v along Z; caps are mapped planar.
    void extrude(const glm::mat4& xf, const Profile& profile, float z0, float z1,
                 const ExtrudeOptions& options = {});

    // Revolves a closed convex profile around local Z (lathe). Profile coordinates are
    // (radius, z), counter-clockwise, so normals face outward. u runs around (arc length
    // at each face's mid radius), v along the profile. With flat `around` shading and
    // segment angles k * 2pi / segments, the facets line up with profile::circle polygons.
    void revolve(const glm::mat4& xf, const Profile& profile, int segments, const RevolveOptions& options = {});

    // Box of the given size centered on the local origin.
    void box(const glm::mat4& xf, const glm::vec3& size);
    void cylinder(const glm::mat4& xf, float radius, float z0, float z1, int segments, bool caps = true);
    void frustum(const glm::mat4& xf, float r0, float r1, float z0, float z1, int segments,
                 bool caps = true);
    // Flat polygon at local z facing +Z (or -Z).
    void polygon(const glm::mat4& xf, const Profile& profile, float z, bool facePositiveZ = true,
                 const glm::vec2& scale = glm::vec2(1.0f), const glm::vec2& offset = glm::vec2(0.0f));
    void disc(const glm::mat4& xf, float radius, float z, int segments, bool facePositiveZ = true);
    void annulus(const glm::mat4& xf, float innerRadius, float outerRadius, float z, int segments,
                 bool facePositiveZ = true);
    // Flat face at local z between two convex outlines that both contain the origin, `inner` inside
    // `outer`: a polygon with a hole, such as a wall with a doorway. Mapped planar in local x / y.
    void polygonRing(const glm::mat4& xf, const Profile& outer, const Profile& inner, float z,
                     bool facePositiveZ = true);
    // Hemisphere whose equator lies at local z, bulging towards +Z (or -Z).
    void dome(const glm::mat4& xf, float radius, float z, int segments, int rings, bool towardsPositiveZ = true);
    // Square-section bar between two points given in xf's local space.
    void beam(const glm::mat4& xf, const glm::vec3& a, const glm::vec3& b, float thickness,
              const glm::vec3& upHint = glm::vec3(0.0f, 1.0f, 0.0f));

private:
    MeshData& target();
    float uvU(float distance, float total) const;
    float uvV(float distance, float total) const;

    ModelData& m_model;
    std::vector<int> m_subMeshOfMaterial;
    std::size_t m_material = 0;
    glm::vec2 m_uvTile{4.0f};
};

} // namespace space
