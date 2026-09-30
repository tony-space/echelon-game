#include "render/roads.hpp"

#include "render/mesh.hpp"
#include "render/shader.hpp"

#include <echelon/core/log.hpp>

#include <boost/json.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

namespace ech {

namespace json = boost::json;

namespace {

constexpr float kSampleStep = 16.0f; // metres between cross-sections
constexpr float kLift = 0.5f;        // above the heightfield at every vertex
// Written depth is pulled this fraction closer (see lit.frag). Slivers of a
// 64 m terrain cell that still poke through between sections lose the test.
constexpr float kDepthBias = 0.0015f;

glm::vec3 readVec3(const json::value& v)
{
	const json::array* a = v.if_array();
	if (!a || a->size() != 3)
		return glm::vec3(0.0f);
	return glm::vec3(static_cast<float>((*a)[0].to_number<double>()), static_cast<float>((*a)[1].to_number<double>()),
		static_cast<float>((*a)[2].to_number<double>()));
}

glm::vec2 flat(const glm::vec3& p)
{
	return glm::vec2(p.x, p.z);
}

// Right of a normalised XZ direction. cross(forward, right) points up.
glm::vec2 rightOf(const glm::vec2& forward)
{
	return glm::vec2(forward.y, -forward.x);
}

glm::vec2 normalised(const glm::vec2& v)
{
	const float len = glm::length(v);
	return len > 1.0e-4f ? v / len : glm::vec2(0.0f, 1.0f);
}

// Centreline resampled so each step is at most kSampleStep. y keeps the
// authored value; the ground is sampled per vertex when the ribbon is built.
std::vector<glm::vec3> resample(const std::vector<glm::vec3>& points)
{
	std::vector<glm::vec3> out;
	if (points.size() < 2)
		return out;
	auto push = [&](const glm::vec3& p) {
		if (!out.empty() && glm::length(flat(p - out.back())) < 0.5f)
			return;
		out.push_back(p);
	};
	push(points.front());
	for (std::size_t i = 1; i < points.size(); ++i) {
		const glm::vec3 a = points[i - 1];
		const glm::vec3 b = points[i];
		const float len = glm::length(flat(b - a));
		const int steps = std::max(1, static_cast<int>(std::ceil(len / kSampleStep)));
		for (int s = 1; s <= steps; ++s)
			push(glm::mix(a, b, static_cast<float>(s) / static_cast<float>(steps)));
	}
	return out.size() >= 2 ? out : std::vector<glm::vec3>{};
}

// Three vertices per cross-section (left, centre, right), each dropped onto the
// heightfield where it stands, so the ribbon follows a side slope instead of
// cutting into it.
void appendRibbon(MeshData& mesh, const std::vector<glm::vec3>& line, float width, const Roads::GroundFn& ground)
{
	const float half = std::max(width, 1.0f) * 0.5f;
	const float repeat = std::max(width, 8.0f);
	const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
	const glm::vec3 up(0.0f, 1.0f, 0.0f);
	const auto settle = [&](glm::vec3 p, float authored) {
		p.y = std::max(ground ? ground(p.x, p.z) : 0.0f, authored) + kLift;
		return p;
	};
	float distance = 0.0f;
	for (std::size_t i = 0; i < line.size(); ++i) {
		if (i > 0)
			distance += glm::length(flat(line[i] - line[i - 1]));
		glm::vec2 dir;
		if (i == 0)
			dir = normalised(flat(line[1] - line[0]));
		else if (i + 1 == line.size())
			dir = normalised(flat(line[i] - line[i - 1]));
		else {
			const glm::vec2 in = normalised(flat(line[i] - line[i - 1]));
			const glm::vec2 out = normalised(flat(line[i + 1] - line[i]));
			dir = normalised(in + out);
		}
		glm::vec2 side = rightOf(dir);
		if (i > 0 && i + 1 < line.size()) {
			const float denom = glm::dot(side, rightOf(normalised(flat(line[i + 1] - line[i]))));
			const float scale = std::abs(denom) > 0.2f ? 1.0f / denom : 1.0f;
			side *= std::clamp(scale, 0.5f, 3.0f);
		}
		const glm::vec3 offset(side.x * half, 0.0f, side.y * half);
		const float v = distance / repeat;
		const float authored = line[i].y;
		mesh.vertices.push_back({settle(line[i] - offset, authored), up, glm::vec2(0.0f, v)});
		mesh.vertices.push_back({settle(line[i], authored), up, glm::vec2(0.5f, v)});
		mesh.vertices.push_back({settle(line[i] + offset, authored), up, glm::vec2(1.0f, v)});
	}
	for (std::uint32_t i = 0; i + 1 < static_cast<std::uint32_t>(line.size()); ++i) {
		const std::uint32_t l0 = base + i * 3;
		const std::uint32_t c0 = l0 + 1;
		const std::uint32_t r0 = l0 + 2;
		const std::uint32_t l1 = l0 + 3;
		const std::uint32_t c1 = l0 + 4;
		const std::uint32_t r1 = l0 + 5;
		// a → b1 → b0 is counter-clockwise from above (cross(forward, right) = up).
		mesh.indices.insert(mesh.indices.end(), {l0, c1, c0, l0, l1, c1, c0, r1, r0, c0, c1, r1});
	}
}

} // namespace

Roads::Roads(const std::filesystem::path& roadsJson, const std::filesystem::path& texturesDir,
	const Texture& fallback, const GroundFn& ground)
{
	std::ifstream in(roadsJson, std::ios::binary);
	if (!in)
		throw std::runtime_error(std::format("cannot open '{}'", roadsJson.string()));
	std::ostringstream buf;
	buf << in.rdbuf();

	json::value root;
	try {
		root = json::parse(buf.str());
	} catch (const std::exception& e) {
		throw std::runtime_error(std::format("{}: {}", roadsJson.string(), e.what()));
	}
	const json::object* obj = root.if_object();
	const json::array* roads = obj && obj->if_contains("roads") ? obj->at("roads").if_array() : nullptr;
	if (!roads)
		throw std::runtime_error(std::format("{}: no 'roads' array", roadsJson.string()));

	// One mesh per texture, so a bind covers every ribbon that shares it.
	struct Acc {
		const Texture* texture = nullptr;
		MeshData data;
		int count = 0;
	};
	std::vector<std::pair<std::string, Acc>> groups;

	for (const json::value& v : *roads) {
		const json::object* road = v.if_object();
		if (!road || !road->if_contains("points") || !road->if_contains("width"))
			continue;
		const json::array* pts = road->at("points").if_array();
		if (!pts || pts->size() < 2)
			continue;
		std::vector<glm::vec3> points;
		points.reserve(pts->size());
		for (const json::value& p : *pts)
			points.push_back(readVec3(p));
		const auto line = resample(points);
		if (line.size() < 2)
			continue;

		std::string texName;
		if (const json::value* t = road->if_contains("texture"))
			texName = json::value_to<std::string>(*t);
		const float width = static_cast<float>(road->at("width").to_number<double>());

		Acc* acc = nullptr;
		for (auto& [name, group] : groups) {
			if (name == texName) {
				acc = &group;
				break;
			}
		}
		if (!acc) {
			const Texture* texture = &fallback;
			const auto file = texturesDir / (texName + ".png");
			if (!texName.empty() && std::filesystem::exists(file)) {
				try {
					m_owned.push_back(std::make_unique<Texture>(file));
					texture = m_owned.back().get();
				} catch (const std::exception& e) {
					log::warn("road texture {}: {}", texName, e.what());
				}
			} else if (!texName.empty()) {
				log::warn("road texture {} not exported ({})", texName, file.string());
			}
			groups.emplace_back(texName, Acc{texture, {}, 0});
			acc = &groups.back().second;
		}
		appendRibbon(acc->data, line, width, ground);
		++acc->count;
		++m_roadCount;
	}

	for (auto& [name, acc] : groups) {
		if (acc.data.indices.empty())
			continue;
		Batch batch;
		batch.texture = acc.texture;
		batch.mesh = std::make_unique<Mesh>(acc.data);
		m_batches.push_back(std::move(batch));
		log::info("roads {}: {} polylines, {} triangles", name.empty() ? "(no texture)" : name, acc.count,
			acc.data.indices.size() / 3);
	}
}

void Roads::draw(const Shader& shader) const
{
	shader.set("uModel", glm::mat4(1.0f));
	shader.set("uAlbedo", 0);
	shader.set("uTint", glm::vec3(1.0f));
	shader.set("uAlpha", 1.0f);
	// Ground decal: tested against the terrain with a bias, never written, so
	// overlapping ribbons at junctions cannot fight and later geometry paints
	// over the road as usual.
	shader.set("uDepthBias", kDepthBias);
	glDepthMask(GL_FALSE);
	for (const Batch& batch : m_batches) {
		batch.texture->bind(0);
		batch.mesh->draw();
	}
	glDepthMask(GL_TRUE);
	shader.set("uDepthBias", 0.0f);
}

} // namespace ech
