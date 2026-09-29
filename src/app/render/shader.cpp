#include "render/shader.hpp"

#include <echelon/core/log.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace ech {

namespace {

std::string readFile(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
		throw ShaderError(std::format("cannot open shader '{}'", file.string()));
	std::ostringstream buf;
	buf << in.rdbuf();
	return buf.str();
}

GLuint compile(GLenum type, const std::string& source, const std::filesystem::path& file)
{
	const GLuint shader = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		GLint len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		std::string info(static_cast<std::size_t>(std::max(len, 1)), '\0');
		glGetShaderInfoLog(shader, len, nullptr, info.data());
		glDeleteShader(shader);
		throw ShaderError(std::format("{}:\n{}", file.string(), info.c_str()));
	}
	return shader;
}

} // namespace

Shader::Shader(const std::filesystem::path& vertexFile, const std::filesystem::path& fragmentFile)
{
	const GLuint vs = compile(GL_VERTEX_SHADER, readFile(vertexFile), vertexFile);
	GLuint fs = 0;
	try {
		fs = compile(GL_FRAGMENT_SHADER, readFile(fragmentFile), fragmentFile);
	} catch (...) {
		glDeleteShader(vs);
		throw;
	}

	m_program = glCreateProgram();
	glAttachShader(m_program, vs);
	glAttachShader(m_program, fs);
	glLinkProgram(m_program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = GL_FALSE;
	glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
	if (!ok) {
		GLint len = 0;
		glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
		std::string info(static_cast<std::size_t>(std::max(len, 1)), '\0');
		glGetProgramInfoLog(m_program, len, nullptr, info.data());
		glDeleteProgram(m_program);
		m_program = 0;
		throw ShaderError(std::format("link {} + {}:\n{}", vertexFile.string(), fragmentFile.string(), info.c_str()));
	}
}

Shader::~Shader()
{
	if (m_program)
		glDeleteProgram(m_program);
}

void Shader::use() const
{
	glUseProgram(m_program);
}

GLint Shader::lookup(std::string_view name) const
{
	if (const auto it = m_locations.find(name); it != m_locations.end())
		return it->second;
	const std::string n(name);
	const GLint loc = glGetUniformLocation(m_program, n.c_str());
	m_locations.emplace(n, loc);
	return loc;
}

GLint Shader::location(std::string_view name) const
{
	const bool known = m_locations.contains(name);
	const GLint loc = lookup(name);
	if (loc < 0 && !known)
		log::warn("uniform '{}' not found", name);
	return loc;
}

bool Shader::has(std::string_view name) const
{
	return lookup(name) >= 0;
}

void Shader::set(std::string_view name, int value) const
{
	glUniform1i(location(name), value);
}

void Shader::set(std::string_view name, const glm::ivec2& value) const
{
	glUniform2iv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::ivec4& value) const
{
	glUniform4iv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, float value) const
{
	glUniform1f(location(name), value);
}

void Shader::set(std::string_view name, const glm::vec3& value) const
{
	glUniform3fv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::mat4& value) const
{
	glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace ech
