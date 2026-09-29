#pragma once

#include "render/gl.hpp"
#include "render/shader.hpp"
#include "render/texture.hpp"

#include <echelon/terrain/heightfield.hpp>

#include <glm/vec3.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ech {

struct Frame;

class TerrainError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Draws a Heightfield on the GPU: heights, ground layers and water levels live
// in integer textures, geometry is generated in the vertex shader per block of
// kBlockQuads x kBlockQuads quads with a distance-based LOD (see terrain.vert).
class TerrainRenderer {
public:
	static constexpr int kBlockQuads = 128;

	// `descJson` is the <name>.terrain.json written by tools/terrain_export.py.
	TerrainRenderer(const Heightfield& field, const std::filesystem::path& descJson,
		const std::filesystem::path& texturesDir, const std::filesystem::path& shadersDir);
	~TerrainRenderer();

	TerrainRenderer(const TerrainRenderer&) = delete;
	TerrainRenderer& operator=(const TerrainRenderer&) = delete;

	void draw(const Frame& frame);

	struct Stats {
		int blocks = 0;
		int waterBlocks = 0;
		long long triangles = 0;
	};
	const Stats& stats() const { return m_stats; }

	float tilePeriod = 512.0f;   // metres per ground texture repeat
	float detailPeriod = 24.0f;  // metres per detail texture repeat
	float detailFade = 400.0f;   // metres
	float detailStrength = 0.5f;
	float maxDistance = 90000.0f;
	// Distance up to which full-resolution quads are drawn; about 16 px per
	// 64 m quad at 60 degrees FOV on a 1024 px wide view.
	float lodDistance = 4000.0f;

private:
	struct Block {
		glm::vec3 min{0.0f};
		glm::vec3 max{0.0f};
		bool water = false;
	};

	int lodStep(const Block& b, const glm::vec3& eye) const;

	const Heightfield& m_field;
	int m_blocksX = 0;
	int m_blocksY = 0;
	std::vector<Block> m_blocks;
	std::vector<int> m_steps;

	GLuint m_heights = 0;
	GLuint m_info = 0;
	GLuint m_water = 0;
	GLuint m_layers = 0;
	GLuint m_vao = 0;
	std::unique_ptr<Texture> m_detail;
	std::unique_ptr<Texture> m_waterTexture;
	glm::vec3 m_detailMean{1.0f};
	Shader m_terrainShader;
	Shader m_waterShader;
	Stats m_stats;
};

} // namespace ech
