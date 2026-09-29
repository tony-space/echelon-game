#pragma once

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace ech {

// Lighting runs in linear RGB: albedo textures are sampled as sRGB by the GPU,
// the default framebuffer is sRGB-encoded on write. Any colour constant that was
// picked by eye (sky, tints, UI) is an sRGB value and must be linearised once
// before it is handed to a shader.
inline float srgbToLinear(float c)
{
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linearToSrgb(float c)
{
	return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline glm::vec3 srgbToLinear(const glm::vec3& c)
{
	return glm::vec3(srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z));
}

inline glm::vec3 linearToSrgb(const glm::vec3& c)
{
	return glm::vec3(linearToSrgb(c.x), linearToSrgb(c.y), linearToSrgb(c.z));
}

// Beer-Lambert transmittance through a homogeneous medium: the fraction of the
// surface radiance that survives `distance` metres. Fog is 1 - transmittance
// and must be blended in linear RGB (the fixed-function "fog factor" curves of
// the 90s were only tuned to look right on gamma-encoded colours).
inline float fogTransmittance(float extinctionPerMetre, float distance)
{
	return std::exp(-extinctionPerMetre * distance);
}

// Same, through an exponential atmosphere: extinction sigma0 * exp(-y / H) at
// height y. Integrated analytically along the straight segment a -> b.
// Must match fogTransmittance() in the lit / terrain / water shaders.
inline float heightFogTransmittance(float sigma0, float scaleHeight, const glm::vec3& a, const glm::vec3& b)
{
	const float dist = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
	const float k = 1.0f / scaleHeight;
	const float x = (b.y - a.y) * k;
	const float ea = std::exp(-a.y * k);
	// Mean density factor along the segment; the limit for a level ray is exp(-y / H).
	const float safeX = std::copysign(std::max(std::abs(x), 1e-4f), x);
	const float mean = std::abs(x) < 1e-4f ? ea : (ea - std::exp(-b.y * k)) / safeX;
	return std::exp(-sigma0 * dist * mean);
}

} // namespace ech
