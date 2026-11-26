#pragma once

#include <glm/glm.hpp>

namespace space {

// What an effect needs to draw itself in the current frame.
struct EffectContext {
    glm::mat4 viewProj{1.0f};
    glm::mat4 parent{1.0f}; // world transform of the object the effect is attached to
    glm::vec3 cameraPos{0.0f};
};

// A visual effect attached to an object (see Model::attach). Effects are drawn after all opaque
// geometry, each with its own shader and blending, and are updated once per frame.
class Effect {
public:
    virtual ~Effect() = default;

    virtual void update(float dt) = 0;
    virtual void draw(const EffectContext& ctx) const = 0;
};

} // namespace space
