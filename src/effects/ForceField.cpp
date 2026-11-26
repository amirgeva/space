#include "effects/ForceField.h"

#include "render/Shader.h"
#include "render/Texture.h"

#include <GL/glew.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace space {

namespace {

// Fraction of the noise texture shown at once, across the field's longer side.
constexpr float kWindow = 0.5f;

Mesh makeQuad(const glm::vec2& size)
{
    const glm::vec2 h = size * 0.5f;
    const glm::vec3 n(0.0f, 0.0f, 1.0f);
    const std::vector<Vertex> vertices = {
        {{-h.x, -h.y, 0.0f}, n, {0.0f, 0.0f}},
        {{h.x, -h.y, 0.0f}, n, {1.0f, 0.0f}},
        {{h.x, h.y, 0.0f}, n, {1.0f, 1.0f}},
        {{-h.x, h.y, 0.0f}, n, {0.0f, 1.0f}},
    };
    return Mesh(vertices, {0, 1, 2, 0, 2, 3});
}

} // namespace

ForceField::ForceField(std::shared_ptr<const Shader> shader, std::shared_ptr<const Texture> noise,
                       const glm::mat4& transform, const glm::vec2& size, unsigned seed)
    : m_shader(std::move(shader)), m_noise(std::move(noise)), m_transform(transform), m_quad(makeQuad(size)),
      m_rng(seed)
{
    // Keep the noise's aspect ratio: the window is kWindow wide along the longer side.
    const float longer = std::max(size.x, size.y);
    m_uvScale = kWindow * size / longer;
    update(0.0f);
}

void ForceField::update(float)
{
    // A new random window every frame; the texture repeats, so any offset works.
    std::uniform_real_distribution<float> offset(0.0f, 1.0f);
    m_uvOffset = {offset(m_rng), offset(m_rng)};
}

void ForceField::draw(const EffectContext& ctx) const
{
    m_shader->use();
    m_shader->set("uModelViewProj", ctx.viewProj * ctx.parent * m_transform);
    m_shader->set("uUvOffset", m_uvOffset);
    m_shader->set("uUvScale", m_uvScale);
    m_shader->set("uNoise", 0);
    m_noise->bind(0);

    // Additive, seen from both sides, and not hiding what is behind it
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    m_quad.draw();
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace space
