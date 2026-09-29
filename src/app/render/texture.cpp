#include "render/texture.hpp"

#include <stb/stb_image.h>

#include <format>

namespace ech {

namespace {

GLint internalFormat(ColorSpace space)
{
	return space == ColorSpace::Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
}

} // namespace

Texture::Texture(const std::filesystem::path& file, ColorSpace space)
{
	stbi_set_flip_vertically_on_load(1);
	int channels = 0;
	const std::string name = file.string();
	stbi_uc* pixels = stbi_load(name.c_str(), &m_width, &m_height, &channels, 4);
	if (!pixels)
		throw TextureError(std::format("cannot load texture '{}': {}", name, stbi_failure_reason()));

	glGenTextures(1, &m_id);
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat(space), m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	// Mips of an sRGB texture are filtered in linear space by the driver.
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	stbi_image_free(pixels);
}

Texture::Texture(unsigned char r, unsigned char g, unsigned char b, unsigned char a, ColorSpace space)
	: m_width(1), m_height(1)
{
	const unsigned char px[4] = {r, g, b, a};
	glGenTextures(1, &m_id);
	glBindTexture(GL_TEXTURE_2D, m_id);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat(space), 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture()
{
	if (m_id)
		glDeleteTextures(1, &m_id);
}

void Texture::bind(int unit) const
{
	glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
	glBindTexture(GL_TEXTURE_2D, m_id);
}

} // namespace ech
