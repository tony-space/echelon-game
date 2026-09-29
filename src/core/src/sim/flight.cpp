#include <echelon/sim/flight.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ech {

namespace {

constexpr float kMinAltitudeM = 2.0f;   // keep the fuselage off the ground plane
constexpr float kBankFollowRate = 4.0f; // 1/s, how quickly bank follows yaw input

float clampf(float v, float lo, float hi)
{
	return std::min(std::max(v, lo), hi);
}

// Exponential approach: moves `value` towards `target`, `rate` 1/s.
float approach(float value, float target, float rate, float dt)
{
	const float k = 1.0f - std::exp(-rate * dt);
	return value + (target - value) * k;
}

} // namespace

glm::quat FlightState::orientation() const
{
	// Positive heading turns right; a right-hand rotation about +Y turns left, hence the sign.
	const glm::quat yaw = glm::angleAxis(glm::radians(-headingDeg), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::quat pitch = glm::angleAxis(glm::radians(pitchDeg), glm::vec3(1.0f, 0.0f, 0.0f));
	const glm::quat roll = glm::angleAxis(glm::radians(bankDeg), glm::vec3(0.0f, 0.0f, 1.0f));
	return yaw * pitch * roll;
}

glm::vec3 FlightState::forward() const
{
	// Heading 0 looks down -Z; positive heading turns right (clockwise from above).
	const float h = glm::radians(headingDeg);
	const float p = glm::radians(pitchDeg);
	const float cp = std::cos(p);
	return glm::vec3(std::sin(h) * cp, std::sin(p), -std::cos(h) * cp);
}

glm::vec3 FlightState::up() const
{
	return orientation() * glm::vec3(0.0f, 1.0f, 0.0f);
}

void stepFlight(const FlightSpec& spec, FlightState& s, const FlightControls& c, float dt)
{
	if (dt <= 0.0f)
		return;

	const float aimYaw = clampf(c.aimYaw, -1.0f, 1.0f);
	const float aimPitch = clampf(c.aimPitch, -1.0f, 1.0f);
	const float throttle = clampf(c.throttle, 0.0f, 1.0f);

	// Turn authority depends on speed (CornerCFromSpeed in the original data).
	const float speedFrac = spec.maxSpeedMps > 0.0f ? s.speedMps / spec.maxSpeedMps : 0.0f;
	const float turnRate = spec.maxTurnRateDps * spec.turnFromSpeed.sample(speedFrac);

	s.headingDeg += aimYaw * turnRate * dt;
	s.headingDeg = std::fmod(s.headingDeg, 360.0f);
	if (s.headingDeg < 0.0f)
		s.headingDeg += 360.0f;

	s.pitchDeg = clampf(s.pitchDeg + aimPitch * turnRate * dt, -spec.maxPitchDeg, spec.maxPitchDeg);

	// Bank is cosmetic: lean into the turn.
	s.bankDeg = approach(s.bankDeg, -aimYaw * spec.maxBankDeg, kBankFollowRate, dt);

	// Thrust scales with altitude (ThrustCFromAlt). Speed chases the target
	// with a fixed acceleration so throttle changes feel weighty.
	const float thrustC = spec.thrustFromAltitude.sample(s.position.y);
	const float targetSpeed = clampf(spec.maxSpeedMps * throttle * thrustC, 0.0f, spec.maxSpeedMps);
	const float maxDelta = spec.accelerationMps2 * dt;
	s.speedMps += clampf(targetSpeed - s.speedMps, -maxDelta, maxDelta);

	s.position += s.forward() * (s.speedMps * dt);
	if (s.position.y < kMinAltitudeM) {
		s.position.y = kMinAltitudeM;
		if (s.pitchDeg < 0.0f)
			s.pitchDeg = 0.0f;
	}
}

} // namespace ech
