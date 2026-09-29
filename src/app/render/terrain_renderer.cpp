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

	buildLevels();
	log::info("terrain: {}x{} samples, {} LOD levels, {}x{} top nodes of {} samples", w, h, m_levels.size(),
		m_levels.back().nodesX, m_levels.back().nodesY, m_levels.back().size);
}

// Bounds pyramid: level 0 nodes of kNodeQuads samples straight from the data,
// every coarser level merges 2x2 children. The y range is padded because
// morphing borrows heights from up to `step` samples outside the node.
void TerrainRenderer::buildLevels()
{
	const Heightfield& field = m_field;
	const int w = field.width(), h = field.height();
	constexpr float kPadY = 300.0f;

	Level fine;
	fine.size = kNodeQuads;
	fine.nodesX = (w - 1 + kNodeQuads - 1) / kNodeQuads;
	fine.nodesY = (h - 1 + kNodeQuads - 1) / kNodeQuads;
	fine.nodes.resize(static_cast<std::size_t>(fine.nodesX) * fine.nodesY);
	const int cellsPerNode = kNodeQuads / Heightfield::kCellSamples;
	for (int ny = 0; ny < fine.nodesY; ++ny)
		for (int nx = 0; nx < fine.nodesX; ++nx) {
			float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
			for (int j = ny * kNodeQuads; j <= (ny + 1) * kNodeQuads; ++j)
				for (int i = nx * kNodeQuads; i <= (nx + 1) * kNodeQuads; ++i) {
					const float y = field.sampleHeight(i, j);
					lo = std::min(lo, y);
					hi = std::max(hi, y);
				}
			bool hasWater = false;
			for (int cj = ny * cellsPerNode; cj <= (ny + 1) * cellsPerNode; ++cj)
				for (int ci = nx * cellsPerNode; ci <= (nx + 1) * cellsPerNode; ++ci) {
					const TerrainCell& c = field.cell(ci, cj);
					const float level = c.waterRaw * field.heightScale();
					if (c.hasWater() && level > lo) {
						hasWater = true;
						hi = std::max(hi, level);
					}
				}
			Node& n = fine.nodes[static_cast<std::size_t>(ny) * fine.nodesX + nx];
			n.minY = lo - kPadY;
			n.maxY = hi + kPadY;
			n.water = hasWater;
		}
	m_levels.push_back(std::move(fine));

	while (static_cast<int>(m_levels.size()) <= kMaxLevel && (m_levels.back().nodesX > 1 || m_levels.back().nodesY > 1)) {
		const Level& child = m_levels.back();
		Level parent;
		parent.size = child.size * 2;
		parent.nodesX = (child.nodesX + 1) / 2;
		parent.nodesY = (child.nodesY + 1) / 2;
		parent.nodes.resize(static_cast<std::size_t>(parent.nodesX) * parent.nodesY);
		for (int ny = 0; ny < parent.nodesY; ++ny)
			for (int nx = 0; nx < parent.nodesX; ++nx) {
				Node& p = parent.nodes[static_cast<std::size_t>(ny) * parent.nodesX + nx];
				p.minY = std::numeric_limits<float>::max();
				p.maxY = std::numeric_limits<float>::lowest();
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) {
						const int cx = nx * 2 + dx, cy = ny * 2 + dy;
						if (cx >= child.nodesX || cy >= child.nodesY)
							continue;
						const Node& c = child.at(cx, cy);
						p.minY = std::min(p.minY, c.minY);
						p.maxY = std::max(p.maxY, c.maxY);
						p.water = p.water || c.water;
					}
			}
		m_levels.push_back(std::move(parent));
	}
}

TerrainRenderer::~TerrainRenderer()
{
	const GLuint textures[] = {m_heights, m_info, m_water, m_layers};
	glDeleteTextures(4, textures);
	if (m_vao)
		glDeleteVertexArrays(1, &m_vao);
}

// CDLOD selection: a node is drawn at its own step when it lies entirely
// outside the range of the finer level; otherwise its children take over.
void TerrainRenderer::selectNodes(int level, int nx, int ny, const glm::vec3& eye,
	const std::array<glm::vec4, 6>& planes, std::vector<DrawItem>& out) const
{
	const Level& lv = m_levels[static_cast<std::size_t>(level)];
	if (nx >= lv.nodesX || ny >= lv.nodesY)
		return;
	const Node& node = lv.at(nx, ny);
	const float s = m_field.spacing();
	const int step = 1 << level;
	const glm::ivec2 origin(nx * lv.size, ny * lv.size);
	const glm::ivec2 end(std::min(origin.x + lv.size, m_field.width() - 1), std::min(origin.y + lv.size, m_field.height() - 1));
	const glm::vec3 lo(origin.x * s, node.minY, -end.y * s);
	const glm::vec3 hi(end.x * s, node.maxY, -origin.y * s);
	if (!boxVisible(planes, lo, hi))
		return;

	const float d = glm::length(eye - glm::clamp(eye, lo, hi));
	if (level > 0 && d < lodDistance * static_cast<float>(step) * 0.5f) {
		for (int dy = 0; dy < 2; ++dy)
			for (int dx = 0; dx < 2; ++dx)
				selectNodes(level - 1, nx * 2 + dx, ny * 2 + dy, eye, planes, out);
		return;
	}
	// A sibling may have forced the split while this node is already past its
	// level's range: draw it at the step its distance asks for. That is at
	// most a few doublings (the parent was in range), so the node stays a
	// proper grid of at least a few quads aligned to the coarser step.
	const int maxStep = std::min(1 << (static_cast<int>(m_levels.size()) - 1), lv.size / 2);
	int drawStep = step;
	while (drawStep < maxStep && d > lodDistance * static_cast<float>(drawStep))
		drawStep *= 2;
	DrawItem item;
	item.origin = origin;
	item.quads = glm::ivec2((end.x - origin.x + drawStep - 1) / drawStep, (end.y - origin.y + drawStep - 1) / drawStep);
	item.step = drawStep;
	item.water = node.water;
	item.coarsest = drawStep >= 1 << (static_cast<int>(m_levels.size()) - 1);
	out.push_back(item);
}

void TerrainRenderer::draw(const Frame& frame)
{
	m_stats = {};
	const glm::vec3 eye = frame.cameraPos;
	const auto planes = frustumPlanes(frame.proj * frame.view);

	std::vector<DrawItem> items;
	const Level& top = m_levels.back();
	for (int ny = 0; ny < top.nodesY; ++ny)
		for (int nx = 0; nx < top.nodesX; ++nx)
			selectNodes(static_cast<int>(m_levels.size()) - 1, nx, ny, eye, planes, items);

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
	ts.set("uLodDistance", lodDistance);
	ts.set("uMorphStart", morphStart);
	for (const DrawItem& it : items) {
		ts.set("uOrigin", it.origin);
		ts.set("uStep", it.step);
		ts.set("uQuads", it.quads);
		ts.set("uMorph", it.coarsest ? 0 : 1);
		glDrawArrays(GL_TRIANGLES, 0, it.quads.x * it.quads.y * 6);
		++m_stats.nodes;
		m_stats.triangles += 2LL * it.quads.x * it.quads.y;
	}

	// Water: one quad per .vb cell near the camera, coarser with the terrain step.
	const Shader& ws = m_waterShader;
	frame.apply(ws);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_water);
	m_waterTexture->bind(1);
	ws.set("uWater", 0);
	ws.set("uWaterTex", 1);
	ws.set("uCellSize", m_field.spacing() * Heightfield::kCellSamples);
	ws.set("uHeightScale", m_field.heightScale());
	ws.set("uTilePeriod", 256.0f);
	for (const DrawItem& it : items) {
		if (!it.water)
			continue;
		const int cellStep = std::max(1, it.step / Heightfield::kCellSamples);
		const glm::ivec2 cellOrigin = it.origin / Heightfield::kCellSamples;
		const glm::ivec2 cellEnd(std::min((it.origin.x + it.quads.x * it.step) / Heightfield::kCellSamples, m_field.cellsX() - 1),
			std::min((it.origin.y + it.quads.y * it.step) / Heightfield::kCellSamples, m_field.cellsY() - 1));
		const glm::ivec2 cells((cellEnd.x - cellOrigin.x + cellStep - 1) / cellStep,
			(cellEnd.y - cellOrigin.y + cellStep - 1) / cellStep);
		if (cells.x <= 0 || cells.y <= 0)
			continue;
		ws.set("uCellOrigin", cellOrigin);
		ws.set("uCells", cells);
		ws.set("uCellStep", cellStep);
		glDrawArrays(GL_TRIANGLES, 0, cells.x * cells.y * 6);
		++m_stats.waterNodes;
	}

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE0);
}

} // namespace ech
