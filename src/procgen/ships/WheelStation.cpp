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

// The ring, spokes and rear section are this much bigger than in the first station design, to make room
// for the hangar. Surface detail (greebles, windows, lights, ports) keeps its real size, so the bigger
// structure gets more of it rather than larger pieces.
constexpr float kScale = 2.5f;
// Landing pads the hangar decks are sized for: a square with room for the largest fighter (about 20 x 26).
constexpr float kPadSize = 34.0f;
// World units per repeat of the high-resolution hub textures.
constexpr float kHubTile = 16.0f;

glm::vec3 radial(float a) { return {std::cos(a), std::sin(a), 0.0f}; }

// Scales an (r, z) profile about a center point.
Profile scaledAbout(const Profile& prof, const glm::vec2& center, float s)
{
    Profile out;
    for (const glm::vec2& p : prof)
        out.push_back(center + (p - center) * s);
    return out;
}

// Prism with flat faces whose inscribed radius is `inner`. Face k is centered at angle 2 pi k / sides.
Profile prism(int sides, float inner)
{
    return profile::regular(sides, inner / std::cos(kPi / float(sides)), kPi / float(sides));
}

// Frame on a flat face at height z facing +Z, centered at `center`, with local X along `dir`.
glm::mat4 faceFrame(const glm::vec2& center, const glm::vec2& dir, float z)
{
    return glm::mat4(glm::vec4(dir, 0, 0), glm::vec4(-dir.y, dir.x, 0, 0), glm::vec4(kAxis, 0),
                     glm::vec4(center, z, 1));
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

// Materials for the hub, which is seen from close by: high-resolution plating and the hangar deck.
struct HubSurfaces {
    Surface plating; // outer hull
    Surface dark;    // collars, frames, the entrance tunnel
    Surface accent;  // livery belts
    Surface wall;    // hangar walls
    Surface deck;    // hangar decks, where the landing pads go
    Surface hazard;  // striped trim around the entrance
};

class WheelStation {
public:
    WheelStation(std::uint64_t seed, const std::filesystem::path& textureDir, ModelData& out)
        : m_rng(seed), m_model(out), m_b(out), m_parts(m_b, m_mat)
    {
        Rng livery = m_rng.fork();
        m_mat = ShipMaterials::create(m_b, textureDir, livery);

        auto add = [&](const MaterialDesc& m, const glm::vec2& tile) { return Surface{m_b.addMaterial(m), tile}; };
        // Inside the hangar, surfaces glow faintly with their own texture. The renderer has no lamps or
        // shadows, so this stands in for the hangar's floodlights and keeps the walls facing away from the
        // sun from going black.
        auto floodlit = [](MaterialDesc m) {
            m.emissive = m.diffuse * 0.5f;
            m.emissiveMap = m.diffuseMap;
            return m;
        };
        const std::filesystem::path plating = textureDir / "hub_plating.png";
        const std::filesystem::path deck = textureDir / "hangar_deck.png";
        const std::filesystem::path hazard = textureDir / "hazard.png";
        const glm::vec3 hull = m_mat.hullTint;
        const glm::vec2 tile(kHubTile);
        m_hub.plating = add(texturedMaterial("hub_plating", plating, hull, 0.35f, 24.0f), tile);
        m_hub.dark = add(texturedMaterial("hub_dark", plating, hull * 0.55f, 0.3f, 24.0f), tile);
        m_hub.accent = add(texturedMaterial("hub_accent", plating, m_mat.accentTint, 0.4f, 32.0f), tile);
        m_hub.wall = add(floodlit(texturedMaterial("hangar_wall", plating, hull * 0.7f, 0.3f, 24.0f)), tile);
        m_hub.deck = add(floodlit(texturedMaterial("hangar_deck", deck, hull * 0.8f, 0.25f, 16.0f)), tile);
        m_hub.hazard = add(texturedMaterial("hazard", hazard, glm::vec3(1.0f), 0.3f, 24.0f), {6.0f, -1.0f});
    }

    void build()
    {
        Rng dims = m_rng.fork();

        // Hangar: a prism whose inner faces are decks, each wide enough for a row of landing pads
        m_hangarSides = dims.pick(std::vector<int>{4, 4, 6, 6, 6, 8});
        m_deckWidth = kPadSize + 2.0f * dims.range(3.0f, 6.0f);
        m_hangarR = m_deckWidth * 0.5f / std::tan(kPi / float(m_hangarSides));
        const int padsAlong = dims.range(1, 2);
        const float hangarLen = float(padsAlong) * (kPadSize + 4.0f) + dims.range(10.0f, 20.0f);

        // Hub: twice as many faces as the hangar, so each hangar corner sits behind the middle of a hull
        // face, with a wall in between
        m_hubSides = 2 * m_hangarSides;
        m_hubR = m_hangarR / std::cos(kPi / float(m_hangarSides)) + dims.range(3.0f, 5.0f);
        m_collarDepth = dims.range(2.0f, 3.5f);

        // Ring and spokes
        m_ringW = dims.range(7.0f, 13.0f) * kScale * 0.85f;
        m_ringH = m_ringW * dims.range(0.6f, 0.95f);
        m_ringC = std::min(m_ringW, m_ringH) * dims.range(0.1f, 0.25f);
        m_R = std::max(dims.range(38.0f, 70.0f) * kScale, m_hubR * 3.2f + m_ringH * 0.5f);
        m_torus = dims.chance(0.3f);
        m_spokes = dims.pick(std::vector<int>{3, 4, 4, 5, 6, 6, 8});
        m_modulesPerSpoke = dims.range(2, 4);
        m_modules = m_spokes * m_modulesPerSpoke;
        m_segsPerModule = std::max(2, int(std::round(kTwoPi * m_R / float(m_modules) / dims.range(7.0f, 10.0f))));
        m_segments = m_modules * m_segsPerModule;
        m_spokeR = dims.range(0.9f, 1.6f) * kScale;
        if (dims.chance(0.25f)) {
            const float d = m_ringW * 0.5f + dims.range(4.0f, 9.0f) * kScale;
            m_ringZ = {-d, d};
        } else {
            m_ringZ = {0.0f};
        }

        // The hub holds the hangar and every spoke collar, with room to spare at both ends. It may sit a
        // little forward of the ring when it is longer than the collars need.
        const float frontWall = dims.range(3.0f, 5.0f), backWall = 3.0f;
        const float ringNeed = m_ringZ.back() + collarHalf() + dims.range(10.0f, 20.0f);
        const float hubLen = std::max(hangarLen + frontWall + backWall, 2.0f * ringNeed);
        m_hubZ0 = -hubLen * 0.5f + dims.range(0.0f, hubLen * 0.5f - ringNeed);
        m_hubZ1 = m_hubZ0 + hubLen;
        m_hangarZ0 = m_hubZ0 + backWall;
        m_hangarZ1 = m_hubZ1 - frontWall;

        // Entrance slot: wide enough for a fighter with its wings level, and inside the hangar's outline
        m_slot.x = dims.range(18.0f, 24.0f);
        m_slot.y = m_slot.x * dims.range(0.32f, 0.42f);
        float reach = 0.0f;
        for (int k = 0; k < m_hangarSides; ++k) {
            const float a = kTwoPi * float(k) / float(m_hangarSides);
            reach = std::max(reach, std::abs(std::cos(a)) * m_slot.x + std::abs(std::sin(a)) * m_slot.y);
        }
        m_slot *= std::min(1.0f, (m_hangarR - 4.0f) / reach);

        Rng hubRng = m_rng.fork();
        Rng entranceRng = m_rng.fork();
        Rng rearRng = m_rng.fork();
        Rng ringRng = m_rng.fork();
        Rng spokeRng = m_rng.fork();
        Rng hangarRng = m_rng.fork();

        buildHub(hubRng);
        buildEntrance(entranceRng);
        buildHangar(hangarRng);
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

    GreebleSettings hubGreebles(float density) const
    {
        GreebleSettings s = m_parts.greebleSettings(density, 0.9f);
        s.plate = s.block = m_hub.plating;
        s.pipe = m_hub.dark;
        return s;
    }

    // Raised band around the hub between z0 and z1, standing `height` proud of the hull. Open in the
    // middle, so it doesn't wall off the hangar.
    void hubBand(float z0, float z1, float height)
    {
        const Profile in = prism(m_hubSides, m_hubR);
        const Profile out = prism(m_hubSides, m_hubR + height);
        ExtrudeOptions side;
        side.cap0 = side.cap1 = false;
        m_b.extrude(glm::mat4(1.0f), out, z0, z1, side);
        m_b.polygonRing(glm::mat4(1.0f), out, in, z0, false);
        m_b.polygonRing(glm::mat4(1.0f), out, in, z1, true);
    }

    // -----------------------------------------------------------------------
    // Hub: the port of entry, a hollow prism holding the hangar
    // -----------------------------------------------------------------------

    void buildHub(Rng& rng)
    {
        const Profile prof = prism(m_hubSides, m_hubR);
        ExtrudeOptions shell;
        shell.cap1 = false; // the front wall, with the entrance, is built separately
        use(m_b, m_hub.plating);
        m_b.extrude(glm::mat4(1.0f), prof, m_hubZ0, m_hubZ1, shell);

        struct Band {
            float z0, z1;
        };
        std::vector<Band> bands;

        // Collars where the spokes attach
        use(m_b, m_hub.dark);
        for (float z : m_ringZ) {
            hubBand(z - collarHalf(), z + collarHalf(), m_collarDepth);
            bands.push_back({z - collarHalf(), z + collarHalf()});
        }

        // End belts and optional accent belts
        const float beltW = rng.range(2.0f, 3.5f);
        use(m_b, m_hub.accent);
        hubBand(m_hubZ0, m_hubZ0 + beltW, 0.6f);
        hubBand(m_hubZ1 - beltW, m_hubZ1, 0.6f);
        bands.push_back({m_hubZ0 - 1.0f, m_hubZ0 + beltW});
        bands.push_back({m_hubZ1 - beltW, m_hubZ1 + 1.0f});
        const int extra = rng.range(0, 3);
        for (int i = 0; i < extra; ++i) {
            const float z = rng.range(m_hubZ0 + 8.0f, m_hubZ1 - 10.0f);
            const float w = rng.range(1.2f, 3.0f);
            const bool clash = std::any_of(bands.begin(), bands.end(),
                                           [&](const Band& b) { return z < b.z1 + 3.0f && z + w > b.z0 - 3.0f; });
            if (clash)
                continue;
            hubBand(z, z + w, 0.4f);
            bands.push_back({z, z + w});
        }

        const float zMid = (m_hubZ0 + m_hubZ1) * 0.5f;
        const int rcsEvery = std::max(1, m_hubSides / 4);
        int index = 0;
        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, m_hubZ0, m_hubZ1, 0.25f)) {
            const Patch& p = fp.patch;
            GreebleSettings s = hubGreebles(0.06f);
            for (const Band& b : bands)
                s.keepOut.push_back(zBand(p, zMid, b.z0, b.z1));
            if (index % rcsEvery == 0) {
                for (float v : {-p.halfSize.y + beltW + 2.0f, p.halfSize.y - beltW - 2.0f}) {
                    m_parts.rcsPod(p.frame * at({0.0f, v, 0.0f}));
                    s.keepOut.push_back(UvRect::around({0.0f, v}, glm::vec2(1.0f)));
                }
            } else if (index % 2 == 1 && rng.chance(0.7f)) {
                const float u = p.halfSize.x > 8.0f ? rng.range(-0.3f, 0.3f) * p.halfSize.x : 0.0f;
                m_parts.portholeRow(rng, p, u, -p.halfSize.y, p.halfSize.y, s.keepOut);
                s.keepOut.push_back({{u - 0.8f, -1e3f}, {u + 0.8f, 1e3f}});
            }
            greeble(m_b, rng, p, s);
            ++index;
        }
    }

    // The front wall: a slot into the hangar, lit and striped, with approach light masts and airlock
    // ports for ships too big to fly inside.
    void buildEntrance(Rng& rng)
    {
        const glm::mat4 I(1.0f);
        const Profile hull = prism(m_hubSides, m_hubR);
        const Profile slot = profile::rect(m_slot.x * 2.0f, m_slot.y * 2.0f);
        const float zf = m_hubZ1, zi = m_hangarZ1;

        // Front wall, its inside facing the hangar, and the tunnel through it
        use(m_b, m_hub.plating);
        m_b.polygonRing(I, hull, slot, zf);
        use(m_b, m_hub.wall);
        m_b.polygonRing(I, prism(m_hangarSides, m_hangarR), slot, zi, false);
        ExtrudeOptions tunnel;
        tunnel.inward = true;
        tunnel.cap0 = tunnel.cap1 = false;
        use(m_b, m_hub.dark);
        m_b.extrude(I, slot, zi, zf, tunnel);
        // Force field across the mouth of the slot, just inside the striped frame
        m_model.effects.push_back({EffectType::ForceField, at({0.0f, 0.0f, zf - 0.3f}), m_slot * 2.0f});
        use(m_b, m_mat.lamp);
        for (float s : {-1.0f, 1.0f})
            m_b.box(at({0.0f, s * (m_slot.y - 0.1f), (zi + zf) * 0.5f}), {m_slot.x * 1.8f, 0.2f, zf - zi - 0.4f});

        // Striped frame around the slot. The side bars are turned so their stripes run along them too.
        const float fw = rng.range(1.2f, 2.0f), fd = 0.6f;
        use(m_b, m_hub.hazard, {fw * 4.0f, -1.0f});
        for (float s : {-1.0f, 1.0f}) {
            m_b.box(at({0.0f, s * (m_slot.y + fw * 0.5f), zf + fd * 0.5f}), {(m_slot.x + fw) * 2.0f, fw, fd});
            m_b.box(faceFrame({s * (m_slot.x + fw * 0.5f), 0.0f}, {0.0f, 1.0f}, zf + fd * 0.5f),
                    {m_slot.y * 2.0f, fw, fd});
        }

        // Rows of lamps above and below the frame
        const float lampY = m_slot.y + fw + 0.8f;
        for (float x = -m_slot.x; x <= m_slot.x + 0.01f; x += m_slot.x / std::round(m_slot.x / 3.0f))
            for (float s : {-1.0f, 1.0f})
                m_parts.light(m_mat.lamp, {x, s * lampY, zf + 0.25f}, 0.45f);

        // Approach light masts beside (and sometimes above and below) the slot
        const float mastLen = rng.range(15.0f, 35.0f);
        std::vector<glm::vec2> masts = {{m_slot.x + fw + 2.0f, 0.0f}, {-(m_slot.x + fw + 2.0f), 0.0f}};
        if (rng.chance(0.5f)) {
            masts.push_back({0.0f, m_slot.y + fw + 2.5f});
            masts.push_back({0.0f, -(m_slot.y + fw + 2.5f)});
        }
        for (const glm::vec2& m : masts) {
            use(m_b, m_mat.truss);
            m_b.beam(I, glm::vec3(m, zf), glm::vec3(m, zf + mastLen), 0.5f);
            for (float t = 4.0f; t <= mastLen; t += 4.0f)
                m_parts.light(t + 4.0f > mastLen ? m_mat.navWhite : m_mat.lamp, glm::vec3(m, zf + t), 0.6f);
        }
        m_parts.light(m_mat.navRed, {m_hubR + 0.3f, 0.0f, zf + 0.3f}, 0.7f);
        m_parts.light(m_mat.navGreen, {-m_hubR - 0.3f, 0.0f, zf + 0.3f}, 0.7f);

        // Airlock ports above and below the slot
        const float portR = rng.range(1.4f, 2.0f);
        const float portRing = m_hubR * rng.range(0.68f, 0.8f);
        const float spread = rng.chance(0.5f) ? 0.0f : rng.range(0.3f, 0.45f);
        std::vector<glm::vec2> ports;
        for (float base : {0.5f * kPi, 1.5f * kPi})
            for (float a : spread > 0.0f ? std::vector<float>{base - spread, base + spread} : std::vector<float>{base})
                ports.push_back(glm::vec2(std::cos(a), std::sin(a)) * portRing);
        const float portLen = rng.range(2.0f, 4.0f);
        for (const glm::vec2& p : ports)
            m_parts.dockingPort(at(glm::vec3(p, zf)), portR, portLen);

        // Greebles on the rest of the front wall: above and below the slot, and beside it
        const float clear = fw + 3.0f;
        const float rim = m_hubR - 1.5f; // keeps patch corners inside the hull outline
        std::vector<Patch> patches;
        const float y0 = m_slot.y + clear, y1 = rim * 0.8f;
        const float topHalfX = std::sqrt(std::max(0.0f, rim * rim - y1 * y1));
        if (y1 > y0 + 2.0f && topHalfX > 2.0f)
            for (float s : {-1.0f, 1.0f})
                patches.push_back({faceFrame({0.0f, s * (y0 + y1) * 0.5f}, {1.0f, 0.0f}, zf),
                                   {topHalfX, (y1 - y0) * 0.5f}});
        const float x0 = m_slot.x + clear, sideHalfY = m_slot.y + fw;
        const float x1 = std::sqrt(std::max(0.0f, rim * rim - sideHalfY * sideHalfY));
        if (x1 > x0 + 2.0f)
            for (float s : {-1.0f, 1.0f})
                patches.push_back({faceFrame({s * (x0 + x1) * 0.5f, 0.0f}, {1.0f, 0.0f}, zf),
                                   {(x1 - x0) * 0.5f, sideHalfY}});
        for (const Patch& p : patches) {
            GreebleSettings s = hubGreebles(0.08f);
            const glm::vec2 center(p.frame[3]);
            for (const glm::vec2& port : ports)
                s.keepOut.push_back(UvRect::around(port - center, glm::vec2(portR * 1.45f + 0.5f)));
            greeble(m_b, rng, p, s);
        }
    }

    // The hangar: decks on every inner face, kept clear for landing pads, girders with light strips in
    // the corners, and control room windows on the back wall overlooking each deck.
    void buildHangar(Rng& rng)
    {
        const glm::mat4 I(1.0f);
        const Profile prof = prism(m_hangarSides, m_hangarR);
        const float z0 = m_hangarZ0, z1 = m_hangarZ1;

        ExtrudeOptions decks;
        decks.inward = true;
        decks.cap0 = decks.cap1 = false;
        use(m_b, m_hub.deck);
        m_b.extrude(I, prof, z0, z1, decks);
        use(m_b, m_hub.wall);
        m_b.polygon(I, prof, z0, true);

        const float girder = rng.range(1.8f, 2.8f);
        for (const glm::vec2& c : prof) {
            const glm::vec2 out = glm::normalize(c);
            use(m_b, m_hub.dark);
            m_b.beam(I, glm::vec3(c, z0), glm::vec3(c, z1), girder, glm::vec3(out, 0.0f));
            const glm::vec2 strip = c - out * (girder * 0.5f + 0.15f);
            use(m_b, m_mat.lamp);
            m_b.beam(I, glm::vec3(strip, z0 + 1.0f), glm::vec3(strip, z1 - 1.0f), 0.3f, glm::vec3(out, 0.0f));
        }

        // Control room windows, one band above each deck; short enough that neighbors don't meet
        const float bh = rng.range(1.0f, 1.6f);
        const float lift = rng.range(5.0f, 8.0f); // height above the deck
        const float d = m_hangarR - lift;
        const float bandLen = std::min(m_deckWidth * rng.range(0.4f, 0.7f),
                                       2.0f * (d - bh) * std::tan(kPi / float(m_hangarSides)) - 2.0f);
        for (int k = 0; k < m_hangarSides; ++k) {
            const float a = kTwoPi * float(k) / float(m_hangarSides);
            const glm::vec2 n(std::cos(a), std::sin(a)), t(-n.y, n.x);
            use(m_b, m_hub.dark);
            m_b.box(faceFrame(n * d, t, z0 + 0.1f), {bandLen + 0.8f, bh + 0.8f, 0.2f});
            use(m_b, m_mat.windows, {bh * 4.0f, -1.0f});
            m_b.box(faceFrame(n * d, t, z0 + 0.15f), {bandLen, bh, 0.3f});
        }

        // Machinery in the middle of the back wall, inside the ring of windows
        const float half = (d - bh) / 1.45f;
        if (half > 2.0f)
            greeble(m_b, rng, {faceFrame({0.0f, 0.0f}, {1.0f, 0.0f}, z0), glm::vec2(half)}, hubGreebles(0.2f));
    }

    // -----------------------------------------------------------------------
    // Rear: service module, mast, solar arrays, dish
    // -----------------------------------------------------------------------

    void buildRear(Rng& rng)
    {
        const float smR = m_hubR * rng.range(0.5f, 0.75f);
        const float smLen = rng.range(5.0f, 10.0f) * kScale;
        const float z1 = m_hubZ0 + 0.2f, z0 = m_hubZ0 - smLen;
        const Profile prof = prism(m_hubSides, smR);
        use(m_b, m_mat.hullDark);
        m_b.extrude(glm::mat4(1.0f), prof, z0, z1);
        for (const FacePatch& fp : facePatches(glm::mat4(1.0f), prof, z0, z1, 0.2f))
            greeble(m_b, rng, fp.patch, m_parts.greebleSettings(0.15f, 0.8f));

        // Radiator fins at the prism's corners
        if (rng.chance(0.6f)) {
            const int fins = m_hubSides / (rng.chance(0.5f) ? 2 : 1);
            const float span = rng.range(3.0f, 7.0f) * kScale;
            const float circum = smR / std::cos(kPi / float(m_hubSides));
            for (int i = 0; i < fins; ++i) {
                const float a = kPi / float(m_hubSides) + kTwoPi * float(i) / float(fins);
                const glm::vec3 r = radial(a), t(-std::sin(a), std::cos(a), 0.0f);
                const glm::vec3 c = r * (circum + span * 0.5f - 0.1f) + kAxis * ((z0 + z1) * 0.5f);
                const glm::mat4 f(glm::vec4(r, 0), glm::vec4(t, 0), glm::vec4(kAxis, 0), glm::vec4(c, 1));
                use(m_b, m_mat.greeble, {2.0f, 2.0f});
                m_b.box(f, {span, 0.3f, smLen * 0.8f});
                use(m_b, m_mat.truss);
                m_b.beam(f, {span * 0.5f, 0, -smLen * 0.4f}, {span * 0.5f, 0, smLen * 0.4f}, 0.5f);
            }
        }

        // Mast with solar arrays
        const float half = rng.range(0.6f, 1.0f) * kScale;
        const float mastLen = rng.range(14.0f, 30.0f) * kScale;
        const float mz0 = z0 - mastLen;
        m_parts.truss(glm::mat4(1.0f), mz0, z0 + 0.2f, half, half * 0.16f, rng.chance(0.5f));

        const float panelLen = rng.range(10.0f, 20.0f) * kScale * std::clamp(m_R / (50.0f * kScale), 0.8f, 1.3f);
        const float panelW = rng.range(3.5f, 6.0f) * kScale;
        const float arm = 1.6f * kScale;
        int pairs = rng.range(1, 3);
        while (pairs > 1 && mastLen / float(pairs) < panelW + 1.5f * kScale)
            --pairs;
        const bool crossed = rng.chance(0.4f);
        for (int i = 0; i < pairs; ++i) {
            const float z = z0 - mastLen * (float(i) + 0.5f) / float(pairs);
            const float rot = crossed && i % 2 == 1 ? kPi * 0.5f : 0.0f;
            for (float side : {-1.0f, 1.0f}) {
                const glm::vec3 dir = radial(rot) * side;
                use(m_b, m_mat.truss);
                m_b.beam(glm::mat4(1.0f), dir * half + kAxis * z, dir * (half + arm) + kAxis * z, 0.7f);
                const glm::vec3 c = dir * (half + arm + panelLen * 0.5f) + kAxis * z;
                const glm::mat4 f(glm::vec4(dir, 0), glm::vec4(kAxis, 0), glm::vec4(glm::cross(dir, kAxis), 0),
                                  glm::vec4(c, 1));
                use(m_b, m_mat.solar);
                m_b.box(f, {panelLen, panelW, 0.2f});
                use(m_b, m_mat.truss);
                for (float y : {-panelW * 0.5f, 0.0f, panelW * 0.5f})
                    m_b.beam(f, {-panelLen * 0.5f, y, 0}, {panelLen * 0.5f, y, 0}, 0.35f);
                m_b.beam(f, {panelLen * 0.5f, -panelW * 0.5f, 0}, {panelLen * 0.5f, panelW * 0.5f, 0}, 0.35f);
            }
        }

        // Dish at the end of the mast
        if (rng.chance(0.75f))
            m_parts.dish(frameAlong(kAxis * (mz0 - 0.8f), -kAxis, glm::vec3(0, 1, 0)), rng.range(2.5f, 5.0f) * kScale);
        m_parts.light(m_mat.navWhite, kAxis * (mz0 - 0.4f), 0.6f);
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
            for (int i = 0; i < 24; ++i) {
                const float t = kTwoPi * float(i) / 24.0f;
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
        const float bulkArc = rng.range(0.6f, 1.0f) * 2.0f;
        const float bulkHalf = bulkArc / m_R;
        RevolveOptions bulk = ring;
        bulk.around = Shading::Flat;
        const Profile bulkProf = scaledAbout(prof, center, 1.05f);
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
            m_parts.light(m_mat.navWhite, radial(spokeAngle(s)) * (m_R + a + 0.5f) + kAxis * z, 0.7f);
    }

    void buildTorusDetail(Rng& rng, float z, float a, float b, const std::vector<bool>& greenhouse, float bulkHalf)
    {
        // Window bands: thin strips hugging the tube's cross-section at angles t, one per deck facing
        // out. The profile starts on its outer face so the window texture's v spans exactly that face.
        const glm::vec2 c(m_R, z);
        const float bh = rng.range(0.6f, 0.9f);
        const float kOut = 1.0f + 0.05f / std::min(a, b), kIn = 1.0f - 0.05f / std::min(a, b);
        RevolveOptions band;
        band.around = Shading::Smooth;
        const float step = rng.range(4.0f, 5.5f) / std::min(a, b); // about one band per deck
        for (float t = -std::floor(0.75f / step) * step; t <= 0.75f + 1e-4f; t += step) {
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

        // Window rows on the outer wall, about one per deck
        const float bh = rng.range(0.7f, 1.1f);
        const float outerFlat = b - m_ringC;
        const int maxOuter = std::max(1, int(outerFlat * 2.0f / 4.0f));
        const int outerCount = rng.range((maxOuter + 1) / 2, maxOuter);
        std::vector<float> outerRows;
        for (int i = 0; i < outerCount; ++i)
            outerRows.push_back(outerFlat * (2.0f * (float(i) + 0.5f) / float(outerCount) - 1.0f) * 0.9f);
        ExtrudeOptions band;
        band.cap0 = band.cap1 = false;
        use(m_b, m_mat.windows, {bh * 4.0f, -1.0f});
        for (float off : outerRows)
            m_b.extrude(glm::mat4(1.0f), profile::circle(outerR + 0.04f, m_segments), z + off - bh * 0.5f,
                        z + off + bh * 0.5f, band);

        // Window rows on both side walls, one per deck where a deck has windows
        const float bh2 = rng.range(0.8f, 1.3f);
        const float sideSpan = a - m_ringC - bh2 * 0.5f - 0.3f;
        const float deck = rng.range(3.5f, 5.0f);
        std::vector<float> sideRows;
        for (float r = m_R + sideSpan; r >= m_R - sideSpan; r -= deck)
            if (sideRows.empty() || rng.chance(0.7f))
                sideRows.push_back(r);
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

                GreebleSettings s = m_parts.greebleSettings(0.12f, 0.75f);
                const float bulkU = bulkHalf * std::min(p0.x, p1.x) + 0.35f;
                if (inModule == 0)
                    s.keepOut.push_back({{-1e3f, -1e3f}, {-patch.halfSize.x + bulkU, 1e3f}});
                if (inModule == m_segsPerModule - 1)
                    s.keepOut.push_back({{patch.halfSize.x - bulkU, -1e3f}, {1e3f, 1e3f}});

                if (nrm.x > 0.9f) { // outer wall
                    s.density = 0.06f;
                    for (float off : outerRows)
                        s.keepOut.push_back({{-1e3f, off - bh * 0.5f - 0.3f}, {1e3f, off + bh * 0.5f + 0.3f}});
                } else if (nrm.x < -0.9f) { // inner wall, facing the hub
                    if (greenhouse[std::size_t(module)] || nearSpoke)
                        continue;
                } else if (std::abs(nrm.y) > 0.9f) { // side walls
                    const float sign = nrm.y > 0.0f ? -1.0f : 1.0f; // +Z wall runs outer -> inner
                    for (float rc : sideRows) {
                        const float v0 = sign * (rc - bh2 * 0.5f - m_R), v1 = sign * (rc + bh2 * 0.5f - m_R);
                        s.keepOut.push_back({{-1e3f, std::min(v0, v1) - 0.3f}, {1e3f, std::max(v0, v1) + 0.3f}});
                    }
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
        const float r0 = m_hubR + m_collarDepth - 0.3f;
        const float r1 = m_R - m_ringH * 0.5f + 0.4f;
        const int style = rng.range(0, 2); // 0 single tube, 1 twin tubes, 2 tube inside a truss
        const float rs = style == 1 ? m_spokeR * 0.75f : m_spokeR;
        const std::vector<float> offsets = style == 1 ? std::vector<float>{-rs * 1.4f, rs * 1.4f} : std::vector<float>{0.0f};
        const float ringSpacing = rng.range(5.0f, 8.0f) * 2.0f;
        const float endA = 3.0f, endB = 3.6f; // reinforced ends at the hub and the ring

        for (int s = 0; s < m_spokes; ++s) {
            const glm::vec3 dir = radial(spokeAngle(s));
            const glm::mat4 base = frameAlong(kAxis * z, dir, kAxis); // local Z radial, local Y = station axis

            for (float off : offsets) {
                const glm::mat4 f = base * at({0.0f, off, 0.0f});
                use(m_b, m_mat.hull, {8.0f, 10.0f});
                m_b.cylinder(f, rs, r0, r1, 20, false);
                use(m_b, m_mat.hullDark);
                m_b.cylinder(f, rs * 1.6f, r0, r0 + endA, 20);
                m_b.cylinder(f, rs * 1.6f, r1 - endB, r1, 20);
                for (float r = r0 + endA + ringSpacing; r < r1 - endB - 2.0f; r += ringSpacing)
                    m_b.cylinder(f, rs * 1.22f, r - 0.4f, r + 0.4f, 20);

                // Elevator cars travelling along the spoke
                const int cars = rng.range(0, 2);
                for (int c = 0; c < cars; ++c) {
                    const float t = rng.range(r0 + 8.0f, r1 - 10.0f);
                    use(m_b, m_mat.hullDark);
                    m_b.cylinder(f, rs * 1.5f, t - 2.6f, t + 2.6f, 20);
                    use(m_b, m_mat.windows, {2.8f, -1.0f});
                    m_b.cylinder(f, rs * 1.53f, t - 0.4f, t + 0.4f, 20, false);
                }
            }
            if (style == 2)
                m_parts.truss(base, r0 + endA, r1 - endB, rs * 1.7f, rs * 0.18f, rng.chance(0.5f));

            // Airlock block where the spoke meets the ring
            use(m_b, m_mat.hullDark);
            const float blockY = style == 1 ? rs * 6.0f : rs * 3.4f;
            m_b.box(base * at({0.0f, 0.0f, r1 - 1.4f}), {rs * 4.0f, std::min(blockY, m_ringW * 0.9f), 3.6f});
        }
    }

    Rng m_rng;
    ModelData& m_model;
    MeshBuilder m_b;
    ShipMaterials m_mat;
    HubSurfaces m_hub;
    ShipParts m_parts;

    float m_R = 0, m_ringW = 0, m_ringH = 0, m_ringC = 0;
    bool m_torus = false;
    int m_spokes = 0, m_modulesPerSpoke = 0, m_modules = 0, m_segsPerModule = 0, m_segments = 0;
    float m_hubR = 0, m_spokeR = 0, m_collarDepth = 0;
    int m_hubSides = 8;
    std::vector<float> m_ringZ;
    float m_hubZ0 = 0, m_hubZ1 = 0;

    int m_hangarSides = 4;
    float m_hangarR = 0, m_deckWidth = 0;
    float m_hangarZ0 = 0, m_hangarZ1 = 0;
    glm::vec2 m_slot{0.0f}; // half size of the entrance slot
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
