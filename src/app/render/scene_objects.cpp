#include "render/scene_objects.hpp"

#include "render/frame.hpp"
#include "render/frustum.hpp"
#include "render/shader.hpp"

#include <echelon/core/log.hpp>

#include <boost/json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace ech {

namespace json = boost::json;

namespace {

glm::vec3 readVec3(const json::value& v, const char* what)
{
	const json::array* a = v.if_array();
	if (!a || a->size() != 3)
		throw SceneObjectsError(std::format("{} must be [x, y, z]", what));
	return glm::vec3(static_cast<float>((*a)[0].to_number<double>()), static_cast<float>((*a)[1].to_number<double>()),
		static_cast<float>((*a)[2].to_number<double>()));
}

} // namespace

SceneObjects::SceneObjects(const std::filesystem::path& sceneJson, const std::filesystem::path& modelsDir,
	const std::filesystem::path& texturesDir, const Texture& fallback, const GroundFn& ground)
{
	std::ifstream in(sceneJson, std::ios::binary);
	if (!in)
		throw SceneObjectsError(std::format("cannot open '{}'", sceneJson.string()));
	std::ostringstream buf;
	buf << in.rdbuf();

	json::value root;
	try {
		root = json::parse(buf.str());
	} catch (const std::exception& e) {
		throw SceneObjectsError(std::format("{}: {}", sceneJson.string(), e.what()));
	}
	const json::object* obj = root.if_object();
	const json::array* objects = obj ? obj->if_contains("objects") ? obj->at("objects").if_array() : nullptr : nullptr;
	if (!objects)
		throw SceneObjectsError(std::format("{}: no 'objects' array", sceneJson.string()));

	int missing = 0;
	for (const json::value& v : *objects) {
		const json::object* o = v.if_object();
		if (!o || !o->if_contains("model") || !o->if_contains("position")) {
			log::warn("scene {}: entry without model/position skipped", sceneJson.filename().string());
			continue;
		}
		Instance inst;
		inst.file = json::value_to<std::string>(o->at("model"));
		inst.position = readVec3(o->at("position"), "position");
		if (const json::value* h = o->if_contains("heading_deg"))
			inst.headingDeg = static_cast<float>(h->to_number<double>());
		bool onGround = true;
		if (const json::value* g = o->if_contains("on_ground"))
			onGround = g->as_bool();
		if (onGround && ground)
			inst.position.y += ground(inst.position.x, inst.position.z);

		inst.model = loadModel(inst.file, modelsDir, texturesDir, fallback);
		if (!inst.model) {
			++missing;
			continue;
		}
		// Engine heading: 0 faces -Z, positive turns right, hence the minus (see FlightState::orientation).
		inst.matrix = glm::rotate(glm::translate(glm::mat4(1.0f), inst.position), glm::radians(-inst.headingDeg),
			glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 lo = inst.model->boundsMin(), hi = inst.model->boundsMax();
		inst.centre = glm::vec3(inst.matrix * glm::vec4(0.5f * (lo + hi), 1.0f));
		inst.radius = 0.5f * glm::length(hi - lo);
		m_instances.push_back(std::move(inst));
	}
	log::info("scene {}: {} objects, {} models{}", sceneJson.filename().string(), m_instances.size(),
		m_models.size(), missing ? std::format(", {} without a model", missing) : std::string());
}

const Model* SceneObjects::loadModel(const std::string& file, const std::filesystem::path& modelsDir,
	const std::filesystem::path& texturesDir, const Texture& fallback)
{
	if (const auto it = m_models.find(file); it != m_models.end())
		return it->second.get();
	std::unique_ptr<Model> model;
	const auto jsonFile = modelsDir / std::format("{}_im0.model.json", file);
	if (std::filesystem::exists(jsonFile)) {
		try {
			model = std::make_unique<Model>(jsonFile, texturesDir, fallback);
		} catch (const std::exception& e) {
			log::warn("scene model {} failed: {}", file, e.what());
		}
	} else {
		log::warn("scene model {} not exported ({})", file, jsonFile.string());
	}
	return m_models.emplace(file, std::move(model)).first->second.get();
}

void SceneObjects::draw(const Shader& shader, const Frame& frame)
{
	m_stats = {};
	const FrustumPlanes planes = frustumPlanes(frame.proj * frame.view);
	const float maxDist2 = drawDistance * drawDistance;
	for (const Instance& inst : m_instances) {
		const glm::vec3 d = inst.centre - frame.cameraPos;
		if (glm::dot(d, d) > maxDist2 || !sphereVisible(planes, inst.centre, inst.radius)) {
			++m_stats.culled;
			continue;
		}
		inst.model->draw(shader, inst.matrix);
		++m_stats.drawn;
	}
}

} // namespace ech
