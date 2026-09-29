#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ech {

// Third-person chase camera: sits behind and above the target, looks slightly
// ahead of it, and lags a little so turns read on screen.
class ChaseCamera {
public:
	glm::vec3 position{0.0f};
	glm::vec3 target{0.0f};
	float fovDeg = 60.0f;
	float nearPlane = 0.5f;
	float farPlane = 20000.0f;

	void snapTo(const glm::vec3& craftPos, const glm::vec3& craftForward, const glm::vec3& craftUp)
	{
		m_offset = desiredOffset(craftForward, craftUp);
		position = craftPos + m_offset;
		target = lookTarget(craftPos, craftForward);
	}

	// The lag is applied to the offset in the craft's frame, not to the world
	// position: otherwise the camera would trail by speed / followRate metres.
	void follow(const glm::vec3& craftPos, const glm::vec3& craftForward, const glm::vec3& craftUp, float dt)
	{
		const glm::vec3 wanted = desiredOffset(craftForward, craftUp);
		const float k = 1.0f - glm::exp(-followRate * dt);
		m_offset += (wanted - m_offset) * k;
		position = craftPos + m_offset;
		target = lookTarget(craftPos, craftForward);
	}

	glm::mat4 view() const
	{
		return glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 projection(float aspect) const
	{
		return glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
	}

	float distanceBehind = 22.0f;
	float heightAbove = 6.0f;
	float lookAhead = 40.0f;
	float followRate = 6.0f; // 1/s
	float orbitDeg = 0.0f; // rotates the camera around the craft's up axis (debug views)

private:
	// Orbit views look at the craft itself; the chase view looks ahead of it.
	glm::vec3 lookTarget(const glm::vec3& craftPos, const glm::vec3& fwd) const
	{
		return orbitDeg != 0.0f ? craftPos : craftPos + fwd * lookAhead;
	}

	glm::vec3 desiredOffset(const glm::vec3& fwd, const glm::vec3& up) const
	{
		glm::vec3 back = -fwd * distanceBehind;
		if (orbitDeg != 0.0f)
			back = glm::vec3(glm::rotate(glm::mat4(1.0f), glm::radians(orbitDeg), up) * glm::vec4(back, 0.0f));
		return back + up * heightAbove;
	}

	glm::vec3 m_offset{0.0f};
};

} // namespace ech
