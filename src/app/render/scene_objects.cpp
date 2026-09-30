#include "render/scene_objects.hpp"

#include "render/frame.hpp"
#include "render/frustum.hpp"
#include "render/shader.hpp"

#include <echelon/core/log.hpp>

#include <boost/json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

namespace ech {

namespace json = boost::json;

namespace {

// Deck sits this far above the higher of the recorded height and the ground at each end.
constexpr float kDeckLift = 0.5f;

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
		addInstance(*inst.model, inst.file, inst.matrix);
	}
	const std::size_t placed = m_instances.size();
	log::info("scene {}: {} objects, {} models{}", sceneJson.filename().string(), placed, m_models.size(),
		missing ? std::format(", {} without a model", missing) : std::string());

	const json::array* bridges = obj->if_contains("bridges") ? obj->at("bridges").if_array() : nullptr;
	if (!bridges)
		return;
	int spans = 0;
	for (const json::value& v : *bridges) {
		const json::object* b = v.if_object();
		if (!b || !b->if_contains("entrance") || !b->if_contains("section") || !b->if_contains("points")) {
			log::warn("scene {}: bridge without entrance/section/points skipped", sceneJson.filename().string());
			continue;
		}
		const auto entranceFile = json::value_to<std::string>(b->at("entrance"));
		const auto sectionFile = json::value_to<std::string>(b->at("section"));
		const Model* entrance = loadModel(entranceFile, modelsDir, texturesDir, fallback);
		const Model* section = loadModel(sectionFile, modelsDir, texturesDir, fallback);
		if (!entrance || !section)
			continue;
		const float deckY = b->if_contains("deck_y") ? static_cast<float>(b->at("deck_y").to_number<double>()) : 0.0f;
		const json::array* points = b->at("points").if_array();
		if (!points || points->size() < 2)
			continue;
		std::vector<glm::vec3> ends;
		for (const json::value& p : *points) {
			glm::vec3 e = readVec3(p, "bridge point");
			// The deck meets the roads that end here, and those lie on the ground. The
			// original takes max(recorded y, ground), but the canyon bridge by the
			// training centre records y = 400, some 300 m above both rims.
			if (ground)
				e.y = ground(e.x, e.z);
			e.y += kDeckLift;
			ends.push_back(e);
		}
		for (std::size_t i = 0; i + 1 < ends.size(); ++i) {
			placeSpan(*entrance, *section, entranceFile, sectionFile, ends[i], ends[i + 1], deckY);
			++spans;
		}
	}
	log::info("scene {}: {} bridge spans, {} pieces", sceneJson.filename().string(), spans, m_instances.size() - placed);
}

void SceneObjects::addInstance(const Model& model, const std::string& file, const glm::mat4& matrix)
{
	Instance inst;
	inst.model = &model;
	inst.file = file;
	inst.matrix = matrix;
	inst.position = glm::vec3(matrix[3]);
	const glm::vec3 lo = model.boundsMin(), hi = model.boundsMax();
	inst.centre = glm::vec3(matrix * glm::vec4(0.5f * (lo + hi), 1.0f));
	inst.radius = 0.5f * glm::length(hi - lo);
	m_instances.push_back(std::move(inst));
}

// Same layout as StormGame.dll 10014128..10014754. An entrance stands on each
// end point with its source +Z (engine -Z) towards the other end: the pier and
// abutment behind the end, the pylon and the deck reaching into the span. n =
// round((L - 2 * entrance max Z) / section length) sections follow, the first
// one's min Z against the entrance's max Z. Nothing is stretched: the middle may
// overlap the far entrance or leave a gap, as in the original; n <= 0 drops it.
// Z extents below are in the source frame, i.e. negated engine Z.
int SceneObjects::placeSpan(const Model& entrance, const Model& section, const std::string& entranceFile,
	const std::string& sectionFile, glm::vec3 a, glm::vec3 b, float deckY)
{
	const glm::vec3 diff = b - a;
	const float length = glm::length(diff);
	const float entranceMaxZ = -entrance.boundsMin().z;
	const float sectionMinZ = -section.boundsMax().z;
	const float sectionLength = section.boundsMax().z - section.boundsMin().z;
	if (length < 1.0f || sectionLength < 1.0f)
		return 0;
	const int sections = static_cast<int>(std::lround((length - 2.0f * entranceMaxZ) / sectionLength));

	const glm::vec3 forward = diff / length;
	const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
	// Local engine -Z along `inwards`; the cross axis stays horizontal, +Y follows the slope.
	const auto basis = [&](const glm::vec3& inwards) {
		const glm::vec3 z = -inwards;
		const glm::vec3 x = glm::normalize(glm::cross(worldUp, z));
		const glm::vec3 y = glm::cross(z, x);
		return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	};
	const glm::mat4 fromA = basis(forward);
	const glm::mat4 fromB = basis(-forward);
	const glm::vec3 up = glm::vec3(fromA[1]);
	// Origin at distance s along the span, dropped so that the deck (local y = deckY) meets the line a-b.
	const auto place = [&](float s, const glm::mat4& rot) {
		return glm::translate(glm::mat4(1.0f), a + forward * s - up * deckY) * rot;
	};

	int pieces = 0;
	addInstance(entrance, entranceFile, place(0.0f, fromA));
	++pieces;
	const float first = entranceMaxZ - sectionMinZ;
	for (int i = 0; i < sections; ++i) {
		addInstance(section, sectionFile, place(first + sectionLength * static_cast<float>(i), fromA));
		++pieces;
	}
	addInstance(entrance, entranceFile, place(length, fromB));
	++pieces;
	const float middle = length - 2.0f * entranceMaxZ - sectionLength * static_cast<float>(std::max(sections, 0));
	log::info("bridge {} m: {} sections, {} m {}", static_cast<int>(length), std::max(sections, 0),
		static_cast<int>(std::abs(middle)), middle < 0.0f ? "overlap" : "gap");
	return pieces;
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
