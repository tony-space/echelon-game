#pragma once

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace ech {

using FrustumPlanes = std::array<glm::vec4, 6>;

// Six inward-facing planes (xyz normal, w offset) of a clip-space frustum.
inline FrustumPlanes frustumPlanes(const glm::mat4& viewProj)
{
	const glm::mat4 m = glm::transpose(viewProj);
	FrustumPlanes p = {m[3] + m[0], m[3] - m[0], m[3] + m[1], m[3] - m[1], m[3] + m[2], m[3] - m[2]};
	for (glm::vec4& plane : p)
		plane /= glm::length(glm::vec3(plane));
	return p;
}

inline bool boxVisible(const FrustumPlanes& planes, const glm::vec3& lo, const glm::vec3& hi)
{
	for (const glm::vec4& p : planes) {
		const glm::vec3 v(p.x >= 0.0f ? hi.x : lo.x, p.y >= 0.0f ? hi.y : lo.y, p.z >= 0.0f ? hi.z : lo.z);
		if (glm::dot(glm::vec3(p), v) + p.w < 0.0f)
			return false;
	}
	return true;
}

inline bool sphereVisible(const FrustumPlanes& planes, const glm::vec3& centre, float radius)
{
	for (const glm::vec4& p : planes)
		if (glm::dot(glm::vec3(p), centre) + p.w < -radius)
			return false;
	return true;
}

} // namespace ech
