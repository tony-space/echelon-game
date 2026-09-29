#include "app.hpp"

#include <echelon/core/log.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv)
{
	ech::AppOptions options;
	options.assetsDir = ECH_DEFAULT_ASSETS_DIR;

	const auto number = [&](int& i) { return static_cast<float>(std::atof(argv[++i])); };
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
			options.assetsDir = argv[++i];
		} else if (std::strcmp(argv[i], "--no-vsync") == 0) {
			options.vsync = false;
		} else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
			options.screenshotFile = argv[++i];
		} else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
			options.screenshotFrame = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--cam") == 0 && i + 5 < argc) {
			ech::CameraPose pose;
			pose.x = number(i);
			pose.y = number(i);
			pose.z = number(i);
			pose.yawDeg = number(i);
			pose.pitchDeg = number(i);
			options.cameraPose = pose;
		} else if (std::strcmp(argv[i], "--show-hidden") == 0) {
			options.showHiddenParts = true;
		} else if (std::strcmp(argv[i], "--damage") == 0 && i + 1 < argc) {
			options.damageState = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--terrain") == 0 && i + 1 < argc) {
			options.terrain = argv[++i];
		} else {
			ech::log::error("unknown argument '{}'. Usage: echelon [--assets <dir>] [--terrain <name>] [--damage N] "
							"[--no-vsync] [--show-hidden] [--cam <x> <y> <z> <yaw> <pitch>] "
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
