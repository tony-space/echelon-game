#include "render/model.hpp"

#include "render/gl.hpp"
#include "render/shader.hpp"

#include <echelon/core/log.hpp>
#include <echelon/math/color.hpp>

#include <boost/json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <format>
#include <fstream>
#include <sstream>

namespace ech {

namespace json = boost::json;

namespace {

struct EmeshSubset {
	std::uint32_t firstIndex;
	std::uint32_t indexCount;
	std::string texture;
	std::string material;
	glm::vec4 diffuse{1.0f};
	glm::vec3 specular{0.0f};
	float power = 0.0f;
};

constexpr std::uint32_t kEmeshVersion = 2;

struct Emesh {
	MeshData data;
	std::vector<EmeshSubset> subsets;
};

std::string readAll(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
		throw ModelError(std::format("cannot open '{}'", file.string()));
	std::ostringstream buf;
	buf << in.rdbuf();
	return buf.str();
}

template <class T>
T readPod(const std::string& bytes, std::size_t& pos, const std::filesystem::path& file)
{
	if (pos + sizeof(T) > bytes.size())
		throw ModelError(std::format("{}: truncated", file.string()));
	T v;
	std::memcpy(&v, bytes.data() + pos, sizeof(T));
	pos += sizeof(T);
	return v;
}

std::string readFixedString(const std::string& bytes, std::size_t& pos, std::size_t size,
	const std::filesystem::path& file)
{
	if (pos + size > bytes.size())
		throw ModelError(std::format("{}: truncated", file.string()));
	const char* p = bytes.data() + pos;
	pos += size;
	return std::string(p, ::strnlen(p, size));
}

// EMSH v2, see tools/legacy_export.py::write_emesh.
Emesh loadEmesh(const std::filesystem::path& file)
{
	const std::string bytes = readAll(file);
	std::size_t pos = 0;

	if (bytes.size() < 4 || bytes.compare(0, 4, "EMSH") != 0)
		throw ModelError(std::format("{}: not an EMSH file", file.string()));
	pos = 4;

	const auto version = readPod<std::uint32_t>(bytes, pos, file);
	if (version != kEmeshVersion)
		throw ModelError(std::format("{}: EMSH version {} (expected {}); re-run tools/legacy_export.py",
			file.string(), version, kEmeshVersion));
	const auto nVerts = readPod<std::uint32_t>(bytes, pos, file);
	const auto nIndices = readPod<std::uint32_t>(bytes, pos, file);
	const auto nSubsets = readPod<std::uint32_t>(bytes, pos, file);

	Emesh out;
	out.subsets.reserve(nSubsets);
	for (std::uint32_t i = 0; i < nSubsets; ++i) {
		EmeshSubset s;
		s.firstIndex = readPod<std::uint32_t>(bytes, pos, file);
		s.indexCount = readPod<std::uint32_t>(bytes, pos, file);
		s.texture = readFixedString(bytes, pos, 64, file);
		s.material = readFixedString(bytes, pos, 64, file);
		for (int k = 0; k < 4; ++k)
			s.diffuse[k] = readPod<float>(bytes, pos, file);
		for (int k = 0; k < 3; ++k)
			s.specular[k] = readPod<float>(bytes, pos, file);
		s.power = readPod<float>(bytes, pos, file);
		out.subsets.push_back(std::move(s));
	}

	out.data.vertices.resize(nVerts);
	for (std::uint32_t i = 0; i < nVerts; ++i) {
		Vertex& v = out.data.vertices[i];
		v.position.x = readPod<float>(bytes, pos, file);
		v.position.y = readPod<float>(bytes, pos, file);
		v.position.z = readPod<float>(bytes, pos, file);
		v.normal.x = readPod<float>(bytes, pos, file);
		v.normal.y = readPod<float>(bytes, pos, file);
		v.normal.z = readPod<float>(bytes, pos, file);
		v.uv.x = readPod<float>(bytes, pos, file);
		v.uv.y = readPod<float>(bytes, pos, file);
	}

	out.data.indices.resize(nIndices);
	for (std::uint32_t i = 0; i < nIndices; ++i) {
		const auto idx = readPod<std::uint32_t>(bytes, pos, file);
		if (idx >= nVerts)
			throw ModelError(std::format("{}: index {} out of range", file.string(), idx));
		out.data.indices[i] = idx;
	}
	for (const EmeshSubset& s : out.subsets) {
		if (s.firstIndex + s.indexCount > nIndices)
			throw ModelError(std::format("{}: subset out of range", file.string()));
	}
	return out;
}

const json::object& expectObject(const json::value& v, const std::string& what)
{
	if (const auto* o = v.if_object())
		return *o;
	throw ModelError(what + ": expected object");
}

} // namespace

Model::Model(const std::filesystem::path& modelJson, const std::filesystem::path& texturesDir, const Texture& fallback)
{
	boost::system::error_code ec;
	const json::value root = json::parse(readAll(modelJson), ec);
	if (ec)
		throw ModelError(std::format("{}: invalid JSON: {}", modelJson.string(), ec.message()));

	const json::object& top = expectObject(root, modelJson.string());
	const auto* partsVal = top.if_contains("parts");
	if (!partsVal || !partsVal->is_array())
		throw ModelError(std::format("{}: missing 'parts' array", modelJson.string()));

	const std::filesystem::path dir = modelJson.parent_path();
	for (const json::value& pv : partsVal->as_array()) {
		const json::object& po = expectObject(pv, "part");
		Part part;
		if (const auto* n = po.if_contains("name"); n && n->is_string())
			part.name = std::string(n->as_string());
		const auto* meshVal = po.if_contains("mesh");
		if (!meshVal || !meshVal->is_string())
			throw ModelError(std::format("{}: part '{}' has no mesh", modelJson.string(), part.name));
		if (const auto* vis = po.if_contains("visible"); vis && vis->is_bool())
			part.visible = vis->as_bool();
		if (const auto* ori = po.if_contains("orientation"); ori && ori->is_array() && ori->as_array().size() == 4) {
			const json::array& q = ori->as_array();
			// File stores (x, y, z, w); glm::quat is (w, x, y, z).
			part.orientation = glm::quat(static_cast<float>(q[3].to_number<double>()),
				static_cast<float>(q[0].to_number<double>()), static_cast<float>(q[1].to_number<double>()),
				static_cast<float>(q[2].to_number<double>()));
		}
		if (const auto* posVal = po.if_contains("position"); posVal && posVal->is_array()) {
			const json::array& a = posVal->as_array();
			if (a.size() == 3)
				part.offset = glm::vec3(static_cast<float>(a[0].to_number<double>()),
					static_cast<float>(a[1].to_number<double>()), static_cast<float>(a[2].to_number<double>()));
		}

		Emesh em = loadEmesh(dir / std::string(meshVal->as_string()));
		m_triangles += em.data.indices.size() / 3;
		part.mesh = std::make_unique<Mesh>(em.data);
		for (const EmeshSubset& s : em.subsets) {
			Subset sub;
			sub.firstIndex = s.firstIndex;
			sub.indexCount = s.indexCount;
			sub.texture = texture(s.texture, texturesDir, fallback);
			sub.material = s.material;
			sub.diffuse = s.diffuse;
			sub.specular = s.specular;
			sub.specularPower = s.power;
			part.subsets.push_back(std::move(sub));
		}
		m_parts.push_back(std::move(part));
	}

	log::info("model {}: {} parts, {} triangles, {} textures", modelJson.filename().string(), m_parts.size(),
		m_triangles, m_textures.size());
}

const Texture* Model::texture(const std::string& name, const std::filesystem::path& texturesDir,
	const Texture& fallback)
{
	if (name.empty())
		return &fallback;
	auto it = m_textures.find(name);
	if (it != m_textures.end())
		return it->second.get();
	const std::filesystem::path file = texturesDir / (name + ".png");
	try {
		auto tex = std::make_unique<Texture>(file);
		return m_textures.emplace(name, std::move(tex)).first->second.get();
	} catch (const TextureError& e) {
		log::warn("{}; using fallback", e.what());
		return &fallback;
	}
}

bool Model::setVisible(std::string_view name, bool visible)
{
	for (Part& part : m_parts) {
		if (part.name == name) {
			part.visible = visible;
			return true;
		}
	}
	return false;
}

void Model::draw(const Shader& shader, const glm::mat4& modelMatrix) const
{
	shader.set("uAlbedo", 0);

	const auto drawPass = [&](bool transparentPass) {
		for (const Part& part : m_parts) {
			if (!part.visible)
				continue;
			bool modelSet = false;
			for (const Subset& s : part.subsets) {
				if (s.transparent() != transparentPass)
					continue;
				if (!modelSet) {
					shader.set("uModel", glm::translate(modelMatrix, part.offset) * glm::mat4_cast(part.orientation));
					modelSet = true;
				}
				// D3D7 material colours multiplied gamma-encoded texels, so they
				// are sRGB-space factors; linearise like every other authored colour.
				shader.set("uTint", srgbToLinear(glm::vec3(s.diffuse)));
				shader.set("uAlpha", s.opacity());
				s.texture->bind(0);
				part.mesh->drawRange(s.firstIndex, s.indexCount);
			}
		}
	};

	drawPass(false);

	// Glass and the like: blend over the opaque geometry, keep depth test but
	// don't write depth so overlapping transparent faces don't punch holes.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	drawPass(true);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	shader.set("uTint", glm::vec3(1.0f));
	shader.set("uAlpha", 1.0f);
}

} // namespace ech
