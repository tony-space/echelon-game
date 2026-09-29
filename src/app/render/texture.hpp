#pragma once

#include "render/gl.hpp"

#include <filesystem>
#include <stdexcept>

namespace ech {

class TextureError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// How the bytes of a texture are interpreted when sampled. Colour (albedo)
// textures are authored in sRGB and are decoded to linear by the sampler
// (GL_SRGB8_ALPHA8); data textures (masks, normals, lookup tables) are stored
// as-is (GL_RGBA8).
enum class ColorSpace { Srgb, Linear };

// 2D RGBA texture loaded through stb_image, mipmapped, repeat wrap.
class Texture {
public:
	explicit Texture(const std::filesystem::path& file, ColorSpace space = ColorSpace::Srgb);
	// Solid single-colour 1x1 texture (fallback for untextured surfaces).
	// The components are sRGB bytes, like the pixels of a PNG.
	Texture(unsigned char r, unsigned char g, unsigned char b, unsigned char a,
		ColorSpace space = ColorSpace::Srgb);
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	void bind(int unit) const;

	int width() const { return m_width; }
	int height() const { return m_height; }

private:
	GLuint m_id = 0;
	int m_width = 0;
	int m_height = 0;
};

} // namespace ech
