#pragma once

#include <echelon/math/curve.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ech {

// Arcade flight parameters. Curves are normalized the same way the original
// Storm data was: thrust_from_altitude takes meters, turn_from_speed takes
// speed as a fraction of max_speed_mps.
struct FlightSpec {
	float maxSpeedMps = 200.0f;
	float accelerationMps2 = 40.0f;
	float maxTurnRateDps = 60.0f;
	float maxPitchDeg = 80.0f;
	float maxBankDeg = 60.0f;
	Curve thrustFromAltitude{{0.0f, 1.0f}};
	Curve turnFromSpeed{{0.0f, 1.0f}};
};

// One destructible piece of the airframe (HULL, LE, RW...). Children are
// destroyed together with their parent.
struct HullPart {
	std::string name;
	float armor = 0.0f;
	bool critical = false;
	std::vector<HullPart> parts;
};

struct CraftSpec {
	std::string name;
	std::string side;
	float sensorsRangeM = 0.0f;
	FlightSpec flight;
	HullPart hull;
};

class CraftSpecError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Throws CraftSpecError with the file name and JSON path of the offending key.
CraftSpec loadCraftSpec(const std::filesystem::path& file);
CraftSpec parseCraftSpec(std::string_view json, std::string_view sourceName = "<memory>");

} // namespace ech
