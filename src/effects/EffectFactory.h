#pragma once

#include "effects/Effect.h"
#include "geometry/ModelData.h"

#include <filesystem>
#include <memory>

namespace space {

class Shader;
class Texture;

// Creates effects from their descriptions, sharing shaders and textures between them.
class EffectFactory {
public:
    explicit EffectFactory(const std::filesystem::path& assetDir);
    ~EffectFactory();

    std::unique_ptr<Effect> create(const EffectDesc& desc);

private:
    std::filesystem::path m_assets;
    std::shared_ptr<const Shader> m_forceFieldShader;
    std::shared_ptr<const Texture> m_forceFieldNoise;
    unsigned m_seed = 1;
};

} // namespace space
