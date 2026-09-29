#pragma once

#include "render/gl.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace ech {

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
};

struct MeshData {
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	// Appends a quad (two triangles) with a flat normal; corners counter-clockwise.
	void addQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
		const glm::vec2& uvScale = glm::vec2(1.0f));
	// Appends an axis-aligned box centred at `center`.
	void addBox(const glm::vec3& center, const glm::vec3& halfExtents);
};

// Static indexed triangle mesh on the GPU.
class Mesh {
public:
	explicit Mesh(const MeshData& data);
	~Mesh();

	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;

	void draw() const;
	// Draws indices [firstIndex, firstIndex + indexCount).
	void drawRange(std::uint32_t firstIndex, std::uint32_t indexCount) const;

	std::uint32_t indexCount() const { return static_cast<std::uint32_t>(m_indexCount); }

private:
	GLuint m_vao = 0;
	GLuint m_vbo = 0;
	GLuint m_ebo = 0;
	GLsizei m_indexCount = 0;
};

} // namespace ech
