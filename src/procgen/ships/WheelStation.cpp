#include "procgen/ships/WheelStation.h"

#include "geometry/MeshBuilder.h"
#include "procgen/Greebles.h"
#include "procgen/Rng.h"
#include "procgen/ShipMaterials.h"
#include "procgen/ShipParts.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace space {

namespace {

constexpr float kPi = glm::pi<float>();
constexpr float kTwoPi = 2.0f * kPi;
constexpr glm::vec3 kAxis(0.0f, 0.0f, 1.0f);

glm::vec3 radial(float a) { return {std::cos(a), std::sin(a), 0.0f}; }

// Scales an (r, z) profile about a center point.
Profile scaledAbout(const Profile& prof, const glm::vec2& center, float s)
{
    Profile out;
    for (const glm::vec2& p : prof)
        out.push_back(center + (p - center) * s);
    return out;
}

// Prism with flat faces whose inscribed radius is `inner`.
Profile prism(int sides, float inner)
{
    return profile::regular(sides, inner / std::cos(kPi / float(sides)), kPi / float(sides));
}

// Flat facet of a faceted (flat-around) revolve: segment between angles a0..a1, profile
// edge p0 -> p1 in (r, z). u runs around the ring, v along the profile edge, +Z outward.
Patch ringFacet(float a0, float a1, const glm::vec2& p0, const glm::vec2& p1, float margin)
{
    const float am = 0.5f * (a0 + a1), h = 0.5f * (a1 - a0), c = std::cos(h);
    const glm::vec3 rad = radial(am);
    const glm::vec3 tangent(-std::sin(am), std::cos(am), 0.0f);
    const glm::vec2 mid = (p0 + p1) * 0.5f;
    const glm::vec2 d = p1 - p0;
    const glm::vec3 edge = rad * (d.x * c) + kAxis * d.y;
    const glm::vec3 y = glm::normalize(edge);
    const glm::vec3 n = glm::cross(tangent, y);
    const glm::vec3 origin = rad * (mid.x * c) + kAxis * mid.y;
    const glm::mat4 frame(glm::vec4(tangent, 0), glm::vec4(y, 0), glm::vec4(n, 0), glm::vec4(origin, 1));
    return {frame, {std::min(p0.x, p1.x) * std::sin(h) - margin, glm::length(edge) * 0.5f - margin}};
}

class WheelStation {
public:
    WheelStation(std::uint64_t seed, const std::filesystem::path& textureDir, ModelData& out)
        : m_rng(seed), m_b(out), m_parts(m_b, m_mat)
    {
        Rng livery = m_rng.fork();
        m_mat = ShipMaterials::create(m_b, textureDir, livery);
    }

    void build()
    {
        Rng dims = m_rng.fork();
        m_R = dims.range(38.0f, 70.0f);
        const float scale = std::clamp(m_R / 50.0f, 0.8f, 1.3f);
        m_ringW = dims.range(7.0f, 13.0f);
        m_ringH = m_ringW * dims.range(0.6f, 0.95f);
        m_ringC = std::min(m_ringW, m_ringH) * dims.range(0.1f, 0.25f);
        m_torus = dims.chance(0.3f);
        m_spokes = dims.pick(std::vector<int>{3, 4, 4, 5, 6, 6, 8});
        m_modulesPerSpoke = dims.range(2, 4);
        m_modules = m_spokes * m_modulesPerSpoke;
        m_segsPerModule = std::max(2, int(std::round(kTwoPi * m_R / float(m_modules) / dims.range(4.5f, 6.5f))));
        m_segments = m_modules * m_segsPerModule;
        m_hubR = dims.range(5.0f, 9.0f) * scale;
        m_hubSides = 2 * dims.range(4, 6);
        m_spokeR = dims.range(0.9f, 1.6f) * scale;
        if (dims.chance(0.25f)) {
            const float d = m_ringW * 0.5f + dims.range(4.0f, 9.0f);
            m_ringZ = {-d, d};
        } else {
            m_ringZ = {0.0f};
        }
        m_hubZ0 = m_ringZ.front() - dims.range(8.0f, 16.0f);
        m_hubZ1 = m_ringZ.back() + dims.range(8.0f, 16.0f);

        Rng hubRng = m_rng.fork();
        Rng dockRng = m_rng.fork();
        Rng rearRng = m_rng.fork();
        Rng ringRng = m_rng.fork();
        Rng spokeRng = m_rng.fork();

        buildHub(hubRng);
        buildDocking(dockRng);
        buildRear(rearRng);
        for (float z : m_ringZ) {
            buildRing(ringRng, z);
            buildSpokes(spokeRng, z);
        }
    }

private:
    float moduleAngle(int m) const { return kTwoPi * float(m) / float(m_modules); }
    float spokeAngle(int s) const { return moduleAngle(s * m_modulesPerSpoke) + kPi / float(m_modules); }
    float collarHalf() const { return m_spokeR * 2.4f; }

    // -----------------------------------------------------------------------
    // Hub: the port of entry
    // -----------------------------------------------------------------------

    void buildHub(Rng& rng)
    {
        const Profile prof = prism(m_hubSides, m_hubR);
        use(m_b, m_mat.hull);
        m_b.extrude(glm::mat4(1.0f), prof, m_hubZ0, m_hubZ1);

        struct Band {
            float z0, z1;
        };
        std::vector<Band> bands;

        // Collars where the spokes attach
        ExtrudeOptions collar;
        collar.scale0 = collar.scale1 = glm::vec2(1.14f);
        use(m_b, m_mat.hullDark);
        for (float z : m_ringZ) {
            m_b.extrude(glm::mat4(1.0f), prof, z - collarHalf(), z + collarHalf(), collar);
            bands.push_back({z - collarHalf(), z + collarHalf()});
        }

        // End belts and optional accent belts
        ExtrudeOptions belt;
        belt.scale0 = belt.scale1 = glm::vec2(1.06f);
        use(m_b, m_mat.accent);
        m_b.extrude(glm::mat4(1.0f), prof, m_hubZ0 - 0.1f, m_hubZ0 + 1.2f, belt);
        m_b.extrude(glm::mat4(1.0f), prof, m_hubZ1 - 1.2f, m_hubZ1 + 0.1f, belt);
        bands.push_back({m_hubZ0 - 1.0f, m_hubZ0 + 1.2f});
        bands.push_back({m_hubZ1 - 1.2f, m_hubZ1 + 1.0f});
        const int extra = rng.range(0, 2);
        for (int i = 0; i < extra; ++i) {
            const float z = rng.range(m_hubZ0 + 3.0f, m_hubZ1 - 4.0f);
            const float w = rng.range(0.5f, 1.2f);
            const bool clash = std::any_of(bands.begin(), bands.end(),
                                           [&](const Band& b) { return z < b.z1 + 1.0f && z + w > b.z0 - 1.0f; });
            if (clash)
                continue;
            m_b.extrude(glm::mat4(1.0f), prof, z, z + w, belt);
            bands.push_back({z, z + w});
        }

        const float zMid = (m_hubZ0 + m_hubZ1) * 0.5f;
        const int rcsEvery = std::max(1, m_hubSides / 4);
        int index = 0;
        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, m_hubZ0, m_hubZ1, 0.25f)) {
            const Patch& p = fp.patch;
            GreebleSettings s = m_parts.greebleSettings(0.25f, 0.7f);
            for (const Band& b : bands)
                s.keepOut.push_back(zBand(p, zMid, b.z0, b.z1));
            if (index % rcsEvery == 0) {
                for (float v : {-p.halfSize.y + 2.0f, p.halfSize.y - 2.0f}) {
                    m_parts.rcsPod(p.frame * at({0.0f, v, 0.0f}));
                    s.keepOut.push_back(UvRect::around({0.0f, v}, glm::vec2(1.0f)));
                }
            } else if (index % 2 == 1 && rng.chance(0.7f)) {
                m_parts.portholeRow(rng, p, 0.0f, -p.halfSize.y, p.halfSize.y, s.keepOut);
                s.keepOut.push_back({{-0.8f, -1e3f}, {0.8f, 1e3f}});
            }
            greeble(m_b, rng, p, s);
            ++index;
        }
    }

    void buildDocking(Rng& rng)
    {
        const float collarR = m_hubR * rng.range(0.75f, 0.92f);
        const float len = rng.range(5.0f, 8.0f);
        const float zf = m_hubZ1 + len;
        use(m_b, m_mat.hull);
        m_b.cylinder(glm::mat4(1.0f), collarR, m_hubZ1 - 0.2f, zf, 32, false);

        // Front rim ring
        use(m_b, m_mat.accent);
        m_b.cylinder(glm::mat4(1.0f), collarR * 1.05f, zf - 0.6f, zf, 32, false);
        m_b.annulus(glm::mat4(1.0f), collarR, collarR * 1.05f, zf, 32);
        m_b.annulus(glm::mat4(1.0f), collarR, collarR * 1.05f, zf - 0.6f, 32, false);

        // Docking bay: a lit recess in the front face
        const float bayR = collarR * rng.range(0.5f, 0.68f);
        const float depth = rng.range(3.0f, 6.0f);
        use(m_b, m_mat.hull);
        m_b.annulus(glm::mat4(1.0f), bayR, collarR, zf, 32);
        ExtrudeOptions bay;
        bay.shading = Shading::Smooth;
        bay.inward = true;
        bay.cap0 = bay.cap1 = false;
        use(m_b, m_mat.windows, {depth * 1.5f, -1.0f});
        m_b.extrude(glm::mat4(1.0f), profile::circle(bayR, 32), zf - depth, zf, bay);
        use(m_b, m_mat.hullDark);
        m_b.disc(glm::mat4(1.0f), bayR, zf - depth, 32);
        use(m_b, m_mat.greeble);
        m_b.box(at({0, 0, zf - depth + 0.4f}), {bayR * 0.9f, bayR * 0.5f, 0.8f});
        use(m_b, m_mat.thrusterGlow);
        m_b.annulus(glm::mat4(1.0f), bayR, bayR + 0.18f, zf + 0.02f, 32);
        const int lamps = rng.range(12, 20);
        const float lampR = (bayR + collarR) * 0.5f;
        for (int i = 0; i < lamps; ++i) {
            const float a = kTwoPi * float(i) / float(lamps);
            m_parts.light(m_mat.lamp, radial(a) * lampR + kAxis * (zf + 0.1f), 0.25f);
        }

        // Radial docking ports around the collar
        const float portR = rng.range(0.8f, 1.2f);
        const int maxPorts = std::max(2, int(kTwoPi * collarR / (portR * 4.2f)));
        const int ports = std::min(rng.range(4, 8), maxPorts);
        const float phase = rng.range(0.0f, kPi);
        const float zc = m_hubZ1 + len * 0.45f;
        for (int i = 0; i < ports; ++i) {
            const float a = phase + kTwoPi * float(i) / float(ports);
            m_parts.dockingPort(frameAlong(radial(a) * collarR + kAxis * zc, radial(a), kAxis), portR,
                                rng.range(1.2f, 2.6f));
        }

        // Approach light masts guiding ships into the bay
        const int masts = rng.range(2, 4);
        const float mastLen = rng.range(6.0f, 14.0f);
        const float mastR = lampR + 0.5f;
        const float mastPhase = kPi / float(lamps);
        for (int i = 0; i < masts; ++i) {
            const glm::vec3 base = radial(mastPhase + kTwoPi * float(i) / float(masts)) * mastR + kAxis * zf;
            use(m_b, m_mat.truss);
            m_b.beam(glm::mat4(1.0f), base, base + kAxis * mastLen, 0.18f);
            for (float t = 2.0f; t <= mastLen; t += 2.0f)
                m_parts.light(t + 2.0f > mastLen ? m_mat.navWhite : m_mat.lamp, base + kAxis * t, 0.3f);
        }
        m_parts.light(m_mat.navRed, glm::vec3(collarR * 1.08f, 0, zf - 0.3f));
        m_parts.light(m_mat.navGreen, glm::vec3(-collarR * 1.08f, 0, zf - 0.3f));
    }

    // -----------------------------------------------------------------------
    // Rear: service module, mast, solar arrays, dish
    // -----------------------------------------------------------------------

    void buildRear(Rng& rng)
    {
        const float smR = m_hubR * rng.range(0.5f, 0.75f);
        const float smLen = rng.range(5.0f, 10.0f);
        const float z1 = m_hubZ0 + 0.2f, z0 = m_hubZ0 - smLen;
        const Profile prof = prism(m_hubSides, smR);
        use(m_b, m_mat.hullDark);
        m_b.extrude(glm::mat4(1.0f), prof, z0, z1);
        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, z0, z1, 0.2f))
            greeble(m_b, rng, fp.patch, m_parts.greebleSettings(0.3f, 0.5f));

        // Radiator fins at the prism's corners
        if (rng.chance(0.6f)) {
            const int fins = m_hubSides / (rng.chance(0.5f) ? 2 : 1);
            const float span = rng.range(3.0f, 7.0f);
            const float circum = smR / std::cos(kPi / float(m_hubSides));
            for (int i = 0; i < fins; ++i) {
                const float a = kPi / float(m_hubSides) + kTwoPi * float(i) / float(fins);
                const glm::vec3 r = radial(a), t(-std::sin(a), std::cos(a), 0.0f);
                const glm::vec3 c = r * (circum + span * 0.5f - 0.1f) + kAxis * ((z0 + z1) * 0.5f);
                const glm::mat4 f(glm::vec4(r, 0), glm::vec4(t, 0), glm::vec4(kAxis, 0), glm::vec4(c, 1));
                use(m_b, m_mat.greeble, {2.0f, 2.0f});
                m_b.box(f, {span, 0.12f, smLen * 0.8f});
                use(m_b, m_mat.truss);
                m_b.beam(f, {span * 0.5f, 0, -smLen * 0.4f}, {span * 0.5f, 0, smLen * 0.4f}, 0.2f);
            }
        }

        // Mast with solar arrays
        const float half = rng.range(0.6f, 1.0f);
        const float mastLen = rng.range(14.0f, 30.0f);
        const float mz0 = z0 - mastLen;
        m_parts.truss(glm::mat4(1.0f), mz0, z0 + 0.2f, half, half * 0.16f, rng.chance(0.5f));

        const float panelLen = rng.range(10.0f, 20.0f) * std::clamp(m_R / 50.0f, 0.8f, 1.3f);
        const float panelW = rng.range(3.5f, 6.0f);
        int pairs = rng.range(1, 3);
        while (pairs > 1 && mastLen / float(pairs) < panelW + 1.5f)
            --pairs;
        const bool crossed = rng.chance(0.4f);
        for (int i = 0; i < pairs; ++i) {
            const float z = z0 - mastLen * (float(i) + 0.5f) / float(pairs);
            const float rot = crossed && i % 2 == 1 ? kPi * 0.5f : 0.0f;
            for (float side : {-1.0f, 1.0f}) {
                const glm::vec3 dir = radial(rot) * side;
                use(m_b, m_mat.truss);
                m_b.beam(glm::mat4(1.0f), dir * half + kAxis * z, dir * (half + 1.6f) + kAxis * z, 0.3f);
                const glm::vec3 c = dir * (half + 1.6f + panelLen * 0.5f) + kAxis * z;
                const glm::mat4 f(glm::vec4(dir, 0), glm::vec4(kAxis, 0), glm::vec4(glm::cross(dir, kAxis), 0),
                                  glm::vec4(c, 1));
                use(m_b, m_mat.solar);
                m_b.box(f, {panelLen, panelW, 0.12f});
                use(m_b, m_mat.truss);
                for (float y : {-panelW * 0.5f, 0.0f, panelW * 0.5f})
                    m_b.beam(f, {-panelLen * 0.5f, y, 0}, {panelLen * 0.5f, y, 0}, 0.16f);
                m_b.beam(f, {panelLen * 0.5f, -panelW * 0.5f, 0}, {panelLen * 0.5f, panelW * 0.5f, 0}, 0.16f);
            }
        }

        // Dish at the end of the mast
        if (rng.chance(0.75f))
            m_parts.dish(frameAlong(kAxis * (mz0 - 0.4f), -kAxis, glm::vec3(0, 1, 0)), rng.range(2.5f, 5.0f));
        m_parts.light(m_mat.navWhite, kAxis * (mz0 - 0.2f), 0.3f);
    }

    // -----------------------------------------------------------------------
    // Outer ring: living quarters
    // -----------------------------------------------------------------------

    void buildRing(Rng& rng, float z)
    {
        const float a = m_ringH * 0.5f, b = m_ringW * 0.5f;
        const glm::vec2 center(m_R, z);
        Profile prof;
        if (m_torus) {
            for (int i = 0; i < 20; ++i) {
                const float t = kTwoPi * float(i) / 20.0f;
                prof.emplace_back(m_R + a * std::cos(t), z + b * std::sin(t));
            }
        } else {
            for (const glm::vec2& p : profile::chamferedRect(m_ringH, m_ringW, m_ringC))
                prof.push_back(p + center);
        }

        RevolveOptions ring;
        ring.around = m_torus ? Shading::Smooth : Shading::Flat;
        ring.profile = m_torus ? Shading::Smooth : Shading::Flat;
        use(m_b, m_mat.hull);
        m_b.revolve(glm::mat4(1.0f), prof, m_segments, ring);

        // Bulkheads between habitat modules
        const float bulkArc = rng.range(0.6f, 1.0f);
        const float bulkHalf = bulkArc / m_R;
        RevolveOptions bulk = ring;
        bulk.around = Shading::Flat;
        const Profile bulkProf = scaledAbout(prof, center, 1.07f);
        use(m_b, m_mat.hullDark);
        for (int m = 0; m < m_modules; ++m) {
            bulk.angle0 = moduleAngle(m) - bulkHalf;
            bulk.angle1 = moduleAngle(m) + bulkHalf;
            m_b.revolve(glm::mat4(1.0f), bulkProf, m_torus ? 3 : 1, bulk);
        }

        // Greenhouse modules: glazing on the hub-facing side, away from the spokes
        std::vector<bool> greenhouse(std::size_t(m_modules), false);
        const float greenChance = rng.range(0.15f, 0.5f);
        for (int m = 0; m < m_modules; ++m)
            greenhouse[std::size_t(m)] = m % m_modulesPerSpoke != 0 && rng.chance(greenChance);

        if (m_torus)
            buildTorusDetail(rng, z, a, b, greenhouse, bulkHalf);
        else
            buildBoxRingDetail(rng, z, prof, greenhouse, bulkHalf);

        // Marker lights on the rim at the spokes
        for (int s = 0; s < m_spokes; ++s)
            m_parts.light(m_mat.navWhite, radial(spokeAngle(s)) * (m_R + a + 0.3f) + kAxis * z, 0.35f);
    }

    void buildTorusDetail(Rng& rng, float z, float a, float b, const std::vector<bool>& greenhouse, float bulkHalf)
    {
        // Window bands: thin strips hugging the tube's cross-section at angles t. The profile
        // starts on its outer face so the window texture's v spans exactly that face.
        const glm::vec2 c(m_R, z);
        const float bh = rng.range(0.6f, 0.9f);
        const float kOut = 1.0f + 0.05f / std::min(a, b), kIn = 1.0f - 0.05f / std::min(a, b);
        RevolveOptions band;
        band.around = Shading::Smooth;
        for (float t : {0.0f, -0.55f, 0.55f}) {
            const float speed = std::sqrt(a * a * std::sin(t) * std::sin(t) + b * b * std::cos(t) * std::cos(t));
            const float d = bh * 0.5f / speed;
            auto onTube = [&](float k, float s) { return c + k * glm::vec2(a * std::cos(s), b * std::sin(s)); };
            const Profile strip = {onTube(kOut, t - d), onTube(kOut, t + d), onTube(kIn, t + d), onTube(kIn, t - d)};
            use(m_b, m_mat.windows, {bh * 4.0f, glm::length(strip[1] - strip[0])});
            m_b.revolve(glm::mat4(1.0f), strip, m_segments, band);
        }
        // Glazing strips on the hub-facing side of greenhouse modules
        RevolveOptions glass;
        glass.around = Shading::Smooth;
        const Profile strip = {{m_R - a - 0.1f, z - b * 0.45f}, {m_R - a + 0.3f, z - b * 0.45f},
                               {m_R - a + 0.3f, z + b * 0.45f}, {m_R - a - 0.1f, z + b * 0.45f}};
        use(m_b, m_mat.greenhouse);
        for (int m = 0; m < m_modules; ++m) {
            if (!greenhouse[std::size_t(m)])
                continue;
            glass.angle0 = moduleAngle(m) + bulkHalf * 1.6f;
            glass.angle1 = moduleAngle(m + 1) - bulkHalf * 1.6f;
            m_b.revolve(glm::mat4(1.0f), strip, m_segsPerModule * 2, glass);
        }
    }

    void buildBoxRingDetail(Rng& rng, float z, const Profile& prof, const std::vector<bool>& greenhouse,
                            float bulkHalf)
    {
        const float a = m_ringH * 0.5f, b = m_ringW * 0.5f;
        const float outerR = m_R + a;

        // Window rows on the outer wall
        const float bh = rng.range(0.7f, 1.1f);
        const std::vector<float> outerRows = rng.chance(0.5f) ? std::vector<float>{0.0f}
                                                              : std::vector<float>{-(b - m_ringC) * 0.45f,
                                                                                   (b - m_ringC) * 0.45f};
        ExtrudeOptions band;
        band.cap0 = band.cap1 = false;
        use(m_b, m_mat.windows, {bh * 4.0f, -1.0f});
        for (float off : outerRows)
            m_b.extrude(glm::mat4(1.0f), profile::circle(outerR + 0.04f, m_segments), z + off - bh * 0.5f,
                        z + off + bh * 0.5f, band);

        // Window rows on both side walls
        const float bh2 = rng.range(0.8f, 1.3f);
        const float sideSpan = a - m_ringC - bh2 * 0.5f - 0.3f;
        std::vector<float> sideRows;
        if (sideSpan > 0.0f) {
            sideRows.push_back(m_R + sideSpan * rng.range(0.0f, 0.6f));
            if (rng.chance(0.5f))
                sideRows.push_back(m_R - sideSpan * rng.range(0.3f, 0.8f));
        }
        use(m_b, m_mat.windows, {bh2 * 4.0f, -1.0f});
        for (float rc : sideRows)
            for (float s : {-1.0f, 1.0f})
                m_b.annulus(glm::mat4(1.0f), rc - bh2 * 0.5f, rc + bh2 * 0.5f, z + s * (b + 0.03f), m_segments, s > 0.0f);

        // Greenhouse glazing on the inner wall
        RevolveOptions glass;
        glass.around = Shading::Flat;
        const float gb = (b - m_ringC) * 0.85f;
        const Profile strip = {{m_R - a - 0.12f, z - gb}, {m_R - a + 0.05f, z - gb},
                               {m_R - a + 0.05f, z + gb}, {m_R - a - 0.12f, z + gb}};
        use(m_b, m_mat.greenhouse);
        for (int m = 0; m < m_modules; ++m) {
            if (!greenhouse[std::size_t(m)])
                continue;
            glass.angle0 = moduleAngle(m) + bulkHalf * 1.6f;
            glass.angle1 = moduleAngle(m + 1) - bulkHalf * 1.6f;
            m_b.revolve(glm::mat4(1.0f), strip, m_segsPerModule, glass);
        }

        // Greebles on every facet of the faceted ring
        const float segAngle = kTwoPi / float(m_segments);
        const std::size_t n = prof.size();
        for (int k = 0; k < m_segments; ++k) {
            const int module = k / m_segsPerModule;
            const int inModule = k % m_segsPerModule;
            const bool spokeModule = module % m_modulesPerSpoke == 0;
            const float spokeSeg = float(m_segsPerModule) * 0.5f; // spoke sits mid-module
            const bool nearSpoke = spokeModule && std::abs(float(inModule) + 0.5f - spokeSeg) < 1.5f;
            const float a0 = segAngle * float(k), a1 = a0 + segAngle;

            for (std::size_t e = 0; e < n; ++e) {
                const glm::vec2 p0 = prof[e], p1 = prof[(e + 1) % n];
                const glm::vec2 d = p1 - p0;
                const glm::vec2 nrm = glm::normalize(glm::vec2(d.y, -d.x));
                const Patch patch = ringFacet(a0, a1, p0, p1, 0.2f);
                if (patch.halfSize.x <= 0.3f || patch.halfSize.y <= 0.3f)
                    continue;

                GreebleSettings s = m_parts.greebleSettings(0.2f, 0.65f);
                const float bulkU = bulkHalf * std::min(p0.x, p1.x) + 0.35f;
                if (inModule == 0)
                    s.keepOut.push_back({{-1e3f, -1e3f}, {-patch.halfSize.x + bulkU, 1e3f}});
                if (inModule == m_segsPerModule - 1)
                    s.keepOut.push_back({{patch.halfSize.x - bulkU, -1e3f}, {1e3f, 1e3f}});

                if (nrm.x > 0.9f) { // outer wall
                    s.density = 0.12f;
                    for (float off : outerRows)
                        s.keepOut.push_back({{-1e3f, off - bh * 0.5f - 0.3f}, {1e3f, off + bh * 0.5f + 0.3f}});
                } else if (nrm.x < -0.9f) { // inner wall, facing the hub
                    if (greenhouse[std::size_t(module)] || nearSpoke)
                        continue;
                    s.density = 0.25f;
                } else if (std::abs(nrm.y) > 0.9f) { // side walls
                    const float sign = nrm.y > 0.0f ? -1.0f : 1.0f; // +Z wall runs outer -> inner
                    for (float rc : sideRows) {
                        const float v0 = sign * (rc - bh2 * 0.5f - m_R), v1 = sign * (rc + bh2 * 0.5f - m_R);
                        s.keepOut.push_back({{-1e3f, std::min(v0, v1) - 0.3f}, {1e3f, std::max(v0, v1) + 0.3f}});
                    }
                } else {
                    s.density = 0.25f;
                }
                greeble(m_b, rng, patch, s);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Spokes: transit tubes between hub and ring
    // -----------------------------------------------------------------------

    void buildSpokes(Rng& rng, float z)
    {
        const float r0 = m_hubR * 1.14f - 0.3f;
        const float r1 = m_R - m_ringH * 0.5f + 0.4f;
        const int style = rng.range(0, 2); // 0 single tube, 1 twin tubes, 2 tube inside a truss
        const float rs = style == 1 ? m_spokeR * 0.75f : m_spokeR;
        const std::vector<float> offsets = style == 1 ? std::vector<float>{-rs * 1.4f, rs * 1.4f} : std::vector<float>{0.0f};
        const float ringSpacing = rng.range(5.0f, 8.0f);

        for (int s = 0; s < m_spokes; ++s) {
            const glm::vec3 dir = radial(spokeAngle(s));
            const glm::mat4 base = frameAlong(kAxis * z, dir, kAxis); // local Z radial, local Y = station axis

            for (float off : offsets) {
                const glm::mat4 f = base * at({0.0f, off, 0.0f});
                use(m_b, m_mat.hull, {4.0f, 6.0f});
                m_b.cylinder(f, rs, r0, r1, 14, false);
                use(m_b, m_mat.hullDark);
                m_b.cylinder(f, rs * 1.6f, r0, r0 + 1.5f, 14);
                m_b.cylinder(f, rs * 1.6f, r1 - 1.8f, r1, 14);
                for (float r = r0 + 1.5f + ringSpacing; r < r1 - 1.8f - 1.0f; r += ringSpacing)
                    m_b.cylinder(f, rs * 1.22f, r - 0.2f, r + 0.2f, 14);

                // Elevator cars travelling along the spoke
                const int cars = rng.range(0, 2);
                for (int c = 0; c < cars; ++c) {
                    const float t = rng.range(r0 + 4.0f, r1 - 5.0f);
                    use(m_b, m_mat.hullDark);
                    m_b.cylinder(f, rs * 1.5f, t - 1.3f, t + 1.3f, 14);
                    use(m_b, m_mat.windows, {2.8f, -1.0f});
                    m_b.cylinder(f, rs * 1.53f, t - 0.35f, t + 0.35f, 14, false);
                }
            }
            if (style == 2)
                m_parts.truss(base, r0 + 1.5f, r1 - 1.8f, rs * 1.7f, rs * 0.18f, rng.chance(0.5f));

            // Airlock block where the spoke meets the ring
            use(m_b, m_mat.hullDark);
            const float blockY = style == 1 ? rs * 6.0f : rs * 3.4f;
            m_b.box(base * at({0.0f, 0.0f, r1 - 0.7f}), {rs * 4.0f, std::min(blockY, m_ringW * 0.9f), 1.8f});
        }
    }

    Rng m_rng;
    MeshBuilder m_b;
    ShipMaterials m_mat;
    ShipParts m_parts;

    float m_R = 0, m_ringW = 0, m_ringH = 0, m_ringC = 0;
    bool m_torus = false;
    int m_spokes = 0, m_modulesPerSpoke = 0, m_modules = 0, m_segsPerModule = 0, m_segments = 0;
    float m_hubR = 0, m_spokeR = 0;
    int m_hubSides = 8;
    std::vector<float> m_ringZ;
    float m_hubZ0 = 0, m_hubZ1 = 0;
};

} // namespace

ModelData generateWheelStation(std::uint64_t seed, const std::filesystem::path& textureDir)
{
    ModelData model;
    WheelStation station(seed, textureDir, model);
    station.build();
    return model;
}

} // namespace space
