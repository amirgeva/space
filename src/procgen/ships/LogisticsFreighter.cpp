#include "procgen/ships/LogisticsFreighter.h"

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

struct Interval {
    float z0, z1;
};

struct DockingPortPlan {
    float z;
    float radius;
    float length;
};

struct BayPlan {
    float x, z, width, length, height;
};

class Freighter {
public:
    Freighter(std::uint64_t seed, const std::filesystem::path& textureDir, ModelData& out)
        : m_rng(seed), m_b(out), m_parts(m_b, m_mat)
    {
        Rng livery = m_rng.fork();
        m_mat = ShipMaterials::create(m_b, textureDir, livery);
    }

    void build()
    {
        Rng dims = m_rng.fork();
        m_W = dims.range(14.0f, 20.0f);
        m_H = m_W * dims.range(0.7f, 0.95f);
        m_C = std::min(m_W, m_H) * dims.range(0.12f, 0.28f);
        m_L = dims.range(36.0f, 56.0f);

        m_engW = m_W * dims.range(0.8f, 1.05f);
        m_engH = m_H * dims.range(0.95f, 1.2f);
        m_engC = std::min(m_engW, m_engH) * dims.range(0.18f, 0.33f);
        m_engLen = dims.range(12.0f, 20.0f);
        m_neckLen = dims.range(3.0f, 7.0f);
        m_zEng1 = -m_neckLen;
        m_zEng0 = m_zEng1 - m_engLen;
        m_cargoLen = dims.range(45.0f, 90.0f);

        Rng hullRng = m_rng.fork();
        Rng towerRng = m_rng.fork();
        Rng neckRng = m_rng.fork();
        Rng engineRng = m_rng.fork();
        Rng cargoRng = m_rng.fork();
        Rng bowRng = m_rng.fork();

        planHull(hullRng);
        planTower(towerRng);
        buildHull(hullRng);
        buildTower(towerRng);
        buildNeck(neckRng);
        buildEngine(engineRng);
        buildCargo(cargoRng);
        buildBow(bowRng);
        buildNavLights();
    }

private:
    // -----------------------------------------------------------------------
    // Main hull
    // -----------------------------------------------------------------------

    void planHull(Rng& rng)
    {
        m_belts.push_back({-0.2f, rng.range(1.5f, 4.0f)});
        m_belts.push_back({m_L - rng.range(1.5f, 5.0f), m_L + 0.2f});
        const int midBelts = rng.range(0, 2);
        for (int i = 0; i < midBelts; ++i) {
            const float z = rng.range(m_L * 0.3f, m_L * 0.8f);
            m_belts.push_back({z, z + rng.range(0.6f, 1.6f)});
        }

        const int ports = rng.range(0, 2);
        for (int i = 0; i < ports; ++i) {
            const float z = m_L * (float(i) + 1.0f) / float(ports + 1) + rng.range(-3.0f, 3.0f);
            m_ports.push_back({z, rng.range(1.0f, 1.6f), rng.range(1.2f, 2.6f)});
        }

        const int bays = rng.range(1, 3);
        const float bottomHalf = m_W * 0.5f - m_C;
        for (int i = 0; i < bays; ++i) {
            BayPlan bay;
            bay.width = rng.range(0.25f, 0.55f) * m_W;
            bay.length = rng.range(4.0f, 10.0f);
            bay.height = rng.range(1.2f, 2.8f);
            bay.x = rng.range(-1.0f, 1.0f) * std::max(0.0f, bottomHalf - bay.width * 0.5f - 0.4f);
            bay.z = rng.range(bay.length * 0.5f + 4.0f, m_L - bay.length * 0.5f - 5.0f);
            bool clash = false;
            for (const BayPlan& o : m_bays)
                clash |= std::abs(o.z - bay.z) < (o.length + bay.length) * 0.5f + 1.0f &&
                         std::abs(o.x - bay.x) < (o.width + bay.width) * 0.5f + 1.0f;
            if (!clash)
                m_bays.push_back(bay);
        }
    }

    void planTower(Rng& rng)
    {
        // Keep the tower on the flat part of the roof, clear of the chamfers.
        m_towerW = std::min(m_W * rng.range(0.42f, 0.6f), (m_W - 2.0f * m_C) * 0.92f);
        m_towerD = std::min(rng.range(6.0f, 11.0f), m_L * 0.3f);
        const float lo = m_belts[0].z1 + m_towerD * 0.5f + 1.0f;
        const float hi = m_belts[1].z0 - m_towerD * 0.5f - 1.0f;
        m_towerZ = lo + (hi - lo) * rng.range(0.1f, 0.45f);
    }

    void buildHull(Rng& rng)
    {
        const Profile prof = profile::chamferedRect(m_W, m_H, m_C);
        use(m_b, m_mat.hull);
        m_b.extrude(glm::mat4(1.0f), prof, 0.0f, m_L);

        // Armor belts: slightly oversized bands around the hull
        const glm::vec2 beltScale((m_W + 0.5f) / m_W, (m_H + 0.5f) / m_H);
        for (std::size_t i = 0; i < m_belts.size(); ++i) {
            ExtrudeOptions o;
            o.scale0 = o.scale1 = i < 2 ? beltScale : (beltScale + 1.0f) * 0.5f;
            use(m_b, i < 2 ? m_mat.hullDark : m_mat.accent);
            m_b.extrude(glm::mat4(1.0f), prof, m_belts[i].z0, m_belts[i].z1, o);
        }

        const float zMid = m_L * 0.5f;
        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, 0.0f, m_L, 0.25f)) {
            const Patch& p = fp.patch;
            std::vector<UvRect> keepOut;
            for (const Interval& belt : m_belts)
                keepOut.push_back(zBand(p, zMid, belt.z0, belt.z1));

            switch (fp.face) {
            case Face::Top: {
                keepOut.push_back(UvRect::around({0.0f, m_towerZ - zMid}, {m_towerW * 0.5f + 0.6f, m_towerD * 0.5f + 0.6f}));
                GreebleSettings s = m_parts.greebleSettings(0.22f);
                s.keepOut = keepOut;
                greeble(m_b, rng, p, s);
                break;
            }
            case Face::Bottom: {
                for (const BayPlan& bay : m_bays)
                    keepOut.push_back(UvRect::around({bay.x, bay.z - zMid}, {bay.width * 0.5f + 0.4f, bay.length * 0.5f + 0.4f}));
                GreebleSettings s = m_parts.greebleSettings(0.25f);
                s.keepOut = keepOut;
                greeble(m_b, rng, p, s);
                break;
            }
            case Face::Side:
                buildHullSide(rng, fp, keepOut);
                break;
            case Face::Chamfer: {
                GreebleSettings s = m_parts.greebleSettings(0.3f, 0.8f);
                s.keepOut = keepOut;
                greeble(m_b, rng, p, s);
                break;
            }
            }
        }

        // Front face around the cargo attachment
        {
            Patch front{at({0, 0, m_L + 0.2f}), {m_W * 0.5f - m_C * 0.6f, m_H * 0.5f - m_C * 0.6f}};
            GreebleSettings s = m_parts.greebleSettings(0.25f, 0.7f);
            s.keepOut.push_back(UvRect::around({0, 0}, {m_W * 0.36f, m_H * 0.36f}));
            greeble(m_b, rng, front, s);
        }

        // Underside bays: boxy hangar / cargo modules with a lit opening
        for (const BayPlan& bay : m_bays) {
            const float y = -m_H * 0.5f - bay.height * 0.5f + 0.1f;
            use(m_b, m_mat.hullDark);
            m_b.box(at({bay.x, y, bay.z}), {bay.width, bay.height, bay.length});
            use(m_b, m_mat.windows, {bay.height * 0.5f * 4.0f, -1.0f});
            m_b.box(at({bay.x, y, bay.z + bay.length * 0.5f + 0.03f}), {bay.width * 0.7f, bay.height * 0.5f, 0.1f});
            const glm::mat4 bottom(glm::vec4(1, 0, 0, 0), glm::vec4(0, 0, 1, 0), glm::vec4(0, -1, 0, 0),
                                   glm::vec4(bay.x, y - bay.height * 0.5f, bay.z, 1));
            greeble(m_b, rng, Patch{bottom, {bay.width * 0.5f - 0.2f, bay.length * 0.5f - 0.2f}},
                    m_parts.greebleSettings(0.35f, 0.6f));
        }
    }

    void buildHullSide(Rng& rng, const FacePatch& fp, std::vector<UvRect> keepOut)
    {
        const Patch& p = fp.patch;
        const float zMid = m_L * 0.5f;
        const float panelHalf = p.halfSize.x * 0.45f;

        // Accent trim lines framing the smooth middle panel
        float vLo = m_belts[0].z1 - zMid + 0.2f, vHi = m_belts[1].z0 - zMid - 0.2f;
        use(m_b, m_mat.accent);
        for (float u : {panelHalf + 0.25f, -panelHalf - 0.25f})
            m_b.box(p.frame * at({u, (vLo + vHi) * 0.5f, 0.04f}), {0.3f, vHi - vLo, 0.08f});

        // Docking ports along the panel's center line
        std::vector<UvRect> panelKeepOut = keepOut;
        for (const DockingPortPlan& port : m_ports) {
            m_parts.dockingPort(p.frame * at({0.0f, port.z - zMid, 0.0f}), port.radius, port.length);
            panelKeepOut.push_back(UvRect::around({0.0f, port.z - zMid}, glm::vec2(port.radius * 1.6f)));
        }

        // Porthole row, and trace relief everywhere else in the panel
        const float rowU = panelHalf * rng.range(-0.3f, 0.3f);
        m_parts.portholeRow(rng, p, rowU, vLo, vHi, panelKeepOut);
        std::vector<UvRect> traceKeepOut = panelKeepOut;
        traceKeepOut.push_back({{rowU - 0.8f, -1e3f}, {rowU + 0.8f, 1e3f}});
        circuitTraces(m_b, rng, Patch{p.frame, {panelHalf, p.halfSize.y}}, m_mat.hull, int(m_L * 0.5f), 0.16f,
                      traceKeepOut);

        // Greebles above and below the panel
        keepOut.push_back({{-panelHalf - 0.5f, -1e3f}, {panelHalf + 0.5f, 1e3f}});
        GreebleSettings s = m_parts.greebleSettings(0.3f, 0.8f);
        s.keepOut = keepOut;
        greeble(m_b, rng, p, s);
    }

    // -----------------------------------------------------------------------
    // Bridge tower
    // -----------------------------------------------------------------------

    void buildTower(Rng& rng)
    {
        float y = m_H * 0.5f - 0.1f;
        float z = m_towerZ;
        float w = m_towerW, d = m_towerD;
        const int tiers = rng.range(2, 4);
        const float chamfer = rng.range(0.05f, 0.25f);
        Profile top;
        glm::vec2 topScale(1.0f);

        for (int t = 0; t < tiers; ++t) {
            const float h = rng.range(1.4f, 2.8f);
            const Profile prof = profile::chamferedRect(w, d, std::min(w, d) * chamfer);
            ExtrudeOptions o;
            o.scale1 = {rng.range(0.85f, 1.0f), rng.range(0.72f, 0.95f)};
            o.offset1 = {0.0f, d * rng.range(0.0f, 0.12f)}; // lean back (local +Y is world -Z)
            use(m_b, t == 0 ? m_mat.hullDark : m_mat.hull);
            m_b.extrude(upFrame({0, y, z}), prof, 0.0f, h, o);

            // Recessed window band on top of the tier
            const float bandH = rng.range(0.6f, 1.0f);
            ExtrudeOptions band;
            band.scale0 = band.scale1 = o.scale1 * 0.97f;
            band.cap0 = band.cap1 = false;
            use(m_b, m_mat.windows, {bandH * 4.0f, -1.0f});
            m_b.extrude(upFrame({0, y + h, z - o.offset1.y}), prof, 0.0f, bandH, band);

            top = prof;
            topScale = o.scale1;
            y += h + bandH;
            z -= o.offset1.y;
            w = w * o.scale1.x * rng.range(0.8f, 0.97f);
            d = d * o.scale1.y * rng.range(0.85f, 1.0f);
        }

        // Roof slab on the last window band
        use(m_b, m_mat.hull);
        ExtrudeOptions roof;
        roof.scale0 = topScale * 1.04f;
        roof.scale1 = topScale * 0.98f;
        m_b.extrude(upFrame({0, y, z}), top, 0.0f, 0.4f, roof);
        y += 0.4f;
        const glm::vec2 roofHalf(w * 0.45f, d * 0.4f);

        greeble(m_b, rng, Patch{upFrame({0, y, z}), roofHalf}, m_parts.greebleSettings(0.5f, 0.45f));

        // Horn-like spires, sensor dome and antenna masts
        if (rng.chance(0.6f)) {
            const float h = rng.range(2.0f, 4.0f);
            for (float s : {-1.0f, 1.0f}) {
                use(m_b, m_mat.hull);
                m_b.frustum(upFrame({s * roofHalf.x * 0.35f, y, z + roofHalf.y * 0.3f}), 0.45f, 0.06f, 0.0f, h, 10);
                use(m_b, m_mat.truss);
                m_b.cylinder(upFrame({s * roofHalf.x * 0.35f, y, z + roofHalf.y * 0.3f}), 0.05f, h, h + 1.5f, 6);
                m_parts.light(m_mat.navWhite, {s * roofHalf.x * 0.35f, y + h + 1.5f, z + roofHalf.y * 0.3f}, 0.18f);
            }
        }
        if (rng.chance(0.45f)) {
            use(m_b, m_mat.hull);
            m_b.dome(upFrame({roofHalf.x * 0.4f, y, z - roofHalf.y * 0.4f}), rng.range(0.6f, 1.2f), 0.0f, 14, 4);
        }
        const int masts = rng.range(1, 3);
        for (int i = 0; i < masts; ++i) {
            const glm::vec3 base(rng.range(-roofHalf.x, roofHalf.x) * 0.8f, y, z + rng.range(-roofHalf.y, roofHalf.y) * 0.8f);
            const float h = rng.range(2.0f, 5.0f);
            use(m_b, m_mat.truss);
            m_b.cylinder(upFrame(base), 0.08f, 0.0f, h, 6);
            use(m_b, m_mat.hullDark);
            m_b.box(at(base + glm::vec3(0, h * 0.6f, 0)), {0.9f, 0.08f, 0.08f});
            m_parts.light(m_mat.navWhite, base + glm::vec3(0, h, 0), 0.2f);
        }
    }

    // -----------------------------------------------------------------------
    // Neck between engine block and hull
    // -----------------------------------------------------------------------

    void buildNeck(Rng& rng)
    {
        const float k = rng.range(0.5f, 0.7f);
        const Profile prof = profile::chamferedRect(m_W * k, m_H * k, m_C * k);
        const float z0 = m_zEng1 - 0.3f, z1 = 0.3f;
        use(m_b, m_mat.hullDark);
        m_b.extrude(glm::mat4(1.0f), prof, z0, z1);

        // Pipe runs along the neck
        const int pipes = rng.range(3, 7);
        for (int i = 0; i < pipes; ++i) {
            const std::size_t e = std::size_t(rng.range(0, int(prof.size()) - 1));
            const glm::vec2 a = prof[e], c = prof[(e + 1) % prof.size()];
            const glm::vec2 t = glm::normalize(c - a);
            const glm::vec2 nrm(t.y, -t.x);
            const float r = rng.range(0.25f, 0.6f);
            const glm::vec2 pos = glm::mix(a, c, rng.range(0.2f, 0.8f)) + nrm * (r + 0.05f);
            use(m_b, m_mat.truss, {1.0f, 2.0f});
            m_b.cylinder(at({pos, 0.0f}), r, m_zEng1, 0.0f, 10);
            use(m_b, m_mat.hullDark);
            for (float z : {m_zEng1 + 0.4f, m_zEng1 * 0.5f, -0.4f})
                m_b.cylinder(at({pos, 0.0f}), r * 1.35f, z - 0.15f, z + 0.15f, 10);
        }

        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, m_zEng1, 0.0f, 0.3f))
            greeble(m_b, rng, fp.patch, m_parts.greebleSettings(0.25f, 0.6f));
    }

    // -----------------------------------------------------------------------
    // Engine block
    // -----------------------------------------------------------------------

    void buildEngine(Rng& rng)
    {
        const Profile prof = profile::chamferedRect(m_engW, m_engH, m_engC);
        use(m_b, m_mat.hull);
        m_b.extrude(glm::mat4(1.0f), prof, m_zEng0, m_zEng1);

        // Rear and front frame belts
        const glm::vec2 beltScale((m_engW + 0.4f) / m_engW, (m_engH + 0.4f) / m_engH);
        ExtrudeOptions belt;
        belt.scale0 = belt.scale1 = beltScale;
        const float rearBelt = rng.range(1.0f, 2.5f);
        const float frontBelt = rng.range(0.8f, 2.0f);
        use(m_b, m_mat.hullDark);
        m_b.extrude(glm::mat4(1.0f), prof, m_zEng0 - 0.1f, m_zEng0 + rearBelt, belt);
        m_b.extrude(glm::mat4(1.0f), prof, m_zEng1 - frontBelt, m_zEng1 + 0.1f, belt);

        const float hw = m_engW * 0.5f, hh = m_engH * 0.5f;
        const float zMid = (m_zEng0 + m_zEng1) * 0.5f;

        // Top cowling
        bool cowling = rng.chance(0.7f);
        const float cowlW = (m_engW - 2.0f * m_engC) * rng.range(0.6f, 0.9f);
        const float cowlZ0 = m_zEng0 + rearBelt + 0.5f, cowlZ1 = m_zEng1 - frontBelt - 0.8f;
        if (cowling && cowlZ1 > cowlZ0 + 2.0f) {
            ExtrudeOptions o;
            o.shading = Shading::Smooth;
            use(m_b, m_mat.hull);
            m_b.extrude(at({0, hh - 0.05f, 0}), profile::arch(cowlW, rng.range(1.2f, 2.6f), 12), cowlZ0, cowlZ1, o);
        } else {
            cowling = false;
        }

        // Radiator panels on the sides
        const bool radiators = rng.chance(0.4f);
        const float radLen = (m_zEng1 - m_zEng0) * rng.range(0.4f, 0.6f);
        const float radSpan = rng.range(4.0f, 8.0f);
        if (radiators) {
            for (float s : {-1.0f, 1.0f}) {
                for (float yOff : {hh * 0.25f, -hh * 0.25f}) {
                    const glm::vec3 c(s * (hw + radSpan * 0.5f), yOff, zMid);
                    use(m_b, m_mat.greeble, {2.0f, 2.0f});
                    m_b.box(at(c), {radSpan, 0.12f, radLen});
                    use(m_b, m_mat.truss);
                    const float x0 = s * hw, x1 = s * (hw + radSpan);
                    m_b.beam(glm::mat4(1.0f), {x1, yOff, zMid - radLen * 0.5f}, {x1, yOff, zMid + radLen * 0.5f}, 0.25f);
                    m_b.beam(glm::mat4(1.0f), {x0, yOff, zMid - radLen * 0.5f}, {x1, yOff, zMid - radLen * 0.5f}, 0.25f);
                    m_b.beam(glm::mat4(1.0f), {x0, yOff, zMid + radLen * 0.5f}, {x1, yOff, zMid + radLen * 0.5f}, 0.25f);
                }
            }
        }

        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, m_zEng0, m_zEng1, 0.25f)) {
            const Patch& p = fp.patch;
            GreebleSettings s = m_parts.greebleSettings(0.28f, 0.8f);
            s.keepOut.push_back(zBand(p, zMid, m_zEng0 - 1.0f, m_zEng0 + rearBelt));
            s.keepOut.push_back(zBand(p, zMid, m_zEng1 - frontBelt, m_zEng1 + 1.0f));
            if (fp.face == Face::Top && cowling)
                s.keepOut.push_back({{-cowlW * 0.5f - 0.4f, cowlZ0 - zMid - 0.4f}, {cowlW * 0.5f + 0.4f, cowlZ1 - zMid + 0.4f}});
            if (fp.face == Face::Side) {
                if (radiators)
                    s.keepOut.push_back({{-p.halfSize.x, -radLen * 0.5f - 0.5f}, {p.halfSize.x, radLen * 0.5f + 0.5f}});
                // RCS pods near both ends, upper part of the side
                for (float v : {-p.halfSize.y + 1.2f, p.halfSize.y - 1.2f}) {
                    const float u = fp.normal.x > 0.0f ? p.halfSize.x * 0.6f : -p.halfSize.x * 0.6f;
                    m_parts.rcsPod(p.frame * at({u, v, 0.0f}));
                    s.keepOut.push_back(UvRect::around({u, v}, glm::vec2(1.0f)));
                }
            }
            greeble(m_b, rng, p, s);
        }

        buildNozzles(rng, hw, hh);

        // Front face of the engine block around the neck
        Patch front{at({0, 0, m_zEng1 + 0.1f}), {hw - m_engC * 0.6f, hh - m_engC * 0.6f}};
        GreebleSettings fs = m_parts.greebleSettings(0.3f, 0.6f);
        fs.keepOut.push_back(UvRect::around({0, 0}, {m_W * 0.36f + 0.4f, m_H * 0.36f + 0.4f}));
        greeble(m_b, rng, front, fs);
    }

    void buildNozzles(Rng& rng, float hw, float hh)
    {
        auto inside = [&](const glm::vec2& p, float r) {
            const float m = 0.35f;
            return std::abs(p.x) + r <= hw - m && std::abs(p.y) + r <= hh - m &&
                   std::abs(p.x) + std::abs(p.y) + r * 1.4142f <= hw + hh - m_engC - m;
        };

        const int count = rng.range(1, 4);
        float R = std::min(hw, hh) * rng.range(0.3f, 0.42f) * (count == 1 ? 1.5f : 1.0f);
        std::vector<glm::vec2> centers;
        for (int attempt = 0; attempt < 30; ++attempt) {
            centers.clear();
            const float s = R * 1.15f;
            switch (count) {
            case 1: centers = {{0.0f, 0.0f}}; break;
            case 2: centers = {{-s, 0.0f}, {s, 0.0f}}; break;
            case 3:
                for (int i = 0; i < 3; ++i) {
                    const float a = kPi * 0.5f + float(i) * kPi * 2.0f / 3.0f;
                    centers.emplace_back(std::cos(a) * s * 1.15f, std::sin(a) * s * 1.15f);
                }
                break;
            default: centers = {{-s, -s}, {s, -s}, {s, s}, {-s, s}}; break;
            }
            if (std::all_of(centers.begin(), centers.end(), [&](const glm::vec2& c) { return inside(c, R * 1.12f); }))
                break;
            R *= 0.9f;
        }

        for (const glm::vec2& c : centers)
            m_parts.nozzle(frameAlong({c, m_zEng0}, -kForward, kUp), R, m_mat.engineGlow, false);

        // Ring of small thrusters around the main cluster
        float clusterR = 0.0f;
        for (const glm::vec2& c : centers)
            clusterR = std::max(clusterR, glm::length(c) + R * 1.12f);
        const float rs = R * rng.range(0.24f, 0.36f);
        const float ringR = clusterR + rs * 1.12f + 0.3f;
        const int slots = rng.range(8, 14);
        const float phase = rng.range(0.0f, kPi);
        const bool rims = rng.chance(0.7f);
        for (int i = 0; i < slots; ++i) {
            const float a = phase + float(i) * 2.0f * kPi / float(slots);
            const glm::vec2 c(std::cos(a) * ringR, std::sin(a) * ringR);
            if (inside(c, rs * 1.12f))
                m_parts.nozzle(frameAlong({c, m_zEng0}, -kForward, kUp), rs, m_mat.thrusterGlow, rims);
        }
    }

    // -----------------------------------------------------------------------
    // Cargo section
    // -----------------------------------------------------------------------

    void buildCargo(Rng& rng)
    {
        switch (rng.range(0, 2)) {
        case 0: buildDrums(rng); break;
        case 1: buildContainers(rng); break;
        default: buildTanks(rng); break;
        }
    }

    void buildDrums(Rng& rng)
    {
        const float R = std::min(m_W, m_H) * rng.range(0.4f, 0.5f);
        const float drumLen = rng.range(5.0f, 9.0f);
        const float gap = rng.range(0.6f, 1.4f);
        const int count = std::max(3, int(m_cargoLen / (drumLen + gap)));
        m_cargoLen = float(count) * (drumLen + gap) + gap;
        const float z0 = m_L, z1 = m_L + m_cargoLen;
        m_envelope = glm::vec2(R);

        use(m_b, m_mat.hullDark);
        m_b.cylinder(glm::mat4(1.0f), R * 0.28f, z0, z1, 16);

        const bool alternate = rng.chance(0.5f);
        for (int i = 0; i < count; ++i) {
            const float zs = z0 + gap + float(i) * (drumLen + gap);
            const float e = drumLen * 0.12f;
            const Surface& cargo = m_mat.cargo[alternate ? std::size_t(i) % m_mat.cargo.size() : 0];
            use(m_b, cargo, {cargo.tile.x, drumLen});
            m_b.frustum(glm::mat4(1.0f), R * 0.8f, R, zs, zs + e, 32, false);
            m_b.cylinder(glm::mat4(1.0f), R, zs + e, zs + drumLen - e, 32, false);
            m_b.frustum(glm::mat4(1.0f), R, R * 0.8f, zs + drumLen - e, zs + drumLen, 32, false);
            use(m_b, m_mat.hullDark);
            m_b.disc(glm::mat4(1.0f), R * 0.8f, zs, 32, false);
            m_b.disc(glm::mat4(1.0f), R * 0.8f, zs + drumLen, 32, true);
            for (float t : {0.3f, 0.7f})
                m_b.cylinder(glm::mat4(1.0f), R * 1.015f, zs + drumLen * t - 0.15f, zs + drumLen * t + 0.15f, 32, false);
        }

        // External trusses running alongside the drums
        const float half = R * rng.range(0.1f, 0.16f);
        const int trusses = rng.range(1, 2);
        const float baseAngle = rng.chance(0.5f) ? 0.0f : kPi * 0.5f;
        const bool xBrace = rng.chance(0.6f);
        for (int t = 0; t < trusses; ++t) {
            const float a = baseAngle + float(t) * kPi;
            const glm::vec2 c = glm::vec2(std::cos(a), std::sin(a)) * (R + half + 0.15f);
            m_parts.truss(at({c, 0.0f}), z0 - 0.5f, z1, half, half * 0.18f, xBrace);
            m_envelope = glm::max(m_envelope, glm::abs(c) + half);
        }
    }

    void buildContainers(Rng& rng)
    {
        const float gap = rng.range(0.3f, 0.7f);
        float cw = rng.range(2.6f, 3.6f);
        cw = std::min(cw, (m_W * 0.6f - gap) / 1.5f);
        const float ch = cw * rng.range(0.85f, 1.15f);
        const float cl = rng.range(6.0f, 12.0f);
        const float bayGap = rng.range(0.8f, 1.6f);
        const int bays = std::max(3, int(m_cargoLen / (cl + bayGap)));
        m_cargoLen = float(bays) * (cl + bayGap) + bayGap;
        const float z0 = m_L, z1 = m_L + m_cargoLen;
        const float spine = std::min(cw, ch) * 0.5f - 0.2f;
        m_envelope = {1.5f * cw + gap, 1.5f * ch + gap};

        m_parts.truss(glm::mat4(1.0f), z0 - 0.5f, z1, spine, spine * 0.16f, rng.chance(0.5f));

        const float emptyChance = rng.range(0.0f, 0.25f);
        for (int b = 0; b < bays; ++b) {
            const float zs = z0 + bayGap + float(b) * (cl + bayGap);
            for (int i = -1; i <= 1; ++i)
                for (int j = -1; j <= 1; ++j) {
                    if ((i == 0 && j == 0) || rng.chance(emptyChance))
                        continue;
                    const glm::vec3 c(float(i) * (cw + gap), float(j) * (ch + gap), zs + cl * 0.5f);
                    const Surface& cargo = rng.pick(m_mat.cargo);
                    use(m_b, cargo, {cargo.tile.x, -1.0f});
                    m_b.box(at(c), {cw, ch, cl * 0.98f});
                    use(m_b, m_mat.hullDark);
                    for (float s : {-1.0f, 1.0f})
                        m_b.box(at(c + glm::vec3(0, 0, s * (cl * 0.5f - 0.15f))), {cw + 0.12f, ch + 0.12f, 0.3f});
                }
            // Bay frame: rectangle around the rack with spokes to the spine
            const float zf = zs - bayGap * 0.5f;
            const glm::vec2 e = m_envelope + 0.15f;
            const glm::vec3 corners[] = {{-e.x, -e.y, zf}, {e.x, -e.y, zf}, {e.x, e.y, zf}, {-e.x, e.y, zf}};
            use(m_b, m_mat.truss);
            for (int k = 0; k < 4; ++k) {
                m_b.beam(glm::mat4(1.0f), corners[k], corners[(k + 1) % 4], 0.3f);
                const glm::vec3 inner(glm::sign(corners[k].x) * spine, glm::sign(corners[k].y) * spine, zf);
                m_b.beam(glm::mat4(1.0f), inner, corners[k], 0.25f);
            }
        }
        m_envelope += 0.3f;
    }

    void buildTanks(Rng& rng)
    {
        const int n = rng.range(5, 8);
        const float ringR = std::min(m_W, m_H) * rng.range(0.3f, 0.4f);
        const float rt = ringR * std::sin(kPi / float(n)) * 0.92f;
        const float tl = std::max(rng.range(6.0f, 12.0f), rt * 3.0f);
        const float bayGap = rng.range(0.8f, 1.8f);
        const int bays = std::max(3, int(m_cargoLen / (tl + bayGap)));
        m_cargoLen = float(bays) * (tl + bayGap) + bayGap;
        const float z0 = m_L, z1 = m_L + m_cargoLen;
        const float spine = std::max(0.6f, (ringR - rt) * 0.5f);
        m_envelope = glm::vec2(ringR + rt);

        m_parts.truss(glm::mat4(1.0f), z0 - 0.5f, z1, spine, spine * 0.18f, rng.chance(0.5f));

        const Surface& tankSurface = m_mat.cargo[0];
        const float phase = rng.range(0.0f, kPi);
        for (int b = 0; b < bays; ++b) {
            const float zs = z0 + bayGap + float(b) * (tl + bayGap);
            std::vector<glm::vec3> ends;
            for (int i = 0; i < n; ++i) {
                const float a = phase + float(i) * 2.0f * kPi / float(n);
                const glm::vec2 c(std::cos(a) * ringR, std::sin(a) * ringR);
                const glm::mat4 f = at({c, 0.0f});
                use(m_b, tankSurface, {tankSurface.tile.x, tl});
                m_b.cylinder(f, rt, zs + rt, zs + tl - rt, 20, false);
                m_b.dome(f, rt, zs + tl - rt, 20, 4, true);
                m_b.dome(f, rt, zs + rt, 20, 4, false);
                use(m_b, m_mat.hullDark);
                m_b.cylinder(f, rt * 1.03f, zs + tl * 0.5f - 0.2f, zs + tl * 0.5f + 0.2f, 20, false);
                ends.emplace_back(c, zs + rt * 0.5f);
            }
            use(m_b, m_mat.truss);
            for (int i = 0; i < n; ++i) {
                const glm::vec3& a = ends[i];
                m_b.beam(glm::mat4(1.0f), a, ends[(i + 1) % n], 0.25f);
                const glm::vec2 dir = glm::normalize(glm::vec2(a));
                m_b.beam(glm::mat4(1.0f), {dir * spine, a.z}, a, 0.25f);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Bow
    // -----------------------------------------------------------------------

    void buildBow(Rng& rng)
    {
        const float z0 = m_L + m_cargoLen;
        const glm::vec2 e = m_envelope;
        glm::vec3 tip;

        if (rng.chance(0.6f)) {
            // Tapered prow
            const float len = rng.range(5.0f, 11.0f);
            const Profile prof = profile::chamferedRect(e.x * 2.0f, e.y * 1.9f, std::min(e.x, e.y) * rng.range(0.3f, 0.6f));
            ExtrudeOptions o;
            o.scale1 = {rng.range(0.25f, 0.45f), rng.range(0.3f, 0.6f)};
            o.offset1 = {0.0f, -e.y * rng.range(0.0f, 0.3f)};
            use(m_b, m_mat.hull);
            m_b.extrude(glm::mat4(1.0f), prof, z0 - 0.3f, z0 + len, o);
            ExtrudeOptions ring;
            ring.scale0 = ring.scale1 = glm::vec2(1.04f);
            use(m_b, m_mat.accent);
            m_b.extrude(glm::mat4(1.0f), prof, z0 - 0.3f, z0 + 0.8f, ring);
            tip = {0.0f, o.offset1.y, z0 + len};
        } else {
            // Docking pod with a forward port
            const float len = rng.range(5.0f, 9.0f);
            const Profile prof = profile::chamferedRect(e.x * 1.6f, e.y * 1.4f, std::min(e.x, e.y) * 0.3f);
            use(m_b, m_mat.hull);
            m_b.extrude(glm::mat4(1.0f), prof, z0 - 0.3f, z0 + len);
            for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, z0, z0 + len, 0.3f))
                greeble(m_b, rng, fp.patch, m_parts.greebleSettings(0.3f, 0.6f));
            const float r = std::min(e.x, e.y) * 0.3f;
            m_parts.dockingPort(at({0, 0, z0 + len}), r, rng.range(1.0f, 2.0f));
            tip = {0.0f, e.y * 0.7f, z0 + len};
        }

        // Antenna spars sweeping forward
        const int spars = rng.range(1, 2);
        for (int i = 0; i < spars; ++i) {
            const glm::vec3 base = tip + glm::vec3((spars == 2 ? (i == 0 ? -1.0f : 1.0f) : 0.0f) * e.x * 0.3f, 0, -0.5f);
            const glm::vec3 dir = glm::normalize(glm::vec3(base.x * 0.05f, rng.range(-0.45f, 0.1f), 1.0f));
            const float len = rng.range(4.0f, 9.0f);
            use(m_b, m_mat.truss);
            m_b.cylinder(frameAlong(base, dir), 0.12f, 0.0f, len, 6);
            m_parts.light(m_mat.navWhite, base + dir * len, 0.22f);
        }
    }

    // -----------------------------------------------------------------------
    // Navigation lights: red to port (+X), green to starboard (-X)
    // -----------------------------------------------------------------------

    void buildNavLights()
    {
        const float hx = m_engW * 0.5f + 0.25f;
        m_parts.light(m_mat.navRed, {hx, m_engH * 0.25f, m_zEng0 + 0.6f});
        m_parts.light(m_mat.navGreen, {-hx, m_engH * 0.25f, m_zEng0 + 0.6f});
        m_parts.light(m_mat.navRed, {m_W * 0.5f + 0.4f, m_H * 0.3f, m_L - 0.6f});
        m_parts.light(m_mat.navGreen, {-m_W * 0.5f - 0.4f, m_H * 0.3f, m_L - 0.6f});
        m_parts.light(m_mat.navWhite, {0.0f, -m_H * 0.5f - 0.3f, m_L * 0.5f});
    }

    Rng m_rng;
    MeshBuilder m_b;
    ShipMaterials m_mat;
    ShipParts m_parts;

    float m_W = 0, m_H = 0, m_C = 0, m_L = 0;
    float m_engW = 0, m_engH = 0, m_engC = 0, m_engLen = 0, m_neckLen = 0;
    float m_zEng0 = 0, m_zEng1 = 0;
    float m_cargoLen = 0;
    glm::vec2 m_envelope{0.0f};

    std::vector<Interval> m_belts;
    std::vector<DockingPortPlan> m_ports;
    std::vector<BayPlan> m_bays;
    float m_towerZ = 0, m_towerW = 0, m_towerD = 0;
};

} // namespace

ModelData generateLogisticsFreighter(std::uint64_t seed, const std::filesystem::path& textureDir)
{
    ModelData model;
    Freighter freighter(seed, textureDir, model);
    freighter.build();
    return model;
}

} // namespace space
