#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace space {

// Free-flying camera. Its orientation is a quaternion, so it can pitch and roll through any attitude
// without gimbal lock. Rotations are about the camera's own axes, in radians.
struct FlyCamera {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f}; // camera to world; the camera looks down its -Z

    void lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
    {
        position = eye;
        orientation = glm::quatLookAt(glm::normalize(target - eye), up);
    }

    glm::vec3 forward() const { return orientation * glm::vec3(0.0f, 0.0f, -1.0f); }

    void pitch(float angle) { rotateLocal(angle, {1.0f, 0.0f, 0.0f}); } // positive: nose up
    void yaw(float angle) { rotateLocal(angle, {0.0f, 1.0f, 0.0f}); }   // positive: turn left
    void roll(float angle) { rotateLocal(angle, {0.0f, 0.0f, -1.0f}); } // positive: roll right
    void move(float distance) { position += forward() * distance; }     // positive: forward

    glm::mat4 view() const
    {
        return glm::mat4_cast(glm::conjugate(orientation)) * glm::translate(glm::mat4(1.0f), -position);
    }

private:
    void rotateLocal(float angle, const glm::vec3& axis)
    {
        orientation = glm::normalize(orientation * glm::angleAxis(angle, axis));
    }
};

} // namespace space
