#include "app.hpp"

#include "platform/input.hpp"
#include "platform/window.hpp"
#include "render/camera.hpp"
#include "render/frame.hpp"
#include "render/gl.hpp"
#include "render/model.hpp"
#include "render/shader.hpp"
#include "render/terrain_renderer.hpp"
#include "render/texture.hpp"

#include <echelon/core/log.hpp>
#include <echelon/terrain/heightfield.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ech {

namespace {

// Flat airfield pad on Continent (BASEGRND texture, 81 m everywhere within
// ~500 m), engine x / z. The exhibit line is centred on it.
constexpr float kExhibitSpotX = 43840.0f;
constexpr float kExhibitSpotZ = -31488.0f;
constexpr float kExhibitGap = 12.0f; // metres between neighbouring wingtips

constexpr float kLookDegPerPixel = 0.12f;
constexpr float kBaseSpeed = 40.0f; // m/s, scaled by the mouse wheel and Shift / Ctrl

void saveScreenshot(const std::filesystem::path& file, int width, int height)
{
	std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	stbi_flip_vertically_on_write(1);
	const std::string name = file.string();
	if (stbi_write_png(name.c_str(), width, height, 4, pixels.data(), width * 4))
		log::info("screenshot saved to {}", name);
	else
		log::error("failed to write screenshot {}", name);
}

// One exported model per damage state (im0 intact ...). Missing states stay null:
// a part the original simply does not have in that state is absent from the file.
constexpr int kMaxDamageStates = 8;

struct CraftModels {
	std::array<std::unique_ptr<Model>, kMaxDamageStates> byState{};
	int shown = 0;

	Model* current() const { return byState[static_cast<std::size_t>(shown)].get(); }

	bool select(int state)
	{
		if (state < 0 || state >= kMaxDamageStates || !byState[static_cast<std::size_t>(state)])
			return false;
		shown = state;
		return true;
	}
};

CraftModels loadCraftModels(const std::filesystem::path& assets, const std::string& file, int preferred,
	const Texture& fallback, bool showHidden)
{
	CraftModels out;
	const auto textures = assets / "legacy/textures";
	for (int state = 0; state < kMaxDamageStates; ++state) {
		const auto json = assets / "legacy/models" / std::format("{}_im{}.model.json", file, state);
		if (!std::filesystem::exists(json))
			continue;
		try {
			auto model = std::make_unique<Model>(json, textures, fallback);
			if (showHidden)
				for (const auto& part : model->parts())
					model->setVisible(part.name, true);
			out.byState[static_cast<std::size_t>(state)] = std::move(model);
		} catch (const std::exception& e) {
			log::warn("legacy model failed: {}", e.what());
		}
	}
	if (!out.select(preferred))
		for (int state = 0; state < kMaxDamageStates; ++state)
			if (out.select(state))
				break;
	return out;
}

// Every craft exported by tools/legacy_export.py (<file>_im0.model.json), sorted by name.
std::vector<std::string> exportedCrafts(const std::filesystem::path& assets)
{
	std::vector<std::string> files;
	const auto dir = assets / "legacy/models";
	if (!std::filesystem::is_directory(dir))
		return files;
	constexpr std::string_view suffix = "_im0.model.json";
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		const std::string name = entry.path().filename().string();
		if (name.size() > suffix.size() && name.ends_with(suffix))
			files.push_back(name.substr(0, name.size() - suffix.size()));
	}
	std::sort(files.begin(), files.end());
	return files;
}

struct Exhibit {
	std::string file;
	CraftModels models;
	glm::vec3 position{0.0f};
};

struct Terrain {
	std::optional<Heightfield> field;
	std::unique_ptr<TerrainRenderer> renderer;

	float groundAt(float x, float z) const { return field ? field->heightAt(x, z) : 0.0f; }
};

Terrain loadTerrain(const AppOptions& options)
{
	Terrain t;
	const auto dir = options.assetsDir / "legacy/terrain";
	const auto heights = dir / (options.terrain + ".eterr");
	if (!std::filesystem::exists(heights)) {
		log::warn("no terrain '{}' under {} (run tools/terrain_export.py)", options.terrain, dir.string());
		return t;
	}
	try {
		const auto start = std::chrono::steady_clock::now();
		t.field.emplace(Heightfield::load(heights));
		t.renderer = std::make_unique<TerrainRenderer>(*t.field, dir / (options.terrain + ".terrain.json"),
			options.assetsDir / "legacy/textures", options.assetsDir / "shaders");
		log::info("terrain '{}' loaded in {:.2f} s", options.terrain,
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	} catch (const std::exception& e) {
		log::error("terrain '{}' failed: {}", options.terrain, e.what());
		t.renderer.reset();
		t.field.reset();
	}
	return t;
}

// Crafts side by side along +X, noses towards -Z, each resting on the ground.
std::vector<Exhibit> buildExhibits(const AppOptions& options, const Terrain& terrain, const Texture& fallback)
{
	std::vector<Exhibit> out;
	for (const std::string& file : exportedCrafts(options.assetsDir)) {
		CraftModels models =
			loadCraftModels(options.assetsDir, file, options.damageState, fallback, options.showHiddenParts);
		if (models.current())
			out.push_back({file, std::move(models), glm::vec3(0.0f)});
	}

	const auto width = [](const Exhibit& e) {
		const Model* m = e.models.byState[0] ? e.models.byState[0].get() : e.models.current();
		return glm::vec2(m->boundsMin().x, m->boundsMax().x);
	};
	float total = 0.0f;
	for (const Exhibit& e : out)
		total += width(e).y - width(e).x + kExhibitGap;

	float x = kExhibitSpotX - total * 0.5f;
	for (Exhibit& e : out) {
		const Model* m = e.models.current();
		const glm::vec2 span = width(e);
		const float cx = x - span.x;
		x += span.y - span.x + kExhibitGap;
		// Rest the lowest point on the highest ground under the footprint.
		float ground = terrain.groundAt(cx, kExhibitSpotZ);
		for (const float dx : {m->boundsMin().x, m->boundsMax().x})
			for (const float dz : {m->boundsMin().z, m->boundsMax().z})
				ground = std::max(ground, terrain.groundAt(cx + dx, kExhibitSpotZ + dz));
		e.position = glm::vec3(cx, ground - m->boundsMin().y, kExhibitSpotZ);
		log::info("exhibit {:12} at x {:.0f}, span {:.1f} m", e.file, cx, span.y - span.x);
	}
	return out;
}

FlyCamera initialCamera(const AppOptions& options, const Terrain& terrain, const std::vector<Exhibit>& exhibits)
{
	FlyCamera cam;
	if (options.cameraPose) {
		cam.position = glm::vec3(options.cameraPose->x, options.cameraPose->y, options.cameraPose->z);
		cam.yawDeg = options.cameraPose->yawDeg;
		cam.pitchDeg = options.cameraPose->pitchDeg;
		return cam;
	}
	// Front three-quarter view of the start of the line.
	const float firstX = exhibits.empty() ? kExhibitSpotX : exhibits.front().position.x;
	const float ground = terrain.groundAt(kExhibitSpotX, kExhibitSpotZ);
	cam.position = glm::vec3(firstX - 25.0f, ground + 9.0f, kExhibitSpotZ - 32.0f);
	cam.lookAt(glm::vec3(firstX + 45.0f, ground + 1.0f, kExhibitSpotZ));
	return cam;
}

} // namespace

int runApp(const AppOptions& options)
{
	try {
		const auto& assets = options.assetsDir;
		log::info("assets: {}", assets.string());

		WindowDesc wd;
		wd.title = "Echelon sandbox";
		wd.vsync = options.vsync;
		Window window(wd);
		Input input(window);

		Shader lit(assets / "shaders/lit.vert", assets / "shaders/lit.frag");
		Texture glassTex(150, 190, 230, 255);

		const Terrain terrain = loadTerrain(options);
		std::vector<Exhibit> exhibits = buildExhibits(options, terrain, glassTex);
		log::info("{} crafts on display", exhibits.size());

		FlyCamera camera = initialCamera(options, terrain, exhibits);
		float speedScale = 1.0f;

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_MULTISAMPLE);

		using clock = std::chrono::steady_clock;
		auto last = clock::now();
		double statTimer = 0.0;
		int statFrames = 0;
		int frameIndex = 0;
		const bool screenshotMode = !options.screenshotFile.empty();

		while (!window.shouldClose()) {
			window.pollEvents();

			const auto now = clock::now();
			float dt = std::chrono::duration<float>(now - last).count();
			last = now;
			dt = std::min(dt, 0.25f);

			input.update();
			if (input.quitRequested())
				window.requestClose();
			if (!screenshotMode) {
				if (input.resetRequested())
					camera = initialCamera(options, terrain, exhibits);
				const glm::vec2 look = input.lookDelta();
				camera.rotate(look.x * kLookDegPerPixel, -look.y * kLookDegPerPixel);
				speedScale = std::clamp(speedScale * std::pow(1.25f, input.scroll()), 0.05f, 400.0f);
				const glm::vec3 m = input.move();
				const glm::vec3 dir = camera.forward() * m.z + camera.right() * m.x + glm::vec3(0.0f, m.y, 0.0f);
				camera.position += dir * (kBaseSpeed * speedScale * input.speedFactor() * dt);
			}
			if (const int sel = input.damageSelect()) {
				int switched = 0;
				for (Exhibit& e : exhibits)
					switched += e.models.select(sel - 1) ? 1 : 0;
				log::info("damage state im{}: {} of {} crafts have it", sel - 1, switched, exhibits.size());
			}

			const int fbw = window.framebufferWidth();
			const int fbh = window.framebufferHeight();
			glViewport(0, 0, fbw, fbh);
			Frame f;
			f.view = camera.view();
			f.proj = camera.projection(window.aspect());
			f.sunDir = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
			f.cameraPos = camera.position;
			f.farPlane = camera.farPlane;

			// The clear colour goes through the same sRGB encode as the shaders.
			const glm::vec3 sky = f.atmosphere.sky();
			glClearColor(sky.r, sky.g, sky.b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			if (terrain.renderer)
				terrain.renderer->draw(f);

			f.apply(lit);
			lit.set("uTint", glm::vec3(1.0f));
			lit.set("uAlpha", 1.0f);
			for (const Exhibit& e : exhibits)
				if (const Model* m = e.models.current())
					m->draw(lit, glm::translate(glm::mat4(1.0f), e.position));

			if (screenshotMode && ++frameIndex >= options.screenshotFrame) {
				glFinish();
				saveScreenshot(options.screenshotFile, fbw, fbh);
				window.requestClose();
			}

			window.swapBuffers();

			statTimer += dt;
			++statFrames;
			if (statTimer >= 2.0) {
				const glm::vec3 p = camera.position;
				const auto stats = terrain.renderer ? terrain.renderer->stats() : TerrainRenderer::Stats{};
				log::info("{:.0f} fps | cam {:.0f} {:.0f} {:.0f} (agl {:.0f} m) yaw {:.0f} pitch {:.0f} | "
						  "terrain {} blocks, {:.1f}M tris, {} water",
					statFrames / statTimer, p.x, p.y, p.z, p.y - terrain.groundAt(p.x, p.z), camera.yawDeg,
					camera.pitchDeg, stats.blocks, static_cast<double>(stats.triangles) / 1e6, stats.waterBlocks);
				statTimer = 0.0;
				statFrames = 0;
			}
		}
		return 0;
	} catch (const std::exception& e) {
		log::error("{}", e.what());
		return 1;
	}
}

} // namespace ech
