#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace space {

// Camera orbiting a target point; yaw/pitch in radians.
struct OrbitCamera {
    glm::vec3 target{0.0f};
    float distance = 10.0f;
    float yaw = 0.6f;
    float pitch = 0.3f;

    void rotate(float dYaw, float dPitch)
    {
        yaw += dYaw;
        pitch = std::clamp(pitch + dPitch, -1.5f, 1.5f);
    }

    void zoom(float steps) { distance = std::max(1.0f, distance * std::pow(0.88f, steps)); }

    glm::vec3 position() const
    {
        const glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        return target + dir * distance;
    }

    glm::mat4 view() const { return glm::lookAt(position(), target, glm::vec3(0.0f, 1.0f, 0.0f)); }
};

} // namespace space
