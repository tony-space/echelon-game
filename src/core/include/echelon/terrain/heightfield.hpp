#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace ech {

class HeightfieldError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Ground texturing of one .vb cell (4 x 4 samples, 5 x 5 corner vertices).
struct TerrainCell {
	static constexpr std::uint8_t kNoLayer = 0xFF;
	static constexpr std::int16_t kNoWater = -2500;

	std::uint8_t layerA = 0;
	std::uint8_t layerB = kNoLayer;
	std::uint32_t mask = 0;          // bit (vy * 5 + vx): corner (vx, vy) uses layerB
	std::int16_t waterRaw = kNoWater; // water surface, * heightScale metres

	std::uint8_t layerAt(int vx, int vy) const
	{
		const bool second = (mask >> (vy * 5 + vx)) & 1u;
		return second && layerB != kNoLayer ? layerB : layerA;
	}
	bool hasWater() const { return waterRaw > kNoWater; }
};

// Terrain height field as exported by tools/terrain_export.py (.eterr v1).
// Sample (i, j) sits at engine position (i * spacing, h, -j * spacing):
// i runs along +X, j along the original +Z, which is -Z here.
class Heightfield {
public:
	static constexpr std::uint8_t kDiagonalFlag = 0x80; // quad (i,j)-(i+1,j+1) split along that diagonal
	static constexpr int kCellSamples = 4;

	Heightfield(int width, int height, float spacing, float heightScale, std::vector<std::int16_t> heights,
		std::vector<std::uint8_t> flags, int cellsX, int cellsY, std::vector<TerrainCell> cells);

	static Heightfield parse(std::span<const std::byte> bytes);
	static Heightfield load(const std::filesystem::path& file);

	int width() const { return m_width; }
	int height() const { return m_height; }
	int cellsX() const { return m_cellsX; }
	int cellsY() const { return m_cellsY; }
	float spacing() const { return m_spacing; }
	float heightScale() const { return m_heightScale; }

	// Clamped to the map edges.
	std::int16_t rawHeight(int i, int j) const { return m_heights[index(i, j)]; }
	float sampleHeight(int i, int j) const { return m_heights[index(i, j)] * m_heightScale; }
	std::uint8_t flags(int i, int j) const { return m_flags[index(i, j)]; }
	glm::vec3 samplePosition(int i, int j) const;
	const TerrainCell& cell(int ci, int cj) const;
	// Ground texture layer of a sample, resolved through its cell's corner mask.
	std::uint8_t layerAt(int i, int j) const;

	// Ground height under an engine-space point, interpolated over the same
	// triangle split the original GroundLevel uses.
	float heightAt(float x, float z) const;

	const std::vector<std::int16_t>& heights() const { return m_heights; }
	const std::vector<TerrainCell>& cells() const { return m_cells; }

private:
	std::size_t index(int i, int j) const;

	int m_width = 0;
	int m_height = 0;
	int m_cellsX = 0;
	int m_cellsY = 0;
	float m_spacing = 64.0f;
	float m_heightScale = 0.2f;
	std::vector<std::int16_t> m_heights;
	std::vector<std::uint8_t> m_flags;
	std::vector<TerrainCell> m_cells;
};

} // namespace ech
