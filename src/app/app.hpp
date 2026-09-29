#pragma once

#include <filesystem>
#include <string>

namespace ech {

struct AppOptions {
	std::filesystem::path assetsDir;
	bool vsync = true;
	// Debug aid: render `screenshotFrame` frames, save the last one, exit.
	std::filesystem::path screenshotFile;
	int screenshotFrame = 30;
	float cameraDistance = 22.0f; // chase camera distance behind the craft, metres
	float cameraOrbitDeg = 0.0f;  // debug: rotate the camera around the craft
	bool showHiddenParts = false; // debug: draw parts hidden in the intact state
	std::string craftFile = "ha_bf1"; // mesh file name, see --craft
	int damageState = 0;              // initial Im state (keys 1..4 switch it)
};

// Runs the flight sandbox until the window closes. Returns a process exit code.
int runApp(const AppOptions& options);

} // namespace ech
