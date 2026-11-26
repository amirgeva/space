#pragma once

#include "effects/Effect.h"
#include "geometry/ModelData.h"
#include "render/Material.h"
#include "render/Mesh.h"

#include <memory>
#include <vector>

namespace space {

class Shader;
class TextureCache;

// A renderable object on the GPU: one mesh per material, plus the materials themselves.
class Model {
public:
    struct SubMesh {
        Mesh mesh;
        std::size_t materialIndex;
    };

    // Uploads geometry and resolves material textures through `textures`.
    Model(const ModelData& data, TextureCache& textures);

    // Binds each submesh's material (uniforms "uMaterial.*", samplers on units 0 and 1)
    // and draws it.
    void draw(const Shader& shader) const;

    // Effects travel with the model. Draw them after all opaque geometry; ctx.parent is the model's
    // world transform.
    void attach(std::unique_ptr<Effect> effect);
    void updateEffects(float dt);
    void drawEffects(const EffectContext& ctx) const;

    const std::vector<SubMesh>& subMeshes() const { return m_subMeshes; }
    const std::vector<Material>& materials() const { return m_materials; }

    // Axis-aligned bounds in model space.
    glm::vec3 boundsMin() const { return m_min; }
    glm::vec3 boundsMax() const { return m_max; }

private:
    std::vector<SubMesh> m_subMeshes;
    std::vector<Material> m_materials;
    std::vector<std::unique_ptr<Effect>> m_effects;
    glm::vec3 m_min{0.0f};
    glm::vec3 m_max{0.0f};
};

} // namespace space
