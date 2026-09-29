#include "render/terrain_renderer.hpp"

#include "render/frame.hpp"

#include <echelon/core/log.hpp>
#include <echelon/math/color.hpp>

#include <boost/json.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <stb/stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace ech {

namespace json = boost::json;

namespace {

constexpr int kLayerSlots = 32;
constexpr int kLayerSize = 256;
constexpr std::uint8_t kLayerMask = 0x1F;

struct TerrainDesc {
	std::vector<std::string> layers; // empty string = no texture in that slot
	std::string detail;
	glm::vec3 detailMeanSrgb{0.5f};
	std::string water;
};

std::string readText(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
		throw TerrainError(std::format("cannot open '{}' (run tools/terrain_export.py)", file.string()));
	std::ostringstream buf;
	buf << in.rdbuf();
	return buf.str();
}

std::string stringOr(const json::object& o, std::string_view key)
{
	if (const auto* v = o.if_contains(key); v && v->is_string())
		return std::string(v->as_string());
	return {};
}

TerrainDesc loadDesc(const std::filesystem::path& file)
{
	boost::system::error_code ec;
	const json::value root = json::parse(readText(file), ec);
	if (ec || !root.is_object())
		throw TerrainError(std::format("{}: invalid JSON", file.string()));
	const json::object& o = root.as_object();
	TerrainDesc d;
	if (const auto* layers = o.if_contains("layers"); layers && layers->is_array())
		for (const json::value& v : layers->as_array())
			d.layers.push_back(v.is_string() ? std::string(v.as_string()) : std::string());
	d.detail = stringOr(o, "detail");
	d.water = stringOr(o, "water");
	if (const auto* m = o.if_contains("detail_mean_srgb"); m && m->is_array() && m->as_array().size() == 3) {
		const json::array& a = m->as_array();
		d.detailMeanSrgb = glm::vec3(static_cast<float>(a[0].to_number<double>()),
			static_cast<float>(a[1].to_number<double>()), static_cast<float>(a[2].to_number<double>()));
	}
	d.layers.resize(kLayerSlots);
	return d;
}

GLuint makeIntegerTexture(GLint internalFormat, GLenum format, GLenum type, int width, int height, const void* data)
{
	GLuint id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, data);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	// Integer textures are read with texelFetch only; they still need a
	// non-mipmapped filter to be complete.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	return id;
}

void setAnisotropy(GLenum target)
{
	if (!GLEW_EXT_texture_filter_anisotropic)
		return;
	GLfloat maxAniso = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAniso, 16.0f));
}

// All layer textures in one sRGB array, one slot per original layer index.
// Empty or unreadable slots are mid grey so a bad index stays visible but calm.
GLuint makeLayerArray(const TerrainDesc& desc, const std::filesystem::path& texturesDir)
{
	GLuint id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_SRGB8_ALPHA8, kLayerSize, kLayerSize, kLayerSlots, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, nullptr);
	const std::vector<unsigned char> grey(static_cast<std::size_t>(kLayerSize) * kLayerSize * 4, 128);
	stbi_set_flip_vertically_on_load(1);
	int loaded = 0;
	for (int slot = 0; slot < kLayerSlots; ++slot) {
		const std::string& name = desc.layers[static_cast<std::size_t>(slot)];
		stbi_uc* pixels = nullptr;
		if (!name.empty()) {
			const std::string file = (texturesDir / (name + ".png")).string();
			int w = 0, h = 0, channels = 0;
			pixels = stbi_load(file.c_str(), &w, &h, &channels, 4);
			if (!pixels) {
				log::warn("terrain layer {} '{}': {}", slot, file, stbi_failure_reason());
			} else if (w != kLayerSize || h != kLayerSize) {
				log::warn("terrain layer {} '{}' is {}x{}, expected {}", slot, file, w, h, kLayerSize);
				stbi_image_free(pixels);
				pixels = nullptr;
			}
		}
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot, kLayerSize, kLayerSize, 1, GL_RGBA, GL_UNSIGNED_BYTE,
			pixels ? pixels : grey.data());
		if (pixels) {
			stbi_image_free(pixels);
			++loaded;
		}
	}
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	setAnisotropy(GL_TEXTURE_2D_ARRAY);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	log::info("terrain: {} of {} layer textures loaded", loaded, kLayerSlots);
	return id;
}

std::unique_ptr<Texture> optionalTexture(const std::string& name, const std::filesystem::path& texturesDir,
	unsigned char r, unsigned char g, unsigned char b)
{
	if (!name.empty()) {
		try {
			return std::make_unique<Texture>(texturesDir / (name + ".png"));
		} catch (const TextureError& e) {
			log::warn("{}", e.what());
		}
	}
	return std::make_unique<Texture>(r, g, b, 255);
}

// Plane normals point inwards; a box is outside if it lies fully behind one.
std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& viewProj)
{
	const glm::mat4 m = glm::transpose(viewProj);
	std::array<glm::vec4, 6> p = {m[3] + m[0], m[3] - m[0], m[3] + m[1], m[3] - m[1], m[3] + m[2], m[3] - m[2]};
	for (glm::vec4& plane : p)
		plane /= glm::length(glm::vec3(plane));
	return p;
}

bool boxVisible(const std::array<glm::vec4, 6>& planes, const glm::vec3& lo, const glm::vec3& hi)
{
	for (const glm::vec4& p : planes) {
		const glm::vec3 v(p.x >= 0.0f ? hi.x : lo.x, p.y >= 0.0f ? hi.y : lo.y, p.z >= 0.0f ? hi.z : lo.z);
		if (glm::dot(glm::vec3(p), v) + p.w < 0.0f)
			return false;
	}
	return true;
}

} // namespace

TerrainRenderer::TerrainRenderer(const Heightfield& field, const std::filesystem::path& descJson,
	const std::filesystem::path& texturesDir, const std::filesystem::path& shadersDir)
	: m_field(field)
	, m_terrainShader(shadersDir / "terrain.vert", shadersDir / "terrain.frag")
	, m_waterShader(shadersDir / "water.vert", shadersDir / "water.frag")
{
	const TerrainDesc desc = loadDesc(descJson);
	const int w = field.width(), h = field.height();

	GLint maxSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
	if (w > maxSize || h > maxSize)
		throw TerrainError(std::format("terrain {}x{} exceeds GL_MAX_TEXTURE_SIZE {}", w, h, maxSize));

	// Per-sample info byte: ground layer from the cell's corner mask, plus the
	// diagonal flag.
	std::vector<std::uint8_t> info(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
	for (int j = 0; j < h; ++j)
		for (int i = 0; i < w; ++i)
			info[static_cast<std::size_t>(j) * w + i] = static_cast<std::uint8_t>(
				(field.layerAt(i, j) & kLayerMask) | (field.flags(i, j) & Heightfield::kDiagonalFlag));

	std::vector<std::int16_t> water(field.cells().size());
	for (std::size_t k = 0; k < water.size(); ++k)
		water[k] = field.cells()[k].waterRaw;

	m_heights = makeIntegerTexture(GL_R16I, GL_RED_INTEGER, GL_SHORT, w, h, field.heights().data());
	m_info = makeIntegerTexture(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, w, h, info.data());
	m_water = makeIntegerTexture(GL_R16I, GL_RED_INTEGER, GL_SHORT, field.cellsX(), field.cellsY(), water.data());
	m_layers = makeLayerArray(desc, texturesDir);
	m_detail = optionalTexture(desc.detail, texturesDir, 128, 128, 128);
	m_detailMean = desc.detail.empty() ? glm::vec3(srgbToLinear(0.5f)) : srgbToLinear(desc.detailMeanSrgb);
	m_waterTexture = optionalTexture(desc.water, texturesDir, 40, 70, 90);
	glGenVertexArrays(1, &m_vao);

	// Block bounds for culling and LOD; water raises the box where it lies above the ground.
	m_blocksX = (w - 1 + kBlockQuads - 1) / kBlockQuads;
	m_blocksY = (h - 1 + kBlockQuads - 1) / kBlockQuads;
	m_blocks.resize(static_cast<std::size_t>(m_blocksX) * m_blocksY);
	m_steps.assign(m_blocks.size(), 1);
	const int cellsPerBlock = kBlockQuads / Heightfield::kCellSamples;
	for (int by = 0; by < m_blocksY; ++by) {
		for (int bx = 0; bx < m_blocksX; ++bx) {
			float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
			for (int j = by * kBlockQuads; j <= (by + 1) * kBlockQuads; ++j)
				for (int i = bx * kBlockQuads; i <= (bx + 1) * kBlockQuads; ++i) {
					const float y = field.sampleHeight(i, j);
					lo = std::min(lo, y);
					hi = std::max(hi, y);
				}
			bool hasWater = false;
			for (int cj = by * cellsPerBlock; cj <= (by + 1) * cellsPerBlock; ++cj)
				for (int ci = bx * cellsPerBlock; ci <= (bx + 1) * cellsPerBlock; ++ci) {
					const TerrainCell& c = field.cell(ci, cj);
					const float level = c.waterRaw * field.heightScale();
					if (c.hasWater() && level > lo) {
						hasWater = true;
						hi = std::max(hi, level);
					}
				}
			Block& b = m_blocks[static_cast<std::size_t>(by) * m_blocksX + bx];
			const float s = field.spacing();
			b.min = glm::vec3(bx * kBlockQuads * s, lo, -(by + 1) * kBlockQuads * s);
			b.max = glm::vec3((bx + 1) * kBlockQuads * s, hi, -by * kBlockQuads * s);
			b.water = hasWater;
		}
	}
	log::info("terrain: {}x{} samples, {}x{} blocks", w, h, m_blocksX, m_blocksY);
}

TerrainRenderer::~TerrainRenderer()
{
	const GLuint textures[] = {m_heights, m_info, m_water, m_layers};
	glDeleteTextures(4, textures);
	if (m_vao)
		glDeleteVertexArrays(1, &m_vao);
}

int TerrainRenderer::lodStep(const Block& b, const glm::vec3& eye) const
{
	const glm::vec3 nearest = glm::clamp(eye, b.min, b.max);
	const float d = glm::length(eye - nearest);
	// Roughly constant screen-space quad size: double the step every doubling of
	// distance. lodDistance = spacing / (pixels per quad * radians per pixel).
	int step = 1;
	for (float limit = lodDistance; d > limit && step < kBlockQuads / 2; limit *= 2.0f)
		step *= 2;
	return step;
}

void TerrainRenderer::draw(const Frame& frame)
{
	m_stats = {};
	const glm::vec3 eye = frame.cameraPos;
	const auto planes = frustumPlanes(frame.proj * frame.view);

	for (std::size_t k = 0; k < m_blocks.size(); ++k)
		m_steps[k] = lodStep(m_blocks[k], eye);
	const auto stepAt = [&](int bx, int by, int fallback) {
		if (bx < 0 || by < 0 || bx >= m_blocksX || by >= m_blocksY)
			return fallback;
		return m_steps[static_cast<std::size_t>(by) * m_blocksX + bx];
	};

	std::vector<int> visible;
	visible.reserve(m_blocks.size());
	for (int by = 0; by < m_blocksY; ++by)
		for (int bx = 0; bx < m_blocksX; ++bx) {
			const int k = by * m_blocksX + bx;
			const Block& b = m_blocks[static_cast<std::size_t>(k)];
			const glm::vec3 nearest = glm::clamp(eye, b.min, b.max);
			if (glm::length(eye - nearest) > maxDistance || !boxVisible(planes, b.min, b.max))
				continue;
			visible.push_back(k);
		}

	glBindVertexArray(m_vao);

	const Shader& ts = m_terrainShader;
	frame.apply(ts);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_heights);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_info);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_layers);
	m_detail->bind(3);
	ts.set("uHeights", 0);
	ts.set("uInfo", 1);
	ts.set("uLayers", 2);
	ts.set("uDetail", 3);
	ts.set("uSpacing", m_field.spacing());
	ts.set("uHeightScale", m_field.heightScale());
	ts.set("uTilePeriod", tilePeriod);
	ts.set("uDetailPeriod", detailPeriod);
	ts.set("uDetailFade", detailFade);
	ts.set("uDetailStrength", detailStrength);
	ts.set("uDetailMean", m_detailMean);
	for (const int k : visible) {
		const int bx = k % m_blocksX, by = k / m_blocksX;
		const int step = m_steps[static_cast<std::size_t>(k)];
		const int quads = kBlockQuads / step;
		ts.set("uOrigin", glm::ivec2(bx * kBlockQuads, by * kBlockQuads));
		ts.set("uStep", step);
		ts.set("uQuads", quads);
		ts.set("uEdgeStep", glm::ivec4(std::max(step, stepAt(bx - 1, by, step)), std::max(step, stepAt(bx + 1, by, step)),
			std::max(step, stepAt(bx, by - 1, step)), std::max(step, stepAt(bx, by + 1, step))));
		glDrawArrays(GL_TRIANGLES, 0, quads * quads * 6);
		++m_stats.blocks;
		m_stats.triangles += 2LL * quads * quads;
	}

	const Shader& ws = m_waterShader;
	frame.apply(ws);
	const int cells = kBlockQuads / Heightfield::kCellSamples;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_water);
	m_waterTexture->bind(1);
	ws.set("uWater", 0);
	ws.set("uWaterTex", 1);
	ws.set("uCells", cells);
	ws.set("uCellSize", m_field.spacing() * Heightfield::kCellSamples);
	ws.set("uHeightScale", m_field.heightScale());
	ws.set("uTilePeriod", 256.0f);
	for (const int k : visible) {
		if (!m_blocks[static_cast<std::size_t>(k)].water)
			continue;
		const int bx = k % m_blocksX, by = k / m_blocksX;
		ws.set("uCellOrigin", glm::ivec2(bx * cells, by * cells));
		glDrawArrays(GL_TRIANGLES, 0, cells * cells * 6);
		++m_stats.waterBlocks;
	}

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE0);
}

} // namespace ech
