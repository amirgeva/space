#include "procgen/Greebles.h"

#include <algorithm>
#include <cmath>

namespace space {

namespace {

constexpr glm::vec3 kU(1.0f, 0.0f, 0.0f);
constexpr glm::vec3 kV(0.0f, 1.0f, 0.0f);
constexpr glm::vec3 kN(0.0f, 0.0f, 1.0f);

// Places items inside a patch, rejecting spots that leave the patch, hit a keep-out
// area or collide with what was placed before.
class Placer {
public:
    Placer(const Patch& patch, const std::vector<UvRect>& keepOut)
        : m_patch(patch), m_keepOut(keepOut)
    {
    }

    bool tryPlace(Rng& rng, const glm::vec2& half, glm::vec2& center, bool checkOccupied = true)
    {
        const glm::vec2 room = m_patch.halfSize - half;
        if (room.x < 0.0f || room.y < 0.0f)
            return false;
        center = {rng.range(-room.x, room.x), rng.range(-room.y, room.y)};
        const UvRect r = UvRect::around(center, half);
        for (const UvRect& k : m_keepOut)
            if (r.overlaps(k))
                return false;
        if (checkOccupied) {
            const UvRect padded = UvRect::around(center, half + 0.08f);
            for (const UvRect& o : m_occupied)
                if (padded.overlaps(o))
                    return false;
            m_occupied.push_back(r);
        }
        return true;
    }

    void clearOccupied() { m_occupied.clear(); }

private:
    const Patch& m_patch;
    const std::vector<UvRect>& m_keepOut;
    std::vector<UvRect> m_occupied;
};

glm::mat4 local(const Patch& p, float u, float v, float h) { return p.frame * at({u, v, h}); }

// Box resting on the surface at (u, v).
void restingBox(MeshBuilder& b, const Patch& p, float u, float v, const glm::vec3& size, float base = 0.0f)
{
    b.box(local(p, u, v, base + size.z * 0.5f), size);
}

// Cylinder lying on the surface, running along u (alongU) or v.
void lyingCylinder(MeshBuilder& b, const Patch& p, float u, float v, float lift, float radius, float length,
                   bool alongU, int segments)
{
    const glm::mat4 f = p.frame * frameAlong({u, v, lift + radius}, alongU ? kU : kV, kN);
    b.cylinder(f, radius, -length * 0.5f, length * 0.5f, segments);
}

void block(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v,
           const glm::vec2& half, float height)
{
    use(b, rng.chance(0.5f) ? s.block : s.machinery);
    restingBox(b, p, u, v, {half.x * 2.0f, half.y * 2.0f, height});
    if (rng.chance(0.45f)) {
        // Stacked smaller box, offset towards one corner
        const glm::vec2 top = half * glm::vec2(rng.range(0.35f, 0.75f), rng.range(0.35f, 0.75f));
        const glm::vec2 off = (half - top) * glm::vec2(rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f));
        use(b, rng.chance(0.5f) ? s.block : s.machinery);
        restingBox(b, p, u + off.x, v + off.y, {top.x * 2.0f, top.y * 2.0f, height * rng.range(0.3f, 0.8f)}, height);
    }
}

void lShape(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v,
            const glm::vec2& half, float height)
{
    use(b, s.block);
    const float t = rng.range(0.3f, 0.45f);
    restingBox(b, p, u, v - half.y * (1.0f - t), {half.x * 2.0f, half.y * 2.0f * t, height});
    restingBox(b, p, u - half.x * (1.0f - t), v, {half.x * 2.0f * t, half.y * 2.0f, height * rng.range(0.6f, 1.3f)});
}

void vent(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v,
          const glm::vec2& half, float height)
{
    const float baseH = height * 0.35f;
    use(b, s.machinery);
    restingBox(b, p, u, v, {half.x * 2.0f, half.y * 2.0f, baseH});
    const int fins = rng.range(3, 7);
    const float finT = std::min(0.08f, half.x * 0.3f);
    const float finH = height * rng.range(0.4f, 0.8f);
    use(b, s.block);
    for (int i = 0; i < fins; ++i) {
        const float fu = u - half.x + finT + (2.0f * half.x - 2.0f * finT) * (float(i) + 0.5f) / float(fins);
        restingBox(b, p, fu, v, {finT, half.y * 1.8f, finH}, baseH);
    }
}

void pipeRun(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v,
             const glm::vec2& half, bool alongU)
{
    const float length = (alongU ? half.x : half.y) * 2.0f;
    const float r = std::min(alongU ? half.y : half.x, rng.range(0.1f, 0.28f) * s.scale);
    const float lift = r * 0.4f;
    use(b, s.pipe);
    lyingCylinder(b, p, u, v, lift, r, length, alongU, 8);
    // Flanges and supports along the run
    const int flanges = std::max(2, int(length / 1.5f));
    for (int i = 0; i < flanges; ++i) {
        const float t = -0.5f + float(i) / float(flanges - 1);
        const float fu = alongU ? u + t * (length - 0.2f) : u;
        const float fv = alongU ? v : v + t * (length - 0.2f);
        lyingCylinder(b, p, fu, fv, lift - r * 0.35f, r * 1.35f, 0.12f, alongU, 8);
        use(b, s.block);
        restingBox(b, p, fu, fv, alongU ? glm::vec3(0.2f, r * 2.2f, lift + r) : glm::vec3(r * 2.2f, 0.2f, lift + r));
        use(b, s.pipe);
    }
}

void knob(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v, float radius,
          float height)
{
    use(b, rng.chance(0.5f) ? s.machinery : s.block);
    const glm::mat4 f = local(p, u, v, 0.0f);
    const int seg = radius > 0.4f ? 14 : 10;
    b.cylinder(f, radius, 0.0f, height, seg);
    if (rng.chance(0.5f)) {
        b.dome(f, radius * 0.8f, height, seg, 3);
    } else {
        use(b, s.block);
        b.cylinder(f, radius * rng.range(0.4f, 0.7f), height, height + height * rng.range(0.3f, 0.8f), seg);
    }
}

void boxCluster(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v,
                const glm::vec2& half, float height)
{
    const int nu = rng.range(2, 3), nv = rng.range(2, 3);
    const glm::vec2 cell(half.x * 2.0f / float(nu), half.y * 2.0f / float(nv));
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            if (rng.chance(0.2f))
                continue;
            use(b, rng.chance(0.6f) ? s.block : s.machinery);
            const float cu = u - half.x + cell.x * (float(i) + 0.5f);
            const float cv = v - half.y + cell.y * (float(j) + 0.5f);
            restingBox(b, p, cu, cv, {cell.x * 0.85f, cell.y * 0.85f, height * rng.range(0.3f, 1.0f)});
        }
}

void antenna(MeshBuilder& b, Rng& rng, const Patch& p, const GreebleSettings& s, float u, float v)
{
    const float h = rng.range(1.0f, 3.0f) * s.scale;
    const float r = rng.range(0.035f, 0.07f) * s.scale;
    use(b, s.block);
    restingBox(b, p, u, v, glm::vec3(r * 6.0f, r * 6.0f, r * 3.0f));
    use(b, s.pipe);
    b.cylinder(local(p, u, v, 0.0f), r, 0.0f, h, 6);
    restingBox(b, p, u, v, glm::vec3(r * 4.0f), h);
}

void tank(MeshBuilder& b, Rng&, const Patch& p, const GreebleSettings& s, float u, float v, const glm::vec2& half,
          bool alongU)
{
    const float r = alongU ? half.y : half.x;
    const float length = (alongU ? half.x : half.y) * 2.0f;
    if (length < 2.5f * r)
        return;
    use(b, s.block);
    const glm::mat4 f = p.frame * frameAlong({u, v, r * 1.1f}, alongU ? kU : kV, kN);
    b.cylinder(f, r, -length * 0.5f + r, length * 0.5f - r, 12, false);
    b.dome(f, r, length * 0.5f - r, 12, 3, true);
    b.dome(f, r, -length * 0.5f + r, 12, 3, false);
    restingBox(b, p, u, v, alongU ? glm::vec3(length * 0.5f, r, r * 0.4f) : glm::vec3(r, length * 0.5f, r * 0.4f));
}

} // namespace

void greeble(MeshBuilder& b, Rng& rng, const Patch& patch, const GreebleSettings& s)
{
    if (patch.halfSize.x <= 0.2f || patch.halfSize.y <= 0.2f)
        return;

    const glm::vec2 savedTile = b.uvTile();
    const std::size_t savedMaterial = b.material();

    const float area = 4.0f * patch.halfSize.x * patch.halfSize.y;
    const int items = int(area * s.density);
    const float sc = s.scale;
    Placer placer(patch, s.keepOut);

    // Layered armor plates first; other items may sit on top of them.
    const int plates = int(float(items) * s.plateRatio * 0.4f);
    for (int i = 0; i < plates; ++i) {
        const glm::vec2 half(rng.range(0.8f, 3.0f) * sc, rng.range(0.8f, 3.5f) * sc);
        glm::vec2 c;
        if (!placer.tryPlace(rng, glm::min(half, patch.halfSize), c))
            continue;
        const glm::vec2 h = glm::min(half, patch.halfSize);
        const float t = rng.range(0.05f, 0.16f) * sc;
        use(b, s.plate);
        restingBox(b, patch, c.x, c.y, {h.x * 2.0f, h.y * 2.0f, t});
        if (rng.chance(0.3f)) {
            const glm::vec2 inner = h * rng.range(0.5f, 0.8f);
            restingBox(b, patch, c.x, c.y, {inner.x * 2.0f, inner.y * 2.0f, t * 0.8f}, t);
        }
    }
    placer.clearOccupied();

    for (int i = 0; i < items; ++i) {
        const float kind = rng.unit();
        const float maxH = s.maxHeight * sc;
        glm::vec2 c;

        if (kind < 0.36f) {
            const glm::vec2 half(rng.range(0.2f, 1.0f) * sc, rng.range(0.2f, 1.0f) * sc);
            if (placer.tryPlace(rng, half, c))
                block(b, rng, patch, s, c.x, c.y, half, rng.range(0.15f, 1.0f) * maxH);
        } else if (kind < 0.46f) {
            const glm::vec2 half(rng.range(0.4f, 1.1f) * sc, rng.range(0.4f, 1.1f) * sc);
            if (placer.tryPlace(rng, half, c))
                lShape(b, rng, patch, s, c.x, c.y, half, rng.range(0.15f, 0.6f) * maxH);
        } else if (kind < 0.57f) {
            const glm::vec2 half(rng.range(0.3f, 0.8f) * sc, rng.range(0.3f, 0.9f) * sc);
            if (placer.tryPlace(rng, half, c))
                vent(b, rng, patch, s, c.x, c.y, half, rng.range(0.3f, 0.7f) * maxH);
        } else if (kind < 0.68f) {
            const bool alongU = rng.chance(0.5f);
            const float len = rng.range(1.5f, 6.0f) * sc;
            const glm::vec2 half = alongU ? glm::vec2(len * 0.5f, 0.3f * sc) : glm::vec2(0.3f * sc, len * 0.5f);
            if (placer.tryPlace(rng, half, c, false))
                pipeRun(b, rng, patch, s, c.x, c.y, half, alongU);
        } else if (kind < 0.79f) {
            const float r = rng.range(0.15f, 0.6f) * sc;
            if (placer.tryPlace(rng, glm::vec2(r), c))
                knob(b, rng, patch, s, c.x, c.y, r, rng.range(0.1f, 0.6f) * maxH);
        } else if (kind < 0.92f) {
            const glm::vec2 half(rng.range(0.5f, 1.2f) * sc, rng.range(0.5f, 1.2f) * sc);
            if (placer.tryPlace(rng, half, c))
                boxCluster(b, rng, patch, s, c.x, c.y, half, rng.range(0.2f, 0.7f) * maxH);
        } else if (kind < 0.96f) {
            const bool alongU = rng.chance(0.5f);
            const float r = rng.range(0.25f, 0.5f) * sc;
            const float len = r * rng.range(3.0f, 6.0f);
            const glm::vec2 half = alongU ? glm::vec2(len * 0.5f, r) : glm::vec2(r, len * 0.5f);
            if (placer.tryPlace(rng, half, c))
                tank(b, rng, patch, s, c.x, c.y, half, alongU);
        } else {
            if (placer.tryPlace(rng, glm::vec2(0.25f * sc), c))
                antenna(b, rng, patch, s, c.x, c.y);
        }
    }

    b.setMaterial(savedMaterial);
    b.setUvTile(savedTile);
}

void circuitTraces(MeshBuilder& b, Rng& rng, const Patch& patch, const Surface& surface, int count, float width,
                   const std::vector<UvRect>& keepOut)
{
    use(b, surface);
    const float height = width * 0.35f;
    const glm::vec2 lim = patch.halfSize - width;
    if (lim.x <= 0.0f || lim.y <= 0.0f)
        return;

    auto blocked = [&](const glm::vec2& a, const glm::vec2& c) {
        const UvRect r{glm::min(a, c) - width, glm::max(a, c) + width};
        if (std::abs(c.x) > lim.x || std::abs(c.y) > lim.y)
            return true;
        for (const UvRect& k : keepOut)
            if (r.overlaps(k))
                return true;
        return false;
    };

    for (int t = 0; t < count; ++t) {
        glm::vec2 pos(rng.range(-lim.x, lim.x), rng.range(-lim.y, lim.y));
        if (blocked(pos, pos))
            continue;
        bool alongU = rng.chance(0.5f);
        const int steps = rng.range(2, 6);
        // Pad at the start of the trace
        restingBox(b, patch, pos.x, pos.y, {width * 2.2f, width * 2.2f, height});
        for (int s = 0; s < steps; ++s) {
            const float len = rng.range(0.6f, 3.5f) * rng.sign();
            const glm::vec2 next = pos + (alongU ? glm::vec2(len, 0.0f) : glm::vec2(0.0f, len));
            if (blocked(pos, next))
                break;
            const glm::vec2 mid = (pos + next) * 0.5f;
            const glm::vec2 ext = glm::abs(next - pos) + width;
            restingBox(b, patch, mid.x, mid.y, {ext.x, ext.y, height});
            pos = next;
            alongU = !alongU;
        }
        restingBox(b, patch, pos.x, pos.y, {width * 2.2f, width * 2.2f, height});
    }
}

} // namespace space
