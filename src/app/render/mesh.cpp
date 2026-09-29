#include "render/mesh.hpp"

#include <glm/geometric.hpp>

#include <cstddef>

namespace ech {

void MeshData::addQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
	const glm::vec2& uvScale)
{
	const glm::vec3 n = glm::normalize(glm::cross(b - a, d - a));
	const auto base = static_cast<std::uint32_t>(vertices.size());
	vertices.push_back({a, n, glm::vec2(0.0f, 0.0f) * uvScale});
	vertices.push_back({b, n, glm::vec2(1.0f, 0.0f) * uvScale});
	vertices.push_back({c, n, glm::vec2(1.0f, 1.0f) * uvScale});
	vertices.push_back({d, n, glm::vec2(0.0f, 1.0f) * uvScale});
	indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshData::addBox(const glm::vec3& c, const glm::vec3& h)
{
	const glm::vec3 p[8] = {
		c + glm::vec3(-h.x, -h.y, -h.z), c + glm::vec3(h.x, -h.y, -h.z),
		c + glm::vec3(h.x, h.y, -h.z), c + glm::vec3(-h.x, h.y, -h.z),
		c + glm::vec3(-h.x, -h.y, h.z), c + glm::vec3(h.x, -h.y, h.z),
		c + glm::vec3(h.x, h.y, h.z), c + glm::vec3(-h.x, h.y, h.z),
	};
	addQuad(p[4], p[5], p[6], p[7]); // +Z
	addQuad(p[1], p[0], p[3], p[2]); // -Z
	addQuad(p[5], p[1], p[2], p[6]); // +X
	addQuad(p[0], p[4], p[7], p[3]); // -X
	addQuad(p[3], p[7], p[6], p[2]); // +Y
	addQuad(p[0], p[1], p[5], p[4]); // -Y
}

Mesh::Mesh(const MeshData& data)
	: m_indexCount(static_cast<GLsizei>(data.indices.size()))
{
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
		data.vertices.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.indices.size() * sizeof(std::uint32_t)),
		data.indices.data(), GL_STATIC_DRAW);

	const auto stride = static_cast<GLsizei>(sizeof(Vertex));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, position)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, normal)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, uv)));

	glBindVertexArray(0);
}

Mesh::~Mesh()
{
	if (m_ebo)
		glDeleteBuffers(1, &m_ebo);
	if (m_vbo)
		glDeleteBuffers(1, &m_vbo);
	if (m_vao)
		glDeleteVertexArrays(1, &m_vao);
}

void Mesh::draw() const
{
	glBindVertexArray(m_vao);
	glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
}

void Mesh::drawRange(std::uint32_t firstIndex, std::uint32_t indexCount) const
{
	glBindVertexArray(m_vao);
	const auto byteOffset = static_cast<std::uintptr_t>(firstIndex) * sizeof(std::uint32_t);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
		reinterpret_cast<const void*>(byteOffset));
	glBindVertexArray(0);
}

} // namespace ech
