#include "procgen/ShipParts.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace space {

namespace {
constexpr float kPi = glm::pi<float>();
}

Face classifyFace(const glm::vec2& n)
{
    if (n.y > 0.9f)
        return Face::Top;
    if (n.y < -0.9f)
        return Face::Bottom;
    if (std::abs(n.x) > 0.9f)
        return Face::Side;
    return Face::Chamfer;
}

std::vector<FacePatch> facePatches(const glm::mat4& xf, const Profile& prof, float z0, float z1, float margin)
{
    std::vector<FacePatch> out;
    const std::size_t n = prof.size();
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = prof[i], c = prof[(i + 1) % n];
        const float len = glm::length(c - a);
        if (len < 2.0f * margin + 0.4f)
            continue;
        const glm::vec2 t = (c - a) / len;
        const glm::vec2 nrm(t.y, -t.x);
        const glm::vec2 mid = (a + c) * 0.5f;
        const glm::mat4 frame(glm::vec4(t, 0, 0), glm::vec4(0, 0, 1, 0), glm::vec4(nrm, 0, 0),
                              glm::vec4(mid, (z0 + z1) * 0.5f, 1));
        out.push_back({{xf * frame, {len * 0.5f - margin, (z1 - z0) * 0.5f - margin}}, nrm, classifyFace(nrm)});
    }
    return out;
}

UvRect zBand(const Patch& p, float zCenter, float z0, float z1, float pad)
{
    return {{-p.halfSize.x - 1.0f, z0 - zCenter - pad}, {p.halfSize.x + 1.0f, z1 - zCenter + pad}};
}

GreebleSettings ShipParts::greebleSettings(float density, float scale) const
{
    GreebleSettings s;
    s.plate = m_m.hull;
    s.block = m_m.hull;
    s.machinery = m_m.greeble;
    s.pipe = m_m.hullDark;
    s.density = density;
    s.scale = scale;
    return s;
}

void ShipParts::light(const Surface& s, const glm::vec3& pos, float size)
{
    use(m_b, s);
    m_b.box(at(pos), glm::vec3(size));
}

void ShipParts::nozzle(const glm::mat4& mount, float r, const Surface& glow, bool glowingRim)
{
    const int seg = r > 1.0f ? 28 : 16;
    use(m_b, m_m.hullDark);
    m_b.cylinder(mount, r * 1.12f, -0.2f, r * 0.25f, seg);
    use(m_b, m_m.hullDark, {r * 2.0f, r});
    m_b.frustum(mount, r * 0.84f, r, r * 0.25f, r * 1.05f, seg, false);

    use(m_b, m_m.nozzle);
    ExtrudeOptions inner;
    inner.shading = Shading::Smooth;
    inner.inward = true;
    inner.cap0 = inner.cap1 = false;
    inner.scale0 = glm::vec2(0.72f);
    m_b.extrude(mount, profile::circle(r * 0.9f, seg), r * 0.3f, r * 1.05f, inner);

    use(m_b, glowingRim ? glow : m_m.hullDark);
    m_b.annulus(mount, r * 0.9f, r, r * 1.05f, seg);
    use(m_b, glow);
    m_b.disc(mount, r * 0.66f, r * 0.3f, seg);
}

void ShipParts::dockingPort(const glm::mat4& f, float r, float len)
{
    use(m_b, m_m.hullDark);
    m_b.cylinder(f, r * 1.45f, -0.2f, 0.25f, 18);
    use(m_b, m_m.hull);
    m_b.cylinder(f, r, 0.25f, len, 18, false);
    use(m_b, m_m.accent);
    m_b.cylinder(f, r * 1.22f, len, len + 0.45f, 18);
    use(m_b, m_m.greeble);
    m_b.cylinder(f, r * 0.6f, len + 0.45f, len + 0.6f, 12);
    use(m_b, m_m.truss);
    for (int k = 0; k < 4; ++k) {
        const float a = kPi * 0.25f + float(k) * kPi * 0.5f;
        const glm::vec2 d(std::cos(a), std::sin(a));
        m_b.beam(f, glm::vec3(d * r * 1.38f, 0.25f), glm::vec3(d * r * 1.1f, len), 0.12f);
    }
    for (int k = 0; k < 2; ++k) {
        const float a = kPi * 0.5f + float(k) * kPi;
        const glm::vec2 d(std::cos(a), std::sin(a));
        use(m_b, m_m.lamp);
        m_b.box(f * at(glm::vec3(d * r * 1.3f, len + 0.22f)), glm::vec3(0.18f));
    }
}

void ShipParts::rcsPod(const glm::mat4& f)
{
    use(m_b, m_m.hullDark);
    m_b.box(f * at({0, 0, 0.3f}), {1.1f, 1.1f, 0.6f});
    use(m_b, m_m.greeble);
    m_b.box(f * at({0, 0, 0.7f}), {0.7f, 0.7f, 0.2f});
    const glm::vec3 dirs[] = {{0, 0, 1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3& d : dirs) {
        const glm::vec3 origin = glm::vec3(0, 0, 0.3f) + d * (d.z > 0 ? 0.5f : 0.55f);
        const glm::mat4 nf = f * frameAlong(origin, d, glm::vec3(0, 0, 1));
        use(m_b, m_m.hullDark);
        m_b.frustum(nf, 0.14f, 0.22f, 0.0f, 0.3f, 8, false);
        use(m_b, m_m.thrusterGlow);
        m_b.disc(nf, 0.12f, 0.08f, 8);
    }
}

void ShipParts::truss(const glm::mat4& xf, float z0, float z1, float half, float thickness, bool xBrace)
{
    use(m_b, m_m.truss);
    const glm::vec2 corners[] = {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
    for (const glm::vec2& c : corners)
        m_b.beam(xf, {c, z0}, {c, z1}, thickness * 1.3f);
    const int bays = std::max(1, int(std::round((z1 - z0) / (half * 2.0f))));
    const float bay = (z1 - z0) / float(bays);
    for (int i = 0; i <= bays; ++i) {
        const float z = z0 + bay * float(i);
        for (int k = 0; k < 4; ++k)
            m_b.beam(xf, {corners[k], z}, {corners[(k + 1) % 4], z}, thickness);
    }
    for (int i = 0; i < bays; ++i) {
        const float za = z0 + bay * float(i), zb = za + bay;
        for (int k = 0; k < 4; ++k) {
            const glm::vec2 a = corners[k], c = corners[(k + 1) % 4];
            if (xBrace || i % 2 == 0)
                m_b.beam(xf, {a, za}, {c, zb}, thickness * 0.8f);
            if (xBrace || i % 2 == 1)
                m_b.beam(xf, {c, za}, {a, zb}, thickness * 0.8f);
        }
    }
}

void ShipParts::portholeRow(Rng& rng, const Patch& p, float u, float vMin, float vMax,
                            const std::vector<UvRect>& keepOut)
{
    const float r = rng.range(0.28f, 0.42f);
    const float spacing = r * rng.range(2.6f, 3.4f);
    const int group = rng.range(3, 6);
    const float litChance = rng.range(0.0f, 0.5f);
    int inGroup = 0;
    for (float v = vMin + r * 2.0f; v < vMax - r * 2.0f; v += spacing) {
        if (inGroup == group) {
            inGroup = 0;
            v += spacing * rng.range(1.5f, 4.0f);
            continue;
        }
        const UvRect rect = UvRect::around({u, v}, glm::vec2(r * 1.5f));
        if (std::any_of(keepOut.begin(), keepOut.end(), [&](const UvRect& k) { return rect.overlaps(k); }))
            continue;
        const glm::mat4 f = p.frame * at({u, v, 0.0f});
        use(m_b, m_m.hullDark);
        m_b.cylinder(f, r * 1.4f, -0.05f, 0.05f, 14);
        use(m_b, rng.chance(litChance) ? m_m.lamp : m_m.porthole);
        m_b.cylinder(f, r, -0.05f, 0.09f, 14);
        ++inGroup;
    }
}

void ShipParts::dish(const glm::mat4& f, float radius)
{
    const float depth = radius * 0.35f;
    const int seg = 24;
    // Bowl: a shallow cone, with both the concave front and convex back surfaces
    ExtrudeOptions bowl;
    bowl.shading = Shading::Smooth;
    bowl.cap0 = bowl.cap1 = false;
    bowl.scale0 = glm::vec2(0.15f);
    use(m_b, m_m.hull);
    m_b.extrude(f, profile::circle(radius, seg), 0.0f, depth, bowl);
    bowl.inward = true;
    use(m_b, m_m.hullDark);
    m_b.extrude(f, profile::circle(radius, seg), 0.0f, depth, bowl);
    m_b.disc(f, radius * 0.15f, 0.0f, seg, false);
    use(m_b, m_m.hull);
    m_b.annulus(f, radius * 0.97f, radius * 1.03f, depth, seg);
    // Feed horn on three struts
    use(m_b, m_m.truss);
    const glm::vec3 feed(0.0f, 0.0f, depth + radius * 0.55f);
    for (int k = 0; k < 3; ++k) {
        const float a = float(k) * 2.0f * kPi / 3.0f;
        m_b.beam(f, glm::vec3(std::cos(a) * radius * 0.9f, std::sin(a) * radius * 0.9f, depth), feed, radius * 0.03f);
    }
    use(m_b, m_m.greeble);
    m_b.cylinder(f, radius * 0.08f, feed.z - radius * 0.1f, feed.z + radius * 0.05f, 10);
    // Mount
    use(m_b, m_m.hullDark);
    m_b.cylinder(f, radius * 0.12f, -radius * 0.4f, 0.0f, 10);
}

} // namespace space
