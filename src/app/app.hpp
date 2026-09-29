#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ech {

struct CameraPose {
	float x = 0.0f, y = 0.0f, z = 0.0f; // engine metres
	float yawDeg = 0.0f;                // 0 looks along -Z, positive turns right
	float pitchDeg = 0.0f;
};

struct AppOptions {
	std::filesystem::path assetsDir;
	bool vsync = true;
	// Debug aid: render `screenshotFrame` frames, save the last one, exit.
	std::filesystem::path screenshotFile;
	int screenshotFrame = 30;
	std::optional<CameraPose> cameraPose; // start pose instead of the exhibit view
	bool showHiddenParts = false;         // debug: draw parts hidden in the intact state
	int damageState = 0;                  // initial Im state (keys 1..4 switch it)
	std::string terrain = "Continent";    // assets/legacy/terrain/<name>.eterr
};

// Runs the asset sandbox (terrain + every exported craft) until the window
// closes. Returns a process exit code.
int runApp(const AppOptions& options);

} // namespace ech
