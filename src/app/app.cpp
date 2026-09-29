#include "app.hpp"

#include "platform/input.hpp"
#include "platform/window.hpp"
#include "render/camera.hpp"
#include "render/gl.hpp"
#include "render/mesh.hpp"
#include "render/model.hpp"
#include "render/placeholder_craft.hpp"
#include "render/shader.hpp"
#include "render/texture.hpp"

#include <echelon/core/log.hpp>
#include <echelon/data/craft_spec.hpp>
#include <echelon/math/color.hpp>
#include <echelon/sim/fixed_step.hpp>
#include <echelon/sim/flight.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb/stb_image_write.h>

#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>

namespace ech {

namespace {

FlightState initialState()
{
	FlightState s;
	s.position = glm::vec3(0.0f, 150.0f, 0.0f);
	s.speedMps = 120.0f;
	return s;
}

// Small screen-space cross drawn where the mouse reticle is.
MeshData makeReticle()
{
	MeshData m;
	const float a = 0.018f, t = 0.0025f;
	m.addQuad(glm::vec3(-a, -t, 0), glm::vec3(a, -t, 0), glm::vec3(a, t, 0), glm::vec3(-a, t, 0));
	m.addQuad(glm::vec3(-t, -a, 0), glm::vec3(t, -a, 0), glm::vec3(t, a, 0), glm::vec3(-t, a, 0));
	return m;
}

// Scene lighting. Colours are authored as sRGB (what you would pick in an
// image editor) and linearised once here; shaders only ever see linear RGB.
struct Atmosphere {
	glm::vec3 skySrgb{0.45f, 0.62f, 0.85f};       // clear colour and fog colour
	glm::vec3 sunSrgb{1.0f, 0.96f, 0.9f};
	glm::vec3 skyAmbientSrgb{0.45f, 0.55f, 0.75f};
	glm::vec3 groundAmbientSrgb{0.25f, 0.22f, 0.18f};
	float ambientScale = 0.45f;
	// Beer-Lambert extinction in 1/m. Optical depth 1 (37% of the surface
	// radiance left) at ~12 km, which reads like a hazy clear day.
	float fogExtinction = 0.000085f;

	glm::vec3 sky() const { return srgbToLinear(skySrgb); }
	glm::vec3 sun() const { return srgbToLinear(sunSrgb); }
	glm::vec3 skyAmbient() const { return srgbToLinear(skyAmbientSrgb) * ambientScale; }
	glm::vec3 groundAmbient() const { return srgbToLinear(groundAmbientSrgb) * ambientScale; }
};

struct Frame {
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec3 sunDir;
	glm::vec3 cameraPos;
	Atmosphere atmosphere;
};

void beginLit(const Shader& sh, const Frame& f, const glm::vec3& tint)
{
	sh.use();
	sh.set("uView", f.view);
	sh.set("uProj", f.proj);
	sh.set("uSunDir", f.sunDir);
	sh.set("uCameraPos", f.cameraPos);
	sh.set("uTint", tint);
	sh.set("uAlpha", 1.0f);
	sh.set("uAlbedo", 0);
	sh.set("uSunColor", f.atmosphere.sun());
	sh.set("uSkyAmbient", f.atmosphere.skyAmbient());
	sh.set("uGroundAmbient", f.atmosphere.groundAmbient());
	sh.set("uFogColor", f.atmosphere.sky());
	sh.set("uFogExtinction", f.atmosphere.fogExtinction);
}

void drawLit(const Shader& sh, const Mesh& mesh, const Texture& tex, const Frame& f, const glm::mat4& model,
	const glm::vec3& tint)
{
	beginLit(sh, f, tint);
	sh.set("uModel", model);
	tex.bind(0);
	mesh.draw();
}

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
	const Texture& fallback)
{
	CraftModels out;
	const auto textures = assets / "legacy/textures";
	int found = 0;
	for (int state = 0; state < kMaxDamageStates; ++state) {
		const auto json = assets / "legacy/models" / std::format("{}_im{}.model.json", file, state);
		if (!std::filesystem::exists(json))
			continue;
		try {
			out.byState[static_cast<std::size_t>(state)] = std::make_unique<Model>(json, textures, fallback);
			++found;
		} catch (const std::exception& e) {
			log::warn("legacy model failed: {}", e.what());
		}
	}
	if (found == 0) {
		log::info("no legacy model for '{}' under {}; using placeholder (run tools/legacy_export.py)", file,
			(assets / "legacy/models").string());
		return out;
	}
	if (!out.select(preferred)) {
		for (int state = 0; state < kMaxDamageStates; ++state)
			if (out.select(state))
				break;
		log::info("no im{} for '{}'; showing im{}", preferred, file, out.shown);
	}
	return out;
}

} // namespace

int runApp(const AppOptions& options)
{
	try {
		const auto& assets = options.assetsDir;
		log::info("assets: {}", assets.string());

		const CraftSpec craft = loadCraftSpec(assets / "data/crafts/human_bf1.json");
		log::info("craft '{}' ({}), max speed {} m/s", craft.name, craft.side, craft.flight.maxSpeedMps);

		WindowDesc wd;
		wd.title = "Echelon sandbox";
		wd.vsync = options.vsync;
		Window window(wd);
		Input input(window);

		Shader lit(assets / "shaders/lit.vert", assets / "shaders/lit.frag");
		Shader flat(assets / "shaders/flat.vert", assets / "shaders/flat.frag");
		Texture hullTex(assets / "textures/hull_checker.png");
		Texture groundTex(assets / "textures/ground_grid.png");

		Texture glassTex(150, 190, 230, 255);
		CraftModels craftModels = loadCraftModels(assets, options.craftFile, options.damageState, glassTex);
		Mesh craftMesh(makePlaceholderCraft());
		Mesh groundMesh(makeGroundPlane(40000.0f, 100.0f));
		Mesh reticleMesh(makeReticle());

		FlightState state = initialState();
		ChaseCamera camera;
		camera.distanceBehind = options.cameraDistance;
		camera.heightAbove = options.cameraDistance * 0.27f;
		camera.orbitDeg = options.cameraOrbitDeg;
		camera.snapTo(state.position, state.forward(), state.up());

		FixedStep stepper(1.0f / 120.0f);

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

			input.update(dt);
			if (screenshotMode) {
				// Deterministic pose: ignore the mouse, hold a gentle right turn so
				// the wing and fuselage are both visible in the shot.
				dt = 1.0f / 60.0f;
				FlightControls c = input.controls();
				c.aimYaw = 0.35f;
				c.aimPitch = 0.0f;
				input.overrideControls(c);
			}
			if (input.quitRequested())
				window.requestClose();
			if (input.resetRequested()) {
				state = initialState();
				camera.snapTo(state.position, state.forward(), state.up());
			}
			if (const int sel = input.damageSelect()) {
				if (craftModels.select(sel - 1))
					log::info("damage state im{}", craftModels.shown);
				else
					log::info("no mesh for im{}", sel - 1);
			}

			stepper.advance(dt);
			while (stepper.consume())
				stepFlight(craft.flight, state, input.controls(), stepper.step());

			camera.follow(state.position, state.forward(), state.up(), dt);

			const int fbw = window.framebufferWidth();
			const int fbh = window.framebufferHeight();
			glViewport(0, 0, fbw, fbh);
			Frame f;
			f.view = camera.view();
			f.proj = camera.projection(window.aspect());
			f.sunDir = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
			f.cameraPos = camera.position;

			// The clear colour goes through the same sRGB encode as the shaders.
			const glm::vec3 sky = f.atmosphere.sky();
			glClearColor(sky.r, sky.g, sky.b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			drawLit(lit, groundMesh, groundTex, f, glm::mat4(1.0f), srgbToLinear(glm::vec3(0.55f, 0.6f, 0.45f)));

			const glm::mat4 model = glm::translate(glm::mat4(1.0f), state.position) * glm::mat4_cast(state.orientation());
			if (Model* craftModel = craftModels.current()) {
				if (options.showHiddenParts)
					for (const auto& part : craftModel->parts())
						craftModel->setVisible(part.name, true);
				beginLit(lit, f, glm::vec3(1.0f));
				craftModel->draw(lit, model);
			} else {
				drawLit(lit, craftMesh, hullTex, f, model, srgbToLinear(glm::vec3(0.8f, 0.82f, 0.85f)));
			}

			// Reticle in NDC, on top of everything.
			glDisable(GL_DEPTH_TEST);
			flat.use();
			const glm::vec2 r = input.reticleNdc();
			glm::mat4 reticleModel = glm::translate(glm::mat4(1.0f), glm::vec3(r.x, r.y, 0.0f));
			reticleModel = glm::scale(reticleModel, glm::vec3(1.0f, window.aspect(), 1.0f));
			flat.set("uModel", reticleModel);
			flat.set("uColor", srgbToLinear(glm::vec3(0.2f, 1.0f, 0.3f)));
			reticleMesh.draw();
			glEnable(GL_DEPTH_TEST);

			if (screenshotMode && ++frameIndex >= options.screenshotFrame) {
				glFinish();
				saveScreenshot(options.screenshotFile, fbw, fbh);
				window.requestClose();
			}

			window.swapBuffers();

			statTimer += dt;
			++statFrames;
			if (statTimer >= 2.0) {
				log::info("{:.0f} fps | alt {:.0f} m | speed {:.0f} m/s | throttle {:.0f}% | hdg {:.0f}",
					statFrames / statTimer, state.position.y, state.speedMps, input.controls().throttle * 100.0f,
					state.headingDeg);
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
