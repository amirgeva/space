#include "procgen/ships/Fighter.h"

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

constexpr glm::vec3 kUp(0.0f, 1.0f, 0.0f);
constexpr glm::vec3 kForward(0.0f, 0.0f, 1.0f);
constexpr float kPi = glm::pi<float>();

float radians(float degrees) { return degrees * kPi / 180.0f; }

enum class EngineLayout { Single, Twin, Nacelles };
enum class WingShape { Swept, Delta, ForwardSwept };

// One piece of the fuselage, tapering linearly from z0 to z1. Scales are relative to
// the base profile; y is the profile's vertical offset.
struct Section {
    float z0, z1;
    glm::vec2 scale0, scale1;
    float y0, y1;
    const Surface* surface;
};

// A trapezoidal plate attached to the fuselage at x = +-rootX. Root and tip chords run
// along Z; u is the distance from the root along the plate.
struct WingPlan {
    float rootX = 0, y = 0;
    float span = 0;
    float rootTrail = 0, rootLead = 0;
    float tipTrail = 0, tipLead = 0;
    float thickness = 0;
    float dihedral = 0; // radians, positive raises the tips
    bool accentTip = false;

    float lead(float u) const { return rootLead + (tipLead - rootLead) * u / span; }
    float trail(float u) const { return rootTrail + (tipTrail - rootTrail) * u / span; }
};

// Belly air intake of the internal engines.
struct IntakePlan {
    float z0 = 0, z1 = 0, width = 0, height = 0;
};

// Rigid frame in the plane of a wing: local X runs from root to tip, local +Z is the
// upper side and local Y is -side * world Z.
glm::mat4 wingFrame(const WingPlan& w, float side)
{
    const glm::vec3 x(side * std::cos(w.dihedral), std::sin(w.dihedral), 0.0f);
    const glm::vec3 y(0.0f, 0.0f, -side);
    return glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(glm::cross(x, y), 0),
                     glm::vec4(side * w.rootX, w.y, 0.0f, 1));
}

// World position on a wing at distance u from the root and world z.
glm::vec3 wingPoint(const WingPlan& w, float side, float u, float z)
{
    const glm::mat4 f = wingFrame(w, side);
    return glm::vec3(f[3]) + glm::vec3(f[0]) * u + kForward * z;
}

// Rigid frame for a fin: local X is world +Z, local Y runs from root to tip. `base`
// has z = 0, so local x equals world z.
glm::mat4 finFrame(const glm::vec3& base, const glm::vec3& heightDir)
{
    return glm::mat4(glm::vec4(kForward, 0), glm::vec4(heightDir, 0), glm::vec4(glm::cross(kForward, heightDir), 0),
                     glm::vec4(base, 1));
}

Profile scaled(const Profile& prof, const glm::vec2& s)
{
    Profile out = prof;
    for (glm::vec2& p : out)
        p *= s;
    return out;
}

class Fighter {
public:
    Fighter(std::uint64_t seed, const std::filesystem::path& textureDir, ModelData& out)
        : m_rng(seed), m_b(out), m_parts(m_b, m_mat)
    {
        Rng livery = m_rng.fork();
        m_mat = ShipMaterials::create(m_b, textureDir, livery);
        // A fighter is a fraction of a freighter's size, so tile the plating finer.
        m_mat.hull.tile = glm::vec2(3.0f);
        m_mat.hullDark.tile = glm::vec2(2.5f);
        m_mat.accent.tile = glm::vec2(3.0f);
        m_mat.greeble.tile = glm::vec2(1.0f);
    }

    void build()
    {
        Rng dims = m_rng.fork();
        m_L = dims.range(14.0f, 22.0f);
        m_W = m_L * dims.range(0.15f, 0.2f);
        m_H = m_W * dims.range(0.55f, 0.75f);
        m_C = m_H * dims.range(0.25f, 0.4f);
        m_zA = m_L * dims.range(0.26f, 0.32f);
        m_zB = m_L * dims.range(0.58f, 0.64f);
        m_zC = m_L * dims.range(0.78f, 0.84f);

        Rng engineRng = m_rng.fork();
        Rng wingRng = m_rng.fork();
        Rng tailRng = m_rng.fork();
        Rng canopyRng = m_rng.fork();
        Rng hullRng = m_rng.fork();
        Rng weaponRng = m_rng.fork();

        planEngines(engineRng);
        planWings(wingRng);
        planTail(tailRng);
        buildCanopy(canopyRng);
        buildFuselage(hullRng);
        buildEngines(engineRng);
        buildWing(m_wing);
        if (m_hasStab)
            buildWing(m_stab);
        buildTail(tailRng);
        buildWeapons(weaponRng);
        buildNavLights();
    }

private:
    // -----------------------------------------------------------------------
    // Planning
    // -----------------------------------------------------------------------

    void planEngines(Rng& rng)
    {
        switch (rng.range(0, 2)) {
        case 0: m_engines = EngineLayout::Single; break;
        case 1: m_engines = EngineLayout::Twin; break;
        default: m_engines = EngineLayout::Nacelles; break;
        }

        switch (m_engines) {
        case EngineLayout::Single:
            m_engineScale = rng.range(1.0f, 1.1f);
            break;
        case EngineLayout::Twin:
            m_engineScale = rng.range(1.1f, 1.25f);
            break;
        case EngineLayout::Nacelles:
            m_engineScale = rng.range(0.9f, 1.0f);
            m_nacelleR = m_H * rng.range(0.3f, 0.4f);
            m_nacelleX = m_W * 0.5f + m_nacelleR * 0.5f;
            m_nacelleY = (m_H * 0.5f - m_C) * rng.range(-0.6f, 0.1f);
            m_nacelleLen = m_L * rng.range(0.38f, 0.5f);
            break;
        }
        m_bodyHalf = m_engines == EngineLayout::Nacelles ? m_nacelleX + m_nacelleR : m_W * m_engineScale * 0.5f;

        if (m_engines != EngineLayout::Nacelles) {
            // Keep the intake on the flat bottom so its top edge stays buried in the hull.
            m_intake.width = (m_W - 2.0f * m_C) * rng.range(0.4f, 0.55f);
            m_intake.height = m_H * rng.range(0.25f, 0.38f);
            m_intake.z1 = m_zA + m_L * rng.range(0.1f, 0.18f);
            m_intake.z0 = m_intake.z1 - m_L * rng.range(0.14f, 0.2f);
        }
    }

    void planWings(Rng& rng)
    {
        const float sideHalf = m_H * 0.5f - m_C;
        const float r = rng.unit();
        m_wingShape = r < 0.45f ? WingShape::Swept : r < 0.85f ? WingShape::Delta : WingShape::ForwardSwept;

        float chord = 0, sweep = 0, taper = 0;
        switch (m_wingShape) {
        case WingShape::Swept:
            m_wing.span = m_W * rng.range(1.8f, 2.6f);
            chord = m_L * rng.range(0.28f, 0.38f);
            sweep = rng.range(25.0f, 45.0f);
            taper = rng.range(0.3f, 0.5f);
            m_wing.rootTrail = m_L * rng.range(0.12f, 0.2f);
            break;
        case WingShape::Delta:
            m_wing.span = m_W * rng.range(1.5f, 2.1f);
            chord = m_L * rng.range(0.45f, 0.55f);
            sweep = rng.range(50.0f, 62.0f);
            taper = rng.range(0.06f, 0.12f);
            m_wing.rootTrail = m_L * rng.range(0.04f, 0.1f);
            break;
        case WingShape::ForwardSwept:
            m_wing.span = m_W * rng.range(1.9f, 2.5f);
            chord = m_L * rng.range(0.24f, 0.32f);
            sweep = -rng.range(15.0f, 25.0f);
            taper = rng.range(0.35f, 0.55f);
            m_wing.rootTrail = m_L * rng.range(0.1f, 0.16f);
            break;
        }
        m_wing.rootX = m_W * 0.3f;
        m_wing.y = m_engines == EngineLayout::Nacelles ? m_nacelleY : sideHalf * rng.range(-0.8f, 0.5f);
        m_wing.rootLead = m_wing.rootTrail + chord;
        m_wing.tipLead = m_wing.rootLead - m_wing.span * std::tan(radians(sweep));
        m_wing.tipTrail = m_wing.tipLead - chord * taper;
        m_wing.thickness = rng.range(0.14f, 0.24f);
        m_wing.dihedral = radians(rng.range(-8.0f, 12.0f));
        m_wing.accentTip = rng.chance(0.6f);

        // Tail planes: not on deltas, and not where the nacelles would cut through them
        m_hasStab = m_wingShape != WingShape::Delta && m_engines != EngineLayout::Nacelles && rng.chance(0.6f);
        if (m_hasStab) {
            const float stabChord = m_L * rng.range(0.12f, 0.18f);
            m_stab.rootX = m_W * 0.25f;
            m_stab.y = m_wing.y > 0.0f ? m_wing.y - m_H * 0.3f : m_wing.y + m_H * 0.3f;
            m_stab.span = m_wing.span * rng.range(0.35f, 0.5f);
            m_stab.rootTrail = m_L * rng.range(0.0f, 0.03f);
            m_stab.rootLead = m_stab.rootTrail + stabChord;
            m_stab.tipLead = m_stab.rootLead - m_stab.span * std::tan(radians(rng.range(30.0f, 45.0f)));
            m_stab.tipTrail = m_stab.tipLead - stabChord * rng.range(0.4f, 0.6f);
            m_stab.thickness = m_wing.thickness * 0.8f;
            m_stab.dihedral = radians(rng.range(-5.0f, 15.0f));
            m_stab.accentTip = m_wing.accentTip;
        }
    }

    void planTail(Rng& rng)
    {
        m_twinFins = rng.chance(0.5f);
        m_finZ0 = m_L * rng.range(0.0f, 0.04f);
        m_finChord = m_L * rng.range(0.18f, 0.26f);
        m_finH = m_H * rng.range(0.9f, 1.5f);
        m_finSweep = radians(rng.range(35.0f, 55.0f));
        m_finTaper = rng.range(0.3f, 0.5f);
        m_finThickness = rng.range(0.1f, 0.16f);
        m_finCant = m_twinFins ? radians(rng.range(12.0f, 30.0f)) : 0.0f;
        m_finX = m_twinFins ? (m_W * 0.5f - m_C) * 0.7f : 0.0f;
        m_ventralFins = rng.chance(0.35f);
        m_finAccent = rng.chance(0.5f);
    }

    // -----------------------------------------------------------------------
    // Canopy and dorsal fairing, at the front of the mid section
    // -----------------------------------------------------------------------

    void buildCanopy(Rng& rng)
    {
        const float topY = m_H * 0.5f - 0.04f;
        const float cw = std::min((m_W - 2.0f * m_C) * 0.9f, m_W * 0.55f);
        const float ch = m_H * rng.range(0.32f, 0.48f);
        const float cl = m_L * rng.range(0.15f, 0.21f);
        const float fl = m_L * rng.range(0.12f, 0.26f);
        const float zKink = m_zB - cl * rng.range(0.35f, 0.5f);
        const Profile arch = profile::arch(cw, ch, 14);
        const glm::mat4 base = at({0.0f, topY, 0.0f});

        // Glass: a straight rear part and a windscreen sloping down to the nose
        use(m_b, m_mat.porthole);
        ExtrudeOptions glass;
        glass.shading = Shading::Smooth;
        glass.cap1 = false;
        m_b.extrude(base, arch, m_zB - cl, zKink, glass);
        ExtrudeOptions screen;
        screen.shading = Shading::Smooth;
        screen.cap0 = false;
        screen.scale1 = {0.35f, 0.3f};
        m_b.extrude(base, arch, zKink, m_zB, screen);

        // Frame across the windscreen joint
        ExtrudeOptions frame;
        frame.shading = Shading::Smooth;
        frame.scale0 = frame.scale1 = glm::vec2(1.04f);
        use(m_b, m_mat.hullDark);
        m_b.extrude(base, arch, zKink - 0.06f, zKink + 0.06f, frame);

        // Fairing behind the canopy, overlapping the glass to frame its rear edge
        ExtrudeOptions fairing;
        fairing.shading = Shading::Smooth;
        fairing.scale0 = {rng.range(0.35f, 0.6f), rng.range(0.1f, 0.3f)};
        fairing.scale1 = glm::vec2(1.04f);
        use(m_b, m_mat.hull);
        m_b.extrude(base, arch, m_zB - cl - fl, m_zB - cl + 0.12f, fairing);

        m_canopyHalfW = cw * 0.52f;
        m_fairingZ0 = m_zB - cl - fl;
    }

    // -----------------------------------------------------------------------
    // Fuselage
    // -----------------------------------------------------------------------

    void buildFuselage(Rng& rng)
    {
        const Profile prof = profile::chamferedRect(m_W, m_H, m_C);
        const glm::vec2 eng(m_engineScale, 1.0f);
        const glm::vec2 one(1.0f);
        const float tLen = m_L * 0.05f;
        const glm::vec2 fwd(rng.range(0.82f, 0.92f), rng.range(0.82f, 0.92f));
        const float yC = -m_H * rng.range(0.0f, 0.1f);
        const glm::vec2 tip(rng.range(0.1f, 0.2f), rng.range(0.1f, 0.18f));
        const float yTip = yC - m_H * rng.range(0.0f, 0.12f);

        std::vector<Section> sections = {
            {0.0f, m_zA - tLen, eng, eng, 0.0f, 0.0f, &m_mat.hull},
            {m_zA - tLen, m_zA, eng, one, 0.0f, 0.0f, &m_mat.hull},
            {m_zA, m_zB, one, one, 0.0f, 0.0f, &m_mat.hull},
            {m_zB, m_zC, one, fwd, 0.0f, yC, &m_mat.hull},
        };
        if (rng.chance(0.5f)) {
            // Accent-colored nose tip, split off the same cone
            const float t = 0.65f;
            const float zs = m_zC + (m_L - m_zC) * t;
            const glm::vec2 s = glm::mix(fwd, tip, t);
            const float ys = glm::mix(yC, yTip, t);
            sections.push_back({m_zC, zs, fwd, s, yC, ys, &m_mat.hull});
            sections.push_back({zs, m_L, s, tip, ys, yTip, &m_mat.accent});
        } else {
            sections.push_back({m_zC, m_L, fwd, tip, yC, yTip, &m_mat.hull});
        }

        for (std::size_t i = 0; i < sections.size(); ++i) {
            const Section& s = sections[i];
            ExtrudeOptions o;
            o.scale0 = s.scale0;
            o.scale1 = s.scale1;
            o.offset1 = {0.0f, s.y1 - s.y0};
            o.cap0 = i == 0;
            o.cap1 = i + 1 == sections.size();
            use(m_b, *s.surface);
            m_b.extrude(at({0.0f, s.y0, 0.0f}), prof, s.z0, s.z1, o);
        }

        // Sensor probe on the nose
        use(m_b, m_mat.truss);
        m_b.cylinder(at({0.0f, yTip, 0.0f}), 0.035f, m_L - 0.2f, m_L + m_L * rng.range(0.03f, 0.08f), 6);

        const bool trim = rng.chance(0.6f);
        decorateSection(rng, scaled(prof, eng), 0.0f, m_zA - tLen, false);
        decorateSection(rng, prof, m_zA, m_zB, trim);

        if (m_engines != EngineLayout::Nacelles)
            buildIntake();
    }

    // Surface detail on a straight fuselage section: small greebles on the top, bottom
    // and chamfers, panel-line relief and optional accent trim on the sides.
    void decorateSection(Rng& rng, const Profile& prof, float z0, float z1, bool trim)
    {
        const float zMid = (z0 + z1) * 0.5f;
        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, z0, z1, 0.15f)) {
            const Patch& p = fp.patch;
            std::vector<UvRect> keepOut;
            switch (fp.face) {
            case Face::Top: {
                keepOut.push_back({{-m_canopyHalfW - 0.3f, m_fairingZ0 - zMid - 0.3f}, {m_canopyHalfW + 0.3f, 1e3f}});
                keepOut.push_back(zBand(p, zMid, -1.0f, m_finZ0 + m_finChord + 0.3f));
                GreebleSettings s = m_parts.greebleSettings(0.18f, 0.3f);
                s.keepOut = keepOut;
                greeble(m_b, rng, p, s);
                break;
            }
            case Face::Bottom: {
                if (m_engines != EngineLayout::Nacelles)
                    keepOut.push_back({{-m_intake.width * 0.5f - 0.3f, m_intake.z0 - zMid - 0.3f},
                                       {m_intake.width * 0.5f + 0.3f, m_intake.z1 - zMid + 0.3f}});
                if (m_ventralFins)
                    keepOut.push_back(zBand(p, zMid, -1.0f, m_finZ0 + m_finChord * 0.6f + 0.3f));
                GreebleSettings s = m_parts.greebleSettings(0.22f, 0.3f);
                s.keepOut = keepOut;
                greeble(m_b, rng, p, s);
                break;
            }
            case Face::Side:
                circuitTraces(m_b, rng, p, m_mat.hull, int((z1 - z0) * 0.6f), 0.05f);
                if (trim && p.halfSize.x > 0.2f) {
                    use(m_b, m_mat.accent);
                    m_b.box(p.frame * at({p.halfSize.x * 0.5f, 0.0f, 0.03f}), {0.18f, p.halfSize.y * 2.0f, 0.06f});
                }
                break;
            case Face::Chamfer:
                greeble(m_b, rng, p, m_parts.greebleSettings(0.15f, 0.25f));
                break;
            }
        }
    }

    void buildIntake()
    {
        const IntakePlan& in = m_intake;
        const glm::mat4 f = at({0.0f, -m_H * 0.5f - in.height * 0.1f, 0.0f});
        const Profile prof = profile::chamferedRect(in.width, in.height, std::min(in.width, in.height) * 0.3f);

        // Tapers into the belly towards the rear
        ExtrudeOptions body;
        body.scale0 = {0.7f, 0.3f};
        use(m_b, m_mat.hull);
        m_b.extrude(f, prof, in.z0, in.z1 - 0.12f, body);
        ExtrudeOptions lip;
        lip.scale0 = lip.scale1 = glm::vec2(1.05f);
        use(m_b, m_mat.hullDark);
        m_b.extrude(f, prof, in.z1 - 0.12f, in.z1, lip);
        use(m_b, m_mat.porthole);
        m_b.polygon(f, prof, in.z1 + 0.01f, true, glm::vec2(0.82f));
    }

    // -----------------------------------------------------------------------
    // Engines
    // -----------------------------------------------------------------------

    void buildEngines(Rng& rng)
    {
        if (m_engines == EngineLayout::Nacelles) {
            buildNacelles(rng);
            return;
        }

        const float hw = m_W * m_engineScale * 0.5f, hh = m_H * 0.5f;
        auto inside = [&](const glm::vec2& p, float r) {
            const float m = 0.12f;
            return std::abs(p.x) + r <= hw - m && std::abs(p.y) + r <= hh - m &&
                   std::abs(p.x) + std::abs(p.y) + r * 1.4142f <= hw + hh - m_C - m;
        };

        const bool single = m_engines == EngineLayout::Single;
        float R = std::min(hw, hh) * (single ? 0.78f : 0.5f) / 1.12f;
        std::vector<glm::vec2> centers;
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (single)
                centers = {{0.0f, 0.0f}};
            else
                centers = {{-hw * 0.48f, 0.0f}, {hw * 0.48f, 0.0f}};
            if (std::all_of(centers.begin(), centers.end(), [&](const glm::vec2& c) { return inside(c, R * 1.12f); }))
                break;
            R *= 0.92f;
        }

        const bool rims = rng.chance(0.5f);
        for (const glm::vec2& c : centers)
            m_parts.nozzle(frameAlong({c, 0.0f}, -kForward, kUp), R, m_mat.engineGlow, rims);

        // Small maneuvering thrusters in the corners of the tail
        if (rng.chance(0.6f)) {
            const float r = R * 0.22f;
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f}) {
                    const glm::vec2 c(sx * (hw - r * 1.12f - 0.2f), sy * (hh - r * 1.12f - 0.2f));
                    if (inside(c, r * 1.12f) &&
                        std::all_of(centers.begin(), centers.end(),
                                    [&](const glm::vec2& m) { return glm::length(c - m) > R * 1.12f + r * 1.12f; }))
                        m_parts.nozzle(frameAlong({c, 0.0f}, -kForward, kUp), r, m_mat.thrusterGlow, true);
                }
        }
    }

    void buildNacelles(Rng& rng)
    {
        const float r = m_nacelleR, z1 = m_nacelleLen;
        const bool rims = rng.chance(0.5f);
        for (float side : {-1.0f, 1.0f}) {
            const glm::mat4 f = at({side * m_nacelleX, m_nacelleY, 0.0f});
            use(m_b, m_mat.hull);
            m_b.cylinder(f, r, 0.0f, z1, 20, false);

            // Rear frame ring, intake lip with a dark throat and a center cone
            use(m_b, m_mat.hullDark);
            m_b.cylinder(f, r * 1.06f, -0.1f, 0.5f, 20);
            m_b.frustum(f, r, r * 0.92f, z1, z1 + 0.35f, 20, false);
            for (float z : {z1 * 0.45f, z1 * 0.75f})
                m_b.cylinder(f, r * 1.03f, z - 0.08f, z + 0.08f, 20, false);
            use(m_b, m_mat.porthole);
            m_b.disc(f, r * 0.92f, z1 + 0.3f, 20);
            use(m_b, m_mat.greeble);
            m_b.frustum(f, r * 0.4f, 0.04f, z1 + 0.2f, z1 + 0.2f + r * 0.6f, 12);

            m_parts.nozzle(frameAlong(glm::vec3(f[3]), -kForward, kUp), r * 0.73f, m_mat.engineGlow, rims);
        }

        // The fuselage tail carries a small auxiliary thruster
        const float hw = m_W * m_engineScale * 0.5f, hh = m_H * 0.5f;
        Patch rear{frameAlong({0.0f, 0.0f, 0.0f}, -kForward, kUp), {hw - m_C * 0.6f, hh - m_C * 0.6f}};
        const float ra = std::min(hw, hh) * 0.3f;
        GreebleSettings s = m_parts.greebleSettings(0.4f, 0.25f);
        s.keepOut.push_back(UvRect::around({0.0f, 0.0f}, glm::vec2(ra * 1.3f)));
        greeble(m_b, rng, rear, s);
        m_parts.nozzle(rear.frame, ra, m_mat.thrusterGlow, true);
    }

    // -----------------------------------------------------------------------
    // Wings, tail planes and fins
    // -----------------------------------------------------------------------

    // Thin plate with beveled edges; `outline` is a convex polygon in frame's XY plane.
    void plate(const glm::mat4& frame, Profile outline, float thickness)
    {
        float area = 0.0f;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (std::size_t i = 0; i < outline.size(); ++i) {
            const glm::vec2 a = outline[i], b = outline[(i + 1) % outline.size()];
            area += a.x * b.y - b.x * a.y;
            lo = glm::min(lo, a);
            hi = glm::max(hi, a);
        }
        if (area < 0.0f)
            std::reverse(outline.begin(), outline.end());
        const glm::vec2 center = (lo + hi) * 0.5f;
        for (glm::vec2& p : outline)
            p -= center;

        const float bevel = thickness * 0.8f;
        const glm::vec2 inset(std::max(0.5f, 1.0f - 2.0f * bevel / (hi.x - lo.x)),
                              std::max(0.5f, 1.0f - 2.0f * bevel / (hi.y - lo.y)));
        const glm::mat4 xf = frame * at({center, 0.0f});
        ExtrudeOptions lower;
        lower.scale0 = inset;
        lower.cap1 = false;
        m_b.extrude(xf, outline, -thickness * 0.5f, 0.0f, lower);
        ExtrudeOptions upper;
        upper.scale1 = inset;
        upper.cap0 = false;
        m_b.extrude(xf, outline, 0.0f, thickness * 0.5f, upper);
    }

    void buildWing(const WingPlan& w)
    {
        const float split = w.accentTip ? w.span * 0.8f : w.span;
        for (float side : {-1.0f, 1.0f}) {
            const glm::mat4 f = wingFrame(w, side);
            auto pt = [&](float u, float z) { return glm::vec2(u, -side * z); };
            use(m_b, m_mat.hull);
            plate(f, {pt(0.0f, w.rootTrail), pt(0.0f, w.rootLead), pt(split, w.lead(split)), pt(split, w.trail(split))},
                  w.thickness);
            if (w.accentTip) {
                use(m_b, m_mat.accent);
                plate(f, {pt(split, w.trail(split)), pt(split, w.lead(split)), pt(w.span, w.tipLead), pt(w.span, w.tipTrail)},
                      w.thickness);
            }
        }
    }

    // A swept fin rising along the frame's local Y; local X is world z.
    void fin(const glm::mat4& f, float chord, float height, bool light)
    {
        const float root = -0.25f; // buried below the hull surface
        const float tipLead = m_finZ0 + chord - height * std::tan(m_finSweep);
        const float tipTrail = tipLead - chord * m_finTaper;
        auto lead = [&](float v) { return glm::mix(m_finZ0 + chord, tipLead, (v - root) / (height - root)); };
        auto trail = [&](float v) { return glm::mix(m_finZ0, tipTrail, (v - root) / (height - root)); };

        const float split = m_finAccent ? height * 0.7f : height;
        use(m_b, m_mat.hull);
        plate(f, {{m_finZ0, root}, {m_finZ0 + chord, root}, {lead(split), split}, {trail(split), split}}, m_finThickness);
        if (m_finAccent) {
            use(m_b, m_mat.accent);
            plate(f, {{trail(split), split}, {lead(split), split}, {tipLead, height}, {tipTrail, height}}, m_finThickness);
        }
        if (light)
            m_parts.light(m_mat.navWhite, glm::vec3(f * glm::vec4((tipLead + tipTrail) * 0.5f, height + 0.08f, 0.0f, 1.0f)),
                          0.12f);
    }

    void buildTail(Rng& rng)
    {
        const float topY = m_H * 0.5f - 0.05f;
        if (m_twinFins) {
            for (float side : {-1.0f, 1.0f}) {
                const glm::vec3 h(side * std::sin(m_finCant), std::cos(m_finCant), 0.0f);
                fin(finFrame({side * m_finX, topY, 0.0f}, h), m_finChord, m_finH, true);
            }
        } else {
            fin(finFrame({0.0f, topY, 0.0f}, kUp), m_finChord, m_finH, true);
        }

        if (m_ventralFins) {
            const float cant = radians(rng.range(20.0f, 35.0f));
            for (float side : {-1.0f, 1.0f}) {
                const glm::vec3 h(side * std::sin(cant), -std::cos(cant), 0.0f);
                const glm::vec3 base(side * (m_W * 0.5f - m_C) * 0.8f, -m_H * 0.5f + 0.05f, 0.0f);
                fin(finFrame(base, h), m_finChord * 0.6f, m_finH * 0.45f, false);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Weapons
    // -----------------------------------------------------------------------

    void cannon(const glm::vec3& p, float length)
    {
        use(m_b, m_mat.hullDark);
        m_b.cylinder(at(p), 0.14f, -0.6f, 0.3f, 10);
        use(m_b, m_mat.truss);
        m_b.cylinder(at(p), 0.05f, 0.3f, 0.3f + length, 8);
        use(m_b, m_mat.hullDark);
        m_b.cylinder(at(p), 0.08f, 0.1f + length, 0.3f + length, 8);
    }

    void missile(const glm::vec3& c, float length, float r)
    {
        const float nose = r * 3.0f;
        const glm::mat4 f = at(c);
        use(m_b, m_mat.hull);
        m_b.cylinder(f, r, -length * 0.5f, length * 0.5f - nose, 10);
        use(m_b, m_mat.accent);
        m_b.frustum(f, r, r * 0.15f, length * 0.5f - nose, length * 0.5f, 10);
        use(m_b, m_mat.hullDark);
        for (int k = 0; k < 4; ++k) {
            const float a = kPi * 0.25f + float(k) * kPi * 0.5f;
            const glm::vec2 d(std::cos(a), std::sin(a));
            m_b.beam(f, glm::vec3(d * r, -length * 0.5f + r * 3.0f), glm::vec3(d * r * 2.6f, -length * 0.5f + 0.05f), 0.03f);
        }
    }

    void buildWeapons(Rng& rng)
    {
        const WingPlan& w = m_wing;
        // Usable span: clear of the fuselage (and nacelles) out to near the tip
        const float uMin = std::min(m_bodyHalf - w.rootX + 0.35f, w.span * 0.7f);
        const float uMax = w.span * 0.85f;

        // Cannons on the leading edge
        const int cannons = rng.range(1, 2);
        const float barrel = m_L * rng.range(0.06f, 0.12f);
        for (float side : {-1.0f, 1.0f})
            for (int i = 0; i < cannons; ++i) {
                const float u = glm::mix(uMin, uMax, cannons == 1 ? 0.2f : (i == 0 ? 0.1f : 0.5f));
                cannon(wingPoint(w, side, u, w.lead(u) - 0.5f), barrel);
            }

        // Missiles on pylons under the wings
        if (rng.chance(0.65f) && uMax > uMin + 0.6f) {
            const int count = rng.range(1, 2);
            const float r = rng.range(0.09f, 0.14f);
            for (float side : {-1.0f, 1.0f}) {
                const glm::vec3 down = -glm::vec3(wingFrame(w, side)[2]);
                for (int i = 0; i < count; ++i) {
                    const float u = glm::mix(uMin, uMax, count == 1 ? 0.65f : (i == 0 ? 0.4f : 0.9f));
                    const float chord = w.lead(u) - w.trail(u);
                    const float length = std::min(chord * 0.9f, m_L * 0.2f);
                    const float zc = (w.lead(u) + w.trail(u)) * 0.5f;
                    const glm::vec3 top = wingPoint(w, side, u, zc) + down * (w.thickness * 0.5f - 0.02f);
                    const glm::vec3 c = top + down * (0.14f + r);
                    use(m_b, m_mat.hullDark);
                    for (float dz : {-length * 0.2f, length * 0.2f})
                        m_b.beam(glm::mat4(1.0f), top + kForward * dz, c - down * r * 0.8f + kForward * dz, 0.06f);
                    missile(c, length, r);
                }
            }
        }

        // Wingtip rails
        m_tipRails = rng.chance(0.4f);
        if (m_tipRails) {
            const float r = 0.08f;
            const float chord = w.tipLead - w.tipTrail;
            for (float side : {-1.0f, 1.0f}) {
                const glm::vec3 out = glm::vec3(wingFrame(w, side)[0]);
                const glm::vec3 c = wingPoint(w, side, w.span, (w.tipLead + w.tipTrail) * 0.5f) + out * (r + 0.02f);
                missile(c, std::max(chord * 1.2f, m_L * 0.1f), r);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Navigation lights: red to port (+X), green to starboard (-X)
    // -----------------------------------------------------------------------

    void buildNavLights()
    {
        const WingPlan& w = m_wing;
        const float out = m_tipRails ? 0.3f : 0.08f;
        for (float side : {-1.0f, 1.0f})
            m_parts.light(side > 0.0f ? m_mat.navRed : m_mat.navGreen,
                          wingPoint(w, side, w.span + out, (w.tipLead + w.tipTrail) * 0.5f), 0.12f);
        m_parts.light(m_mat.navWhite, {0.0f, -m_H * 0.5f - 0.05f, m_zA}, 0.1f);
    }

    Rng m_rng;
    MeshBuilder m_b;
    ShipMaterials m_mat;
    ShipParts m_parts;

    float m_L = 0, m_W = 0, m_H = 0, m_C = 0;
    float m_zA = 0, m_zB = 0, m_zC = 0; // engine / mid / forward / nose section boundaries

    EngineLayout m_engines = EngineLayout::Single;
    float m_engineScale = 1.0f;
    float m_nacelleR = 0, m_nacelleX = 0, m_nacelleY = 0, m_nacelleLen = 0;
    float m_bodyHalf = 0; // half width of the fuselage including nacelles
    IntakePlan m_intake;

    WingShape m_wingShape = WingShape::Swept;
    WingPlan m_wing, m_stab;
    bool m_hasStab = false;
    bool m_tipRails = false;

    bool m_twinFins = false, m_ventralFins = false, m_finAccent = false;
    float m_finZ0 = 0, m_finChord = 0, m_finH = 0, m_finSweep = 0, m_finTaper = 0, m_finThickness = 0;
    float m_finCant = 0, m_finX = 0;

    float m_canopyHalfW = 0, m_fairingZ0 = 0;
};

} // namespace

ModelData generateFighter(std::uint64_t seed, const std::filesystem::path& textureDir)
{
    ModelData model;
    Fighter fighter(seed, textureDir, model);
    fighter.build();
    return model;
}

} // namespace space
