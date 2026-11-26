#include "render/Model.h"

#include "render/Shader.h"
#include "render/TextureCache.h"

#include <limits>
#include <utility>

namespace space {

Model::Model(const ModelData& data, TextureCache& textures)
{
    m_materials.reserve(data.materials.size());
    for (const MaterialDesc& desc : data.materials) {
        Material& m = m_materials.emplace_back();
        m.desc = desc;
        m.diffuseMap = desc.diffuseMap.empty() ? textures.white() : textures.get(desc.diffuseMap);
        m.emissiveMap = desc.emissiveMap.empty() ? textures.white() : textures.get(desc.emissiveMap);
    }

    m_min = glm::vec3(std::numeric_limits<float>::max());
    m_max = glm::vec3(std::numeric_limits<float>::lowest());
    m_subMeshes.reserve(data.subMeshes.size());
    for (const ModelData::SubMesh& sub : data.subMeshes) {
        if (sub.mesh.indices.empty())
            continue;
        for (const Vertex& v : sub.mesh.vertices) {
            m_min = glm::min(m_min, v.position);
            m_max = glm::max(m_max, v.position);
        }
        m_subMeshes.push_back({Mesh(sub.mesh.vertices, sub.mesh.indices), sub.materialIndex});
    }
    if (m_subMeshes.empty())
        m_min = m_max = glm::vec3(0.0f);
}

void Model::draw(const Shader& shader) const
{
    shader.set("uMaterial.diffuseMap", 0);
    shader.set("uMaterial.emissiveMap", 1);
    for (const SubMesh& sub : m_subMeshes) {
        const Material& mat = m_materials[sub.materialIndex];
        shader.set("uMaterial.ambient", mat.desc.ambient);
        shader.set("uMaterial.diffuse", mat.desc.diffuse);
        shader.set("uMaterial.specular", mat.desc.specular);
        shader.set("uMaterial.emissive", mat.desc.emissive);
        shader.set("uMaterial.shininess", mat.desc.shininess);
        mat.diffuseMap->bind(0);
        mat.emissiveMap->bind(1);
        sub.mesh.draw();
    }
}

void Model::attach(std::unique_ptr<Effect> effect)
{
    if (effect)
        m_effects.push_back(std::move(effect));
}

void Model::updateEffects(float dt)
{
    for (const auto& effect : m_effects)
        effect->update(dt);
}

void Model::drawEffects(const EffectContext& ctx) const
{
    for (const auto& effect : m_effects)
        effect->draw(ctx);
}

} // namespace space
