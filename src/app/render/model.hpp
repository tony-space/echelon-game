#pragma once

#include "render/mesh.hpp"
#include "render/texture.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ech {

class Shader;

class ModelError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Multi-part textured model as written by tools/legacy_export.py:
// a .model.json describing parts and offsets, one .emesh per part.
class Model {
public:
	struct Subset {
		std::uint32_t firstIndex = 0;
		std::uint32_t indexCount = 0;
		const Texture* texture = nullptr;
		std::string material;             // original material name (informational)
		// diffuse.a is not a plain opacity in the original data: Metal has 2.0,
		// RMetal and Cockpit 0.0, Glass 0.4. Only values strictly inside (0, 1)
		// mark a translucent surface.
		glm::vec4 diffuse{1.0f};
		glm::vec3 specular{0.0f};
		float specularPower = 0.0f;
		bool transparent() const { return diffuse.a > 0.0f && diffuse.a < 1.0f; }
		float opacity() const { return transparent() ? diffuse.a : 1.0f; }
	};

	struct Part {
		std::string name;
		glm::vec3 offset{0.0f};
		glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z; absolute, root frame
		bool visible = true;
		std::unique_ptr<Mesh> mesh;
		std::vector<Subset> subsets;
	};

	// `texturesDir` holds the PNGs referenced by the meshes; `fallback` is used
	// for subsets without a texture and must outlive the model.
	Model(const std::filesystem::path& modelJson, const std::filesystem::path& texturesDir, const Texture& fallback);

	// Issues one draw per subset. The shader must already be bound with the
	// frame uniforms set; this updates uModel, uTint, uAlpha and the albedo
	// sampler. Opaque subsets go first; transparent ones are blended afterwards
	// with depth writes off.
	void draw(const Shader& shader, const glm::mat4& modelMatrix) const;

	std::vector<Part>& parts() { return m_parts; }
	const std::vector<Part>& parts() const { return m_parts; }
	// Toggles a sub-object by name (e.g. "cockpit_CoPilot"); returns false if absent.
	bool setVisible(std::string_view name, bool visible);
	std::size_t triangleCount() const { return m_triangles; }
	// Axis-aligned bounds of the parts visible at load time, in model space.
	glm::vec3 boundsMin() const { return m_boundsMin; }
	glm::vec3 boundsMax() const { return m_boundsMax; }

private:
	const Texture* texture(const std::string& name, const std::filesystem::path& texturesDir, const Texture& fallback);

	std::vector<Part> m_parts;
	std::unordered_map<std::string, std::unique_ptr<Texture>> m_textures;
	std::size_t m_triangles = 0;
	glm::vec3 m_boundsMin{std::numeric_limits<float>::max()};
	glm::vec3 m_boundsMax{std::numeric_limits<float>::lowest()};
};

} // namespace ech
