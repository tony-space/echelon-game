#pragma once

#include "render/gl.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

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
	void set(std::string_view name, const glm::ivec2& value) const;
	void set(std::string_view name, const glm::ivec4& value) const;
	void set(std::string_view name, float value) const;
	void set(std::string_view name, const glm::vec3& value) const;
	void set(std::string_view name, const glm::mat4& value) const;
	// True if the program uses the uniform; for shared per-frame parameters.
	bool has(std::string_view name) const;

private:
	GLint lookup(std::string_view name) const;
	struct NameHash {
		using is_transparent = void;
		std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
	};

	// Missing uniforms are cached too (as -1) so they warn only once.
	GLint location(std::string_view name) const;

	GLuint m_program = 0;
	mutable std::unordered_map<std::string, GLint, NameHash, std::equal_to<>> m_locations;
};

} // namespace ech
