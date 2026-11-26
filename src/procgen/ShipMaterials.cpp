#include "procgen/ShipMaterials.h"

#include <string>

namespace space {

MaterialDesc texturedMaterial(const std::string& name, const std::filesystem::path& texture, const glm::vec3& tint,
                              float specular, float shininess)
{
    MaterialDesc m;
    m.name = name;
    m.diffuse = tint;
    m.specular = glm::vec3(specular);
    m.shininess = shininess;
    m.diffuseMap = texture;
    return m;
}

namespace {

MaterialDesc glowing(const std::string& name, const glm::vec3& emissive)
{
    MaterialDesc m;
    m.name = name;
    m.diffuse = glm::vec3(0.05f);
    m.specular = glm::vec3(0.0f);
    m.emissive = emissive;
    return m;
}

} // namespace

ShipMaterials ShipMaterials::create(MeshBuilder& b, const std::filesystem::path& dir, Rng& rng)
{
    const std::vector<glm::vec3> hullTints = {
        {0.86f, 0.87f, 0.89f}, // light grey
        {0.88f, 0.85f, 0.80f}, // warm grey
        {0.76f, 0.81f, 0.88f}, // blue grey
        {0.60f, 0.62f, 0.65f}, // gunmetal
        {0.82f, 0.83f, 0.78f}, // olive white
    };
    const std::vector<glm::vec3> accents = {
        {0.90f, 0.42f, 0.08f}, // orange
        {0.72f, 0.12f, 0.10f}, // red
        {0.12f, 0.32f, 0.72f}, // blue
        {0.90f, 0.72f, 0.12f}, // yellow
        {0.16f, 0.50f, 0.30f}, // green
        {0.85f, 0.85f, 0.85f}, // white
    };
    const std::vector<glm::vec3> cargoTints = {
        {0.88f, 0.42f, 0.12f}, {0.16f, 0.36f, 0.70f}, {0.70f, 0.16f, 0.12f}, {0.85f, 0.85f, 0.82f},
        {0.20f, 0.52f, 0.36f}, {0.45f, 0.47f, 0.50f}, {0.12f, 0.55f, 0.60f}, {0.85f, 0.68f, 0.15f},
    };

    constexpr auto textured = texturedMaterial;
    const glm::vec3 hullTint = rng.pick(hullTints);
    const glm::vec3 accentTint = rng.pick(accents);

    ShipMaterials m;
    m.hullTint = hullTint;
    m.accentTint = accentTint;
    m.hull = {b.addMaterial(textured("hull", dir / "hull.png", hullTint, 0.35f, 24.0f)), {10.0f, 10.0f}};
    m.hullDark = {b.addMaterial(textured("hull_dark", dir / "hull.png", hullTint * 0.55f, 0.3f, 24.0f)), {8.0f, 8.0f}};
    m.accent = {b.addMaterial(textured("accent", dir / "hull.png", accentTint, 0.4f, 32.0f)), {10.0f, 10.0f}};
    m.greeble = {b.addMaterial(textured("greeble", dir / "greeble.png", hullTint, 0.4f, 32.0f)), {3.0f, 3.0f}};
    m.truss = {b.addMaterial(textured("truss", dir / "truss.png", glm::vec3(0.8f), 0.6f, 48.0f)), {0.6f, 2.5f}};
    m.nozzle = {b.addMaterial(textured("nozzle", dir / "nozzle.png", glm::vec3(1.0f), 0.7f, 64.0f)), {-1.0f, -1.0f}};

    m.engineGlow = {b.addMaterial(glowing("engine_glow", {3.2f, 3.4f, 4.0f})), {-1.0f, -1.0f}};
    m.thrusterGlow = {b.addMaterial(glowing("thruster_glow", {0.5f, 2.4f, 3.0f})), {-1.0f, -1.0f}};
    m.navRed = {b.addMaterial(glowing("nav_red", {5.0f, 0.3f, 0.2f})), {-1.0f, -1.0f}};
    m.navGreen = {b.addMaterial(glowing("nav_green", {0.3f, 5.0f, 0.6f})), {-1.0f, -1.0f}};
    m.navWhite = {b.addMaterial(glowing("nav_white", {5.0f, 5.0f, 5.0f})), {-1.0f, -1.0f}};
    m.lamp = {b.addMaterial(glowing("lamp", {2.4f, 2.0f, 1.4f})), {-1.0f, -1.0f}};

    MaterialDesc windows = textured("windows", dir / "windows.png", glm::vec3(1.0f), 0.8f, 96.0f);
    windows.emissive = glm::vec3(1.8f);
    windows.emissiveMap = dir / "windows_emit.png";
    m.windows = {b.addMaterial(windows), {4.0f, -1.0f}};

    m.solar = {b.addMaterial(textured("solar", dir / "solar.png", glm::vec3(1.0f), 0.9f, 96.0f)), {3.0f, 3.0f}};
    MaterialDesc greenhouse = textured("greenhouse", dir / "greenhouse.png", glm::vec3(1.0f), 0.8f, 96.0f);
    greenhouse.emissive = glm::vec3(0.7f);
    greenhouse.emissiveMap = dir / "greenhouse.png";
    m.greenhouse = {b.addMaterial(greenhouse), {4.0f, 4.0f}};

    MaterialDesc glass;
    glass.name = "porthole";
    glass.diffuse = glm::vec3(0.03f, 0.04f, 0.06f);
    glass.specular = glm::vec3(0.9f);
    glass.shininess = 128.0f;
    m.porthole = {b.addMaterial(glass), {-1.0f, -1.0f}};

    // A few cargo colors per ship, so each freighter has a recognizable load.
    const int cargoCount = rng.range(2, 4);
    for (int i = 0; i < cargoCount; ++i) {
        const glm::vec3 tint = rng.pick(cargoTints);
        m.cargo.push_back({b.addMaterial(textured("cargo" + std::to_string(i), dir / "cargo.png", tint, 0.3f, 24.0f)),
                           {6.0f, -1.0f}});
    }
    return m;
}

} // namespace space
