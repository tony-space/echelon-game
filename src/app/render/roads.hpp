#pragma once

#include "render/mesh.hpp"
#include "render/texture.hpp"

#include <glm/vec3.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ech {

class Shader;

// Road ribbons for one map, from assets/legacy/scenes/<terrain>.roads.json:
//   { "roads": [ { "name": "Narrow Tarmac", "width": 40,
//                  "texture": "r_road",
//                  "points": [[x, y, z], ...] } ] }
// Points are engine metres. y is the authored height (usually 0); the ribbon
// is lifted to the heightfield so it sits on the ground. Bridges are not in
// this file — only Type="Road" polylines.
class Roads {
public:
	using GroundFn = std::function<float(float x, float z)>;

	Roads() = default;
	// `fallback` must outlive this object.
	Roads(const std::filesystem::path& roadsJson, const std::filesystem::path& texturesDir,
		const Texture& fallback, const GroundFn& ground);

	void draw(const Shader& shader) const;

	std::size_t roadCount() const { return m_roadCount; }

private:
	struct Batch {
		const Texture* texture = nullptr;
		std::unique_ptr<Mesh> mesh;
	};

	std::vector<std::unique_ptr<Texture>> m_owned;
	std::vector<Batch> m_batches;
	std::size_t m_roadCount = 0;
};

} // namespace ech
