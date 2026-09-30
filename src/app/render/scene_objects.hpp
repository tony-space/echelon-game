#pragma once

#include "render/model.hpp"
#include "render/texture.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ech {

class Shader;
struct Frame;

class SceneObjectsError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Static decorations placed on a map: buildings, bridges, towers... Each entry
// references an exported model by file name and is drawn at a world pose.
//
// Loaded from assets/legacy/scenes/<terrain>.scene.json:
//   { "terrain": "Continent",
//     "objects": [ { "model": "bunker3",           // assets/legacy/models/<model>_im0.model.json
//                    "position": [x, y, z],        // engine metres; see on_ground
//                    "heading_deg": 0.0,           // 0 faces -Z, positive turns right (engine heading)
//                    "on_ground": true } ] }       // y becomes an offset above the terrain
// Missing fields default to heading 0, on_ground true.
class SceneObjects {
public:
	using GroundFn = std::function<float(float x, float z)>;

	struct Instance {
		const Model* model = nullptr;
		std::string file;
		glm::vec3 position{0.0f};
		float headingDeg = 0.0f;
		glm::mat4 matrix{1.0f};
		glm::vec3 centre{0.0f}; // world-space bounding sphere
		float radius = 0.0f;
	};

	struct Stats {
		int drawn = 0;
		int culled = 0;
	};

	SceneObjects() = default;
	// `fallback` must outlive this object.
	SceneObjects(const std::filesystem::path& sceneJson, const std::filesystem::path& modelsDir,
		const std::filesystem::path& texturesDir, const Texture& fallback, const GroundFn& ground);

	// Frame uniforms must already be applied to `shader`.
	void draw(const Shader& shader, const Frame& frame);

	std::size_t modelCount() const { return m_models.size(); }
	const std::vector<Instance>& instances() const { return m_instances; }
	const Stats& stats() const { return m_stats; }

	// Beyond this the object is skipped; buildings are small next to the map.
	float drawDistance = 30000.0f;

private:
	const Model* loadModel(const std::string& file, const std::filesystem::path& modelsDir,
		const std::filesystem::path& texturesDir, const Texture& fallback);

	std::unordered_map<std::string, std::unique_ptr<Model>> m_models;
	std::vector<Instance> m_instances;
	Stats m_stats;
};

} // namespace ech
