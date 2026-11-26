#pragma once

#include "effects/Effect.h"
#include "render/Mesh.h"

#include <memory>
#include <random>

namespace space {

class Shader;
class Texture;

// Flickering energy screen: a quad showing a different random window of a tiling noise texture
// every frame, blended additively and visible from both sides.
class ForceField : public Effect {
public:
    // `transform` places the quad (local XY plane, centered) relative to the parent object.
    ForceField(std::shared_ptr<const Shader> shader, std::shared_ptr<const Texture> noise, const glm::mat4& transform,
               const glm::vec2& size, unsigned seed);

    void update(float dt) override;
    void draw(const EffectContext& ctx) const override;

private:
    std::shared_ptr<const Shader> m_shader;
    std::shared_ptr<const Texture> m_noise;
    glm::mat4 m_transform;
    Mesh m_quad;
    std::mt19937 m_rng;
    glm::vec2 m_uvOffset{0.0f};
    glm::vec2 m_uvScale{1.0f};
};

} // namespace space
