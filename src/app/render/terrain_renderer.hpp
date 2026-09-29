#pragma once

#include "render/gl.hpp"
#include "render/shader.hpp"
#include "render/texture.hpp"

#include <echelon/terrain/heightfield.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
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
// in integer textures, geometry is generated in the vertex shader.
//
// Level of detail is a CDLOD-style quadtree. Step S is meant for ground between
// lodDistance * S / 2 and lodDistance * S from the camera. A node of level k
// covers kNodeQuads * 2^k samples; it is split while it is closer than
// lodDistance * 2^(k-1), otherwise drawn whole at the step its own distance
// asks for (a sibling may have forced the split, so that can be coarser than
// 2^k). Vertices that the next coarser step does not have morph onto the
// coarser grid over the outer part of the step's range, so switching steps
// moves nothing and nodes of different steps meet without cracks or stitching
// (see terrain.vert).
class TerrainRenderer {
public:
	static constexpr int kNodeQuads = 16;
	static constexpr int kMaxLevel = 6; // coarsest step 64

	// `descJson` is the <name>.terrain.json written by tools/terrain_export.py.
	TerrainRenderer(const Heightfield& field, const std::filesystem::path& descJson,
		const std::filesystem::path& texturesDir, const std::filesystem::path& shadersDir);
	~TerrainRenderer();

	TerrainRenderer(const TerrainRenderer&) = delete;
	TerrainRenderer& operator=(const TerrainRenderer&) = delete;

	void draw(const Frame& frame);

	struct Stats {
		int nodes = 0;
		int waterNodes = 0;
		long long triangles = 0;
	};
	const Stats& stats() const { return m_stats; }

	float tilePeriod = 512.0f;   // metres per ground texture repeat
	float detailPeriod = 24.0f;  // metres per detail texture repeat
	float detailFade = 400.0f;   // metres
	float detailStrength = 0.5f;
	// Distance up to which full-resolution quads are drawn; about 16 px per
	// 64 m quad at 60 degrees FOV on a 1024 px wide view. Every doubling of
	// the distance doubles the quad step; nothing is dropped by distance.
	float lodDistance = 4000.0f;
	// Fraction of a step's range where its vertices start morphing onto the
	// next coarser grid. A node drawn at step S has vertices up to
	// lodDistance * S + its diagonal from the camera, and the step-2S neighbour
	// there must not have started its own morph yet:
	// diagonal < (2 * morphStart - 1) * lodDistance * S. With kNodeQuads = 16
	// the flat diagonal is 1448 * S m and up to ~1500 m of height range adds to
	// it, against 2400 * S m.
	float morphStart = 0.8f;

private:
	struct Node {
		float minY = 0.0f;
		float maxY = 0.0f;
		bool water = false;
	};
	struct Level {
		int nodesX = 0;
		int nodesY = 0;
		int size = 0; // node edge in samples
		std::vector<Node> nodes;
		const Node& at(int nx, int ny) const { return nodes[static_cast<std::size_t>(ny) * nodesX + nx]; }
	};
	struct DrawItem {
		glm::ivec2 origin; // first sample
		glm::ivec2 quads;  // quads per axis (clipped at the map edge)
		int step = 1;
		bool water = false;
		bool coarsest = false;
	};

	void buildLevels();
	void selectNodes(int level, int nx, int ny, const glm::vec3& eye, const std::array<glm::vec4, 6>& planes,
		std::vector<DrawItem>& out) const;

	const Heightfield& m_field;
	std::vector<Level> m_levels; // [0] finest (step 1)

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
