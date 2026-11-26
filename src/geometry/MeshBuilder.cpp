#include "geometry/MeshBuilder.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace space {

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

namespace profile {

Profile rect(float width, float height)
{
    const float x = width * 0.5f, y = height * 0.5f;
    return {{-x, -y}, {x, -y}, {x, y}, {-x, y}};
}

Profile chamferedRect(float width, float height, float chamfer)
{
    const float x = width * 0.5f, y = height * 0.5f;
    const float c = std::min(chamfer, std::min(x, y) * 0.99f);
    if (c <= 0.0f)
        return rect(width, height);
    return {{-x + c, -y}, {x - c, -y}, {x, -y + c}, {x, y - c},
            {x - c, y},   {-x + c, y}, {-x, y - c}, {-x, -y + c}};
}

Profile circle(float radius, int segments) { return regular(segments, radius, 0.0f); }

Profile regular(int sides, float radius, float phase)
{
    Profile p;
    p.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const float a = phase + glm::two_pi<float>() * float(i) / float(sides);
        p.emplace_back(radius * std::cos(a), radius * std::sin(a));
    }
    return p;
}

Profile arch(float width, float height, int segments)
{
    Profile p;
    const float x = width * 0.5f;
    for (int i = 0; i <= segments; ++i) {
        const float a = glm::pi<float>() * float(i) / float(segments);
        p.emplace_back(x * std::cos(a), height * std::sin(a));
    }
    return p; // runs from (+x, 0) over the top to (-x, 0); the closing edge is the floor
}

} // namespace profile

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

glm::mat4 frameAlong(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& upHint)
{
    const glm::vec3 z = glm::normalize(dir);
    glm::vec3 up = upHint;
    if (std::abs(glm::dot(z, glm::normalize(up))) > 0.99f)
        up = std::abs(z.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    const glm::vec3 x = glm::normalize(glm::cross(up, z));
    const glm::vec3 y = glm::cross(z, x);
    return glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(origin, 1));
}

glm::mat4 upFrame(const glm::vec3& origin)
{
    return glm::mat4(glm::vec4(1, 0, 0, 0), glm::vec4(0, 0, -1, 0), glm::vec4(0, 1, 0, 0),
                     glm::vec4(origin, 1));
}

glm::mat4 at(const glm::vec3& origin)
{
    glm::mat4 m(1.0f);
    m[3] = glm::vec4(origin, 1.0f);
    return m;
}

// ---------------------------------------------------------------------------
// MeshBuilder
// ---------------------------------------------------------------------------

MeshBuilder::MeshBuilder(ModelData& model)
    : m_model(model)
{
}

std::size_t MeshBuilder::addMaterial(const MaterialDesc& material)
{
    m_model.materials.push_back(material);
    return m_model.materials.size() - 1;
}

MeshData& MeshBuilder::target()
{
    if (m_material >= m_subMeshOfMaterial.size())
        m_subMeshOfMaterial.resize(m_material + 1, -1);
    int& index = m_subMeshOfMaterial[m_material];
    if (index < 0) {
        index = static_cast<int>(m_model.subMeshes.size());
        m_model.subMeshes.push_back({MeshData{}, m_material});
    }
    return m_model.subMeshes[index].mesh;
}

float MeshBuilder::uvU(float distance, float total) const
{
    if (m_uvTile.x > 0.0f)
        return distance / m_uvTile.x;
    return total > 0.0f ? distance / total : 0.0f;
}

float MeshBuilder::uvV(float distance, float total) const
{
    if (m_uvTile.y > 0.0f)
        return distance / m_uvTile.y;
    return total > 0.0f ? distance / total : 0.0f;
}

namespace {

struct Emitter {
    MeshData& mesh;
    glm::mat4 xf;
    glm::mat3 normalMatrix;
    bool flip;

    Emitter(MeshData& m, const glm::mat4& transform, bool inward)
        : mesh(m), xf(transform), normalMatrix(glm::transpose(glm::inverse(glm::mat3(transform)))),
          flip(inward)
    {
    }

    std::uint32_t vertex(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv)
    {
        glm::vec3 wn = normalMatrix * n;
        const float len = glm::length(wn);
        wn = len > 1e-8f ? wn / len : glm::vec3(0, 1, 0);
        mesh.vertices.push_back({glm::vec3(xf * glm::vec4(p, 1.0f)), flip ? -wn : wn, uv});
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    }

    void triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
    {
        if (flip)
            std::swap(b, c);
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
    }

    void quad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
    {
        triangle(a, b, c);
        triangle(a, c, d);
    }
};

} // namespace

void MeshBuilder::extrude(const glm::mat4& xf, const Profile& prof, float z0, float z1,
                          const ExtrudeOptions& opt)
{
    const std::size_t n = prof.size();
    if (n < 2 || z1 <= z0)
        return;

    Emitter e(target(), xf, opt.inward);

    auto ring = [&](std::size_t i, bool end) {
        const glm::vec2& p = prof[i % n];
        const glm::vec2 q = end ? p * opt.scale1 + opt.offset1 : p * opt.scale0;
        return glm::vec3(q, end ? z1 : z0);
    };

    // Distance around the outline (at mid-taper) drives u.
    const glm::vec2 midScale = (opt.scale0 + opt.scale1) * 0.5f;
    std::vector<float> dist(n + 1, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        dist[i + 1] = dist[i] + glm::length((prof[(i + 1) % n] - prof[i]) * midScale);
    const float perimeter = dist[n];

    std::vector<glm::vec3> faceNormal(n);
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec3 a = ring(i, false), b = ring(i + 1, false), c = ring(i + 1, true), d = ring(i, true);
        const glm::vec3 fn = glm::cross(c - a, d - b);
        const float len = glm::length(fn);
        faceNormal[i] = len > 1e-8f ? fn / len : glm::vec3(prof[i], 0.0f);
    }

    const float vA = m_uvTile.y > 0.0f ? z0 / m_uvTile.y : 0.0f;
    const float vB = m_uvTile.y > 0.0f ? z1 / m_uvTile.y : 1.0f;

    if (opt.shading == Shading::Flat) {
        for (std::size_t i = 0; i < n; ++i) {
            const float u0 = uvU(dist[i], perimeter), u1 = uvU(dist[i + 1], perimeter);
            const glm::vec3& fn = faceNormal[i];
            const auto a = e.vertex(ring(i, false), fn, {u0, vA});
            const auto b = e.vertex(ring(i + 1, false), fn, {u1, vA});
            const auto c = e.vertex(ring(i + 1, true), fn, {u1, vB});
            const auto d = e.vertex(ring(i, true), fn, {u0, vB});
            e.quad(a, b, c, d);
        }
    } else {
        std::uint32_t first = 0;
        for (std::size_t i = 0; i <= n; ++i) {
            const glm::vec3 vn = glm::normalize(faceNormal[(i + n - 1) % n] + faceNormal[i % n]);
            const float u = uvU(dist[i], perimeter);
            const auto a = e.vertex(ring(i, false), vn, {u, vA});
            e.vertex(ring(i, true), vn, {u, vB});
            if (i == 0)
                first = a;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t a = first + 2 * i, b = a + 2;
            e.quad(a, b, b + 1, a + 1);
        }
    }

    auto nonZero = [](const glm::vec2& s) { return s.x != 0.0f && s.y != 0.0f; };
    if (opt.cap0 && nonZero(opt.scale0))
        polygon(xf, prof, z0, opt.inward, opt.scale0);
    if (opt.cap1 && nonZero(opt.scale1))
        polygon(xf, prof, z1, !opt.inward, opt.scale1, opt.offset1);
}

void MeshBuilder::polygon(const glm::mat4& xf, const Profile& prof, float z, bool facePositiveZ,
                          const glm::vec2& scale, const glm::vec2& offset)
{
    const std::size_t n = prof.size();
    if (n < 3)
        return;

    glm::vec2 lo(1e30f), hi(-1e30f);
    for (const glm::vec2& p : prof) {
        lo = glm::min(lo, p * scale + offset);
        hi = glm::max(hi, p * scale + offset);
    }
    const glm::vec2 size = hi - lo;

    Emitter e(target(), xf, false);
    const glm::vec3 normal(0.0f, 0.0f, facePositiveZ ? 1.0f : -1.0f);
    const std::uint32_t first = static_cast<std::uint32_t>(e.mesh.vertices.size());
    for (const glm::vec2& p : prof) {
        const glm::vec2 q = p * scale + offset;
        // Mirror u on the back face so textures don't appear flipped.
        const float x = facePositiveZ ? q.x - lo.x : hi.x - q.x;
        e.vertex({q, z}, normal, {uvU(x, size.x), uvV(q.y - lo.y, size.y)});
    }
    for (std::uint32_t i = 1; i + 1 < n; ++i) {
        if (facePositiveZ)
            e.triangle(first, first + i, first + i + 1);
        else
            e.triangle(first, first + i + 1, first + i);
    }
}

void MeshBuilder::revolve(const glm::mat4& xf, const Profile& prof, int segments, const RevolveOptions& opt)
{
    const std::size_t n = prof.size();
    const float span = opt.angle1 - opt.angle0;
    if (n < 2 || segments < 1 || span <= 0.0f)
        return;

    // Outward edge normals in the (r, z) plane and distance along the profile
    std::vector<glm::vec2> edgeNormal(n);
    std::vector<float> dist(n + 1, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 d = prof[(i + 1) % n] - prof[i];
        const float len = glm::length(d);
        edgeNormal[i] = len > 1e-8f ? glm::vec2(d.y, -d.x) / len : glm::vec2(1.0f, 0.0f);
        dist[i + 1] = dist[i] + len;
    }
    const float perimeter = dist[n];

    struct Row {
        glm::vec2 p, n;
        float v;
    };
    std::vector<std::pair<Row, Row>> strips;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        if (opt.profile == Shading::Flat) {
            strips.push_back({{prof[i], edgeNormal[i], dist[i]}, {prof[j], edgeNormal[i], dist[i + 1]}});
        } else {
            const glm::vec2 ni = glm::normalize(edgeNormal[(i + n - 1) % n] + edgeNormal[i]);
            const glm::vec2 nj = glm::normalize(edgeNormal[i] + edgeNormal[j]);
            strips.push_back({{prof[i], ni, dist[i]}, {prof[j], nj, dist[i + 1]}});
        }
    }

    auto pos = [](const glm::vec2& p, float a) { return glm::vec3(p.x * std::cos(a), p.x * std::sin(a), p.y); };
    auto nrm = [](const glm::vec2& q, float a) { return glm::vec3(q.x * std::cos(a), q.x * std::sin(a), q.y); };

    Emitter e(target(), xf, false);
    for (const auto& [r0, r1] : strips) {
        const float rRef = 0.5f * (r0.p.x + r1.p.x);
        const float arc = span * rRef;
        const float v0 = uvV(r0.v, perimeter), v1 = uvV(r1.v, perimeter);
        if (opt.around == Shading::Smooth) {
            const std::uint32_t base = static_cast<std::uint32_t>(e.mesh.vertices.size());
            for (int k = 0; k <= segments; ++k) {
                const float t = float(k) / float(segments);
                const float a = opt.angle0 + span * t;
                const float u = uvU(t * arc, arc);
                e.vertex(pos(r0.p, a), nrm(r0.n, a), {u, v0});
                e.vertex(pos(r1.p, a), nrm(r1.n, a), {u, v1});
            }
            for (std::uint32_t k = 0; k < std::uint32_t(segments); ++k) {
                const std::uint32_t a0 = base + 2 * k; // r0 at angle k
                e.quad(a0, a0 + 2, a0 + 3, a0 + 1);
            }
        } else {
            for (int k = 0; k < segments; ++k) {
                const float t0 = float(k) / float(segments), t1 = float(k + 1) / float(segments);
                const float a0 = opt.angle0 + span * t0, a1 = opt.angle0 + span * t1;
                const float am = 0.5f * (a0 + a1);
                const float u0 = uvU(t0 * arc, arc), u1 = uvU(t1 * arc, arc);
                const auto A = e.vertex(pos(r0.p, a0), nrm(r0.n, am), {u0, v0});
                const auto B = e.vertex(pos(r1.p, a0), nrm(r1.n, am), {u0, v1});
                const auto C = e.vertex(pos(r1.p, a1), nrm(r1.n, am), {u1, v1});
                const auto D = e.vertex(pos(r0.p, a1), nrm(r0.n, am), {u1, v0});
                e.quad(A, D, C, B);
            }
        }
    }

    if (opt.caps && span < glm::two_pi<float>() - 1e-4f) {
        // Cap frames: local X = radial, local Y = axis, local Z = -tangent
        auto capFrame = [&](float a) {
            const glm::vec3 radial(std::cos(a), std::sin(a), 0.0f);
            const glm::vec3 axis(0.0f, 0.0f, 1.0f);
            return xf * glm::mat4(glm::vec4(radial, 0), glm::vec4(axis, 0), glm::vec4(glm::cross(radial, axis), 0),
                                  glm::vec4(0, 0, 0, 1));
        };
        polygon(capFrame(opt.angle0), prof, 0.0f, true);
        polygon(capFrame(opt.angle1), prof, 0.0f, false);
    }
}

void MeshBuilder::box(const glm::mat4& xf, const glm::vec3& size)
{
    extrude(xf, profile::rect(size.x, size.y), -size.z * 0.5f, size.z * 0.5f);
}

void MeshBuilder::cylinder(const glm::mat4& xf, float radius, float z0, float z1, int segments, bool caps)
{
    ExtrudeOptions opt;
    opt.shading = Shading::Smooth;
    opt.cap0 = opt.cap1 = caps;
    extrude(xf, profile::circle(radius, segments), z0, z1, opt);
}

void MeshBuilder::frustum(const glm::mat4& xf, float r0, float r1, float z0, float z1, int segments,
                          bool caps)
{
    const float r = std::max(r0, r1);
    ExtrudeOptions opt;
    opt.shading = Shading::Smooth;
    opt.scale0 = glm::vec2(r0 / r);
    opt.scale1 = glm::vec2(r1 / r);
    opt.cap0 = opt.cap1 = caps;
    extrude(xf, profile::circle(r, segments), z0, z1, opt);
}

void MeshBuilder::disc(const glm::mat4& xf, float radius, float z, int segments, bool facePositiveZ)
{
    polygon(xf, profile::circle(radius, segments), z, facePositiveZ);
}

void MeshBuilder::annulus(const glm::mat4& xf, float innerRadius, float outerRadius, float z, int segments,
                          bool facePositiveZ)
{
    Emitter e(target(), xf, !facePositiveZ);
    const glm::vec3 normal(0.0f, 0.0f, 1.0f); // Emitter flips it for -Z
    const float circumference = glm::two_pi<float>() * outerRadius;
    const std::uint32_t first = static_cast<std::uint32_t>(e.mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float t = float(i) / float(segments);
        const float a = glm::two_pi<float>() * t;
        const glm::vec2 dir(std::cos(a), std::sin(a));
        const float u = uvU(t * circumference, circumference);
        e.vertex({dir * innerRadius, z}, normal, {u, 0.0f});
        e.vertex({dir * outerRadius, z}, normal, {u, uvV(outerRadius - innerRadius, outerRadius - innerRadius)});
    }
    for (std::uint32_t i = 0; i < std::uint32_t(segments); ++i) {
        const std::uint32_t in0 = first + 2 * i, out0 = in0 + 1, in1 = in0 + 2, out1 = in0 + 3;
        e.quad(in0, out0, out1, in1);
    }
}

namespace {

// Where the ray from the origin at angle a leaves a convex outline that contains the origin.
glm::vec2 rayExit(const Profile& prof, float a)
{
    const glm::vec2 d(std::cos(a), std::sin(a));
    auto cross = [](const glm::vec2& p, const glm::vec2& q) { return p.x * q.y - p.y * q.x; };
    float best = 1e30f;
    for (std::size_t i = 0; i < prof.size(); ++i) {
        const glm::vec2 p = prof[i], e = prof[(i + 1) % prof.size()] - p;
        const float den = cross(d, e);
        if (std::abs(den) < 1e-12f)
            continue;
        const float t = cross(p, e) / den, s = cross(p, d) / den;
        if (t > 0.0f && s >= -1e-4f && s <= 1.0f + 1e-4f)
            best = std::min(best, t);
    }
    return d * (best < 1e30f ? best : 0.0f);
}

} // namespace

void MeshBuilder::polygonRing(const glm::mat4& xf, const Profile& outer, const Profile& inner, float z,
                              bool facePositiveZ)
{
    if (outer.size() < 3 || inner.size() < 3)
        return;

    // Sample both outlines on rays through every vertex of either, so each quad's outer edge lies on
    // `outer` and its inner edge on `inner`.
    std::vector<float> angles;
    for (const Profile* prof : {&outer, &inner})
        for (const glm::vec2& p : *prof)
            angles.push_back(std::atan2(p.y, p.x));
    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end(), [](float a, float b) { return b - a < 1e-5f; }),
                 angles.end());

    glm::vec2 lo(1e30f), hi(-1e30f);
    for (const glm::vec2& p : outer) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const glm::vec2 size = hi - lo;

    Emitter e(target(), xf, !facePositiveZ);
    const glm::vec3 normal(0.0f, 0.0f, 1.0f); // Emitter flips it for -Z
    auto vertex = [&](const glm::vec2& q) {
        // Mirror u on the back face so textures don't appear flipped.
        const float x = facePositiveZ ? q.x - lo.x : hi.x - q.x;
        return e.vertex({q, z}, normal, {uvU(x, size.x), uvV(q.y - lo.y, size.y)});
    };
    const std::size_t n = angles.size();
    for (std::size_t i = 0; i < n; ++i) {
        const float a0 = angles[i], a1 = angles[(i + 1) % n];
        const auto in0 = vertex(rayExit(inner, a0)), out0 = vertex(rayExit(outer, a0));
        const auto out1 = vertex(rayExit(outer, a1)), in1 = vertex(rayExit(inner, a1));
        e.quad(in0, out0, out1, in1);
    }
}

void MeshBuilder::dome(const glm::mat4& xf, float radius, float z, int segments, int rings, bool towardsPositiveZ)
{
    Emitter e(target(), xf, !towardsPositiveZ);
    const float dir = towardsPositiveZ ? 1.0f : -1.0f;
    const float circumference = glm::two_pi<float>() * radius;
    const float arc = glm::half_pi<float>() * radius;
    const std::uint32_t first = static_cast<std::uint32_t>(e.mesh.vertices.size());
    for (int k = 0; k <= rings; ++k) {
        const float phi = glm::half_pi<float>() * float(k) / float(rings);
        for (int i = 0; i <= segments; ++i) {
            const float t = float(i) / float(segments);
            const float a = glm::two_pi<float>() * t;
            const glm::vec3 n(std::cos(phi) * std::cos(a), std::cos(phi) * std::sin(a), dir * std::sin(phi));
            // For the -Z dome the Emitter negates normals, so pre-negate to keep them outward.
            e.vertex({n.x * radius, n.y * radius, z + n.z * radius}, towardsPositiveZ ? n : -n,
                     {uvU(t * circumference, circumference), uvV(phi * radius, arc)});
        }
    }
    const std::uint32_t stride = segments + 1;
    for (std::uint32_t k = 0; k < std::uint32_t(rings); ++k)
        for (std::uint32_t i = 0; i < std::uint32_t(segments); ++i) {
            const std::uint32_t a = first + k * stride + i;
            e.quad(a, a + 1, a + 1 + stride, a + stride);
        }
}

void MeshBuilder::beam(const glm::mat4& xf, const glm::vec3& a, const glm::vec3& b, float thickness,
                       const glm::vec3& upHint)
{
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-5f)
        return;
    box(xf * frameAlong((a + b) * 0.5f, d, upHint), {thickness, thickness, len});
}

} // namespace space
