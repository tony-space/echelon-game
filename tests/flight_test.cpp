#include <echelon/sim/flight.hpp>

#include <boost/test/unit_test.hpp>

#include <glm/geometric.hpp>

using namespace ech;

namespace {

FlightSpec testSpec()
{
	FlightSpec s;
	s.maxSpeedMps = 200.0f;
	s.accelerationMps2 = 50.0f;
	s.maxTurnRateDps = 60.0f;
	s.maxPitchDeg = 80.0f;
	s.maxBankDeg = 60.0f;
	s.thrustFromAltitude = Curve{{0.0f, 1.0f}, {2000.0f, 1.0f}};
	s.turnFromSpeed = Curve{{0.0f, 1.0f}, {1.0f, 1.0f}};
	return s;
}

void run(const FlightSpec& spec, FlightState& st, const FlightControls& c, float seconds, float dt = 1.0f / 120.0f)
{
	for (float t = 0.0f; t < seconds; t += dt)
		stepFlight(spec, st, c, dt);
}

} // namespace

BOOST_AUTO_TEST_SUITE(flight)

BOOST_AUTO_TEST_CASE(heading_zero_points_down_negative_z)
{
	FlightState s;
	const glm::vec3 f = s.forward();
	BOOST_TEST(f.x == 0.0f, boost::test_tools::tolerance(1e-6f));
	BOOST_TEST(f.y == 0.0f, boost::test_tools::tolerance(1e-6f));
	BOOST_TEST(f.z == -1.0f, boost::test_tools::tolerance(1e-6f));
}

BOOST_AUTO_TEST_CASE(forward_matches_orientation_quaternion)
{
	FlightState s;
	s.headingDeg = 37.0f;
	s.pitchDeg = -12.0f;
	s.bankDeg = 25.0f; // bank must not change where the nose points
	const glm::vec3 fromQuat = s.orientation() * glm::vec3(0.0f, 0.0f, -1.0f);
	const glm::vec3 f = s.forward();
	BOOST_TEST(glm::length(fromQuat - f) < 1e-4f);
}

BOOST_AUTO_TEST_CASE(full_throttle_reaches_max_speed)
{
	const FlightSpec spec = testSpec();
	FlightState s;
	s.speedMps = 0.0f;
	FlightControls c;
	c.throttle = 1.0f;
	run(spec, s, c, 10.0f);
	BOOST_TEST(s.speedMps == spec.maxSpeedMps, boost::test_tools::tolerance(0.5f));
}

BOOST_AUTO_TEST_CASE(acceleration_is_limited)
{
	const FlightSpec spec = testSpec();
	FlightState s;
	s.speedMps = 0.0f;
	FlightControls c;
	c.throttle = 1.0f;
	run(spec, s, c, 1.0f);
	BOOST_TEST(s.speedMps == spec.accelerationMps2, boost::test_tools::tolerance(1.0f));
}

BOOST_AUTO_TEST_CASE(yaw_input_turns_right_at_configured_rate)
{
	const FlightSpec spec = testSpec();
	FlightState s;
	FlightControls c;
	c.aimYaw = 1.0f;
	run(spec, s, c, 1.0f);
	BOOST_TEST(s.headingDeg == spec.maxTurnRateDps, boost::test_tools::tolerance(0.6f));
	BOOST_TEST(s.bankDeg < 0.0f); // leans into the turn
}

BOOST_AUTO_TEST_CASE(pitch_is_clamped)
{
	const FlightSpec spec = testSpec();
	FlightState s;
	FlightControls c;
	c.aimPitch = 1.0f;
	run(spec, s, c, 10.0f);
	BOOST_TEST(s.pitchDeg == spec.maxPitchDeg, boost::test_tools::tolerance(1e-3f));
}

BOOST_AUTO_TEST_CASE(does_not_sink_below_ground)
{
	const FlightSpec spec = testSpec();
	FlightState s;
	s.position.y = 30.0f;
	s.speedMps = 200.0f;
	FlightControls c;
	c.aimPitch = -1.0f;
	c.throttle = 1.0f;
	run(spec, s, c, 5.0f);
	BOOST_TEST(s.position.y >= 2.0f);
}

BOOST_AUTO_TEST_CASE(altitude_curve_scales_thrust)
{
	FlightSpec spec = testSpec();
	spec.thrustFromAltitude = Curve{{0.0f, 0.5f}, {1000.0f, 0.5f}};
	FlightState s;
	s.speedMps = 0.0f;
	FlightControls c;
	c.throttle = 1.0f;
	run(spec, s, c, 10.0f);
	BOOST_TEST(s.speedMps == spec.maxSpeedMps * 0.5f, boost::test_tools::tolerance(0.5f));
}

BOOST_AUTO_TEST_SUITE_END()
