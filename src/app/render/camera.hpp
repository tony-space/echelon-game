#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace ech {

// Free-flying inspection camera. Yaw 0 looks along -Z (engine heading 0),
// positive yaw turns right; pitch is positive up.
class FlyCamera {
public:
	glm::vec3 position{0.0f};
	float yawDeg = 0.0f;
	float pitchDeg = 0.0f;
	float fovDeg = 60.0f;
	float nearPlane = 0.5f;
	float farPlane = 150000.0f; // depth is logarithmic, see lit.vert

	glm::vec3 forward() const
	{
		const float y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
		return glm::vec3(std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p));
	}
	glm::vec3 right() const
	{
		const float y = glm::radians(yawDeg);
		return glm::vec3(std::cos(y), 0.0f, std::sin(y));
	}

	void rotate(float dYawDeg, float dPitchDeg)
	{
		yawDeg = std::fmod(yawDeg + dYawDeg, 360.0f);
		pitchDeg = std::clamp(pitchDeg + dPitchDeg, -89.0f, 89.0f);
	}

	void lookAt(const glm::vec3& target)
	{
		const glm::vec3 d = target - position;
		const float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
		yawDeg = glm::degrees(std::atan2(d.x, -d.z));
		pitchDeg = std::clamp(glm::degrees(std::atan2(d.y, horizontal)), -89.0f, 89.0f);
	}

	glm::mat4 view() const { return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f)); }

	glm::mat4 projection(float aspect) const
	{
		return glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
	}
};

} // namespace ech
