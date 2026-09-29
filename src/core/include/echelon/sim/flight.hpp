#pragma once

#include <echelon/data/craft_spec.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace ech {

// Player intent, device-agnostic. The mouse (and later a gamepad stick)
// produces the same aim vector: where the nose should go relative to the
// current heading, both components in [-1, 1].
struct FlightControls {
	float aimYaw = 0.0f;   // +1 = turn right at full rate
	float aimPitch = 0.0f; // +1 = nose up at full rate
	float throttle = 0.0f; // [0, 1]
};

// Kinematic arcade state: the craft always flies where its nose points.
struct FlightState {
	glm::vec3 position{0.0f, 200.0f, 0.0f}; // meters, +Y is up
	float headingDeg = 0.0f;                // yaw around +Y, 0 = towards -Z
	float pitchDeg = 0.0f;                  // + = nose up
	float bankDeg = 0.0f;                   // cosmetic roll, follows yaw input
	float speedMps = 0.0f;                  // along the nose

	glm::quat orientation() const;
	glm::vec3 forward() const;
	glm::vec3 up() const;
	glm::vec3 velocity() const { return forward() * speedMps; }
};

// Advances the state by dt seconds. Ground is the plane y = 0.
void stepFlight(const FlightSpec& spec, FlightState& state, const FlightControls& controls, float dt);

} // namespace ech
