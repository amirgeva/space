#include "effects/EffectFactory.h"

#include "effects/ForceField.h"
#include "render/Shader.h"
#include "render/Texture.h"

namespace space {

EffectFactory::EffectFactory(const std::filesystem::path& assetDir)
    : m_assets(assetDir)
{
}

EffectFactory::~EffectFactory() = default;

std::unique_ptr<Effect> EffectFactory::create(const EffectDesc& desc)
{
    switch (desc.type) {
    case EffectType::ForceField:
        // Loaded on first use, so models without force fields don't pay for them
        if (!m_forceFieldShader) {
            m_forceFieldShader = std::make_shared<Shader>(m_assets / "shaders/forcefield.vert",
                                                          m_assets / "shaders/forcefield.frag");
            m_forceFieldNoise = std::make_shared<Texture>(m_assets / "textures/effects/forcefield.png");
        }
        // Each field gets its own random sequence, so neighboring fields don't flicker in step
        return std::make_unique<ForceField>(m_forceFieldShader, m_forceFieldNoise, desc.transform, desc.size,
                                            m_seed++);
    }
    return nullptr;
}

} // namespace space
