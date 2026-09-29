#include "app.hpp"

#include <echelon/core/log.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv)
{
	ech::AppOptions options;
	options.assetsDir = ECH_DEFAULT_ASSETS_DIR;

	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
			options.assetsDir = argv[++i];
		} else if (std::strcmp(argv[i], "--no-vsync") == 0) {
			options.vsync = false;
		} else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
			options.screenshotFile = argv[++i];
		} else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
			options.screenshotFrame = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--cam-dist") == 0 && i + 1 < argc) {
			options.cameraDistance = static_cast<float>(std::atof(argv[++i]));
		} else if (std::strcmp(argv[i], "--cam-orbit") == 0 && i + 1 < argc) {
			options.cameraOrbitDeg = static_cast<float>(std::atof(argv[++i]));
		} else if (std::strcmp(argv[i], "--show-hidden") == 0) {
			options.showHiddenParts = true;
		} else if (std::strcmp(argv[i], "--craft") == 0 && i + 1 < argc) {
			options.craftFile = argv[++i];
		} else if (std::strcmp(argv[i], "--damage") == 0 && i + 1 < argc) {
			options.damageState = std::atoi(argv[++i]);
		} else {
			ech::log::error("unknown argument '{}'. Usage: echelon [--assets <dir>] [--craft <file>] [--damage N] "
							"[--no-vsync] [--cam-dist <m>] [--cam-orbit <deg>] [--show-hidden] "
							"[--screenshot <file.png> [--frames N]]",
				argv[i]);
			return 2;
		}
	}

	if (!std::filesystem::is_directory(options.assetsDir)) {
		ech::log::error("assets directory not found: {}", options.assetsDir.string());
		return 2;
	}

	return ech::runApp(options);
}
