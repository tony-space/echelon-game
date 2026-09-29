#pragma once

#include "render/shader.hpp"

#include <echelon/math/color.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cmath>

namespace ech {

// Scene lighting. Colours are authored as sRGB (what you would pick in an
// image editor) and linearised once here; shaders only ever see linear RGB.
struct Atmosphere {
	glm::vec3 skySrgb{0.45f, 0.62f, 0.85f};       // clear colour and fog colour
	glm::vec3 sunSrgb{1.0f, 0.96f, 0.9f};
	glm::vec3 skyAmbientSrgb{0.45f, 0.55f, 0.75f};
	glm::vec3 groundAmbientSrgb{0.25f, 0.22f, 0.18f};
	float ambientScale = 0.45f;
	// Beer-Lambert extinction in 1/m at sea level. Optical depth 1 (37% of the
	// surface radiance left) at ~12 km, which reads like a hazy clear day.
	float fogExtinction = 0.000085f;
	// Haze thins out with altitude as exp(-y / H); without it everything seen
	// from a few km up drowns in the same fog as at ground level.
	float fogScaleHeight = 2500.0f;

	glm::vec3 sky() const { return srgbToLinear(skySrgb); }
	glm::vec3 sun() const { return srgbToLinear(sunSrgb); }
	glm::vec3 skyAmbient() const { return srgbToLinear(skyAmbientSrgb) * ambientScale; }
	glm::vec3 groundAmbient() const { return srgbToLinear(groundAmbientSrgb) * ambientScale; }
};

// Per-frame shader parameters shared by every lit program.
struct Frame {
	glm::mat4 view{1.0f};
	glm::mat4 proj{1.0f};
	glm::vec3 sunDir{0.0f, 1.0f, 0.0f};
	glm::vec3 cameraPos{0.0f};
	float farPlane = 1.0f;
	Atmosphere atmosphere;

	// Binds `sh` and sets the frame uniforms the program declares.
	void apply(const Shader& sh) const
	{
		sh.use();
		const auto set = [&sh](const char* name, const auto& value) {
			if (sh.has(name))
				sh.set(name, value);
		};
		set("uView", view);
		set("uProj", proj);
		set("uSunDir", sunDir);
		set("uCameraPos", cameraPos);
		set("uSunColor", atmosphere.sun());
		set("uSkyAmbient", atmosphere.skyAmbient());
		set("uGroundAmbient", atmosphere.groundAmbient());
		set("uFogColor", atmosphere.sky());
		set("uFogExtinction", atmosphere.fogExtinction);
		set("uFogScaleHeight", atmosphere.fogScaleHeight);
		set("uLogDepth", 2.0f / std::log2(farPlane + 1.0f));
	}
};

} // namespace ech
