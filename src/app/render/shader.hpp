#pragma once

#include "render/gl.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace ech {

class ShaderError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Vertex + fragment program loaded from two GLSL files.
class Shader {
public:
	Shader(const std::filesystem::path& vertexFile, const std::filesystem::path& fragmentFile);
	~Shader();

	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;

	void use() const;

	void set(std::string_view name, int value) const;
	void set(std::string_view name, float value) const;
	void set(std::string_view name, const glm::vec3& value) const;
	void set(std::string_view name, const glm::mat4& value) const;

private:
	GLint location(std::string_view name) const;

	GLuint m_program = 0;
};

} // namespace ech
