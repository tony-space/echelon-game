#include <echelon/math/color.hpp>

#include <boost/test/unit_test.hpp>
#include <glm/geometric.hpp>

#include <cmath>

using namespace ech;

BOOST_AUTO_TEST_SUITE(color)

BOOST_AUTO_TEST_CASE(mid_grey_is_much_darker_in_linear)
{
	// sRGB 0.5 (128/255) is ~21% linear luminance, the classic reference value.
	BOOST_CHECK_CLOSE(srgbToLinear(0.5f), 0.2140f, 0.5);
	BOOST_CHECK_CLOSE(linearToSrgb(0.2140f), 0.5f, 0.5);
}

BOOST_AUTO_TEST_CASE(endpoints_are_fixed)
{
	BOOST_CHECK_SMALL(srgbToLinear(0.0f), 1e-6f);
	BOOST_CHECK_CLOSE(srgbToLinear(1.0f), 1.0f, 1e-3);
	BOOST_CHECK_SMALL(linearToSrgb(0.0f), 1e-6f);
	BOOST_CHECK_CLOSE(linearToSrgb(1.0f), 1.0f, 1e-3);
}

BOOST_AUTO_TEST_CASE(round_trip_is_identity)
{
	for (float c = 0.0f; c <= 1.0f; c += 0.05f) {
		BOOST_CHECK_CLOSE_FRACTION(linearToSrgb(srgbToLinear(c)), c, 1e-4f);
	}
	const glm::vec3 sky(0.45f, 0.62f, 0.85f);
	const glm::vec3 back = linearToSrgb(srgbToLinear(sky));
	BOOST_CHECK_CLOSE_FRACTION(back.x, sky.x, 1e-4f);
	BOOST_CHECK_CLOSE_FRACTION(back.y, sky.y, 1e-4f);
	BOOST_CHECK_CLOSE_FRACTION(back.z, sky.z, 1e-4f);
}

BOOST_AUTO_TEST_CASE(fog_transmittance_is_beer_lambert)
{
	BOOST_CHECK_CLOSE(fogTransmittance(0.0001f, 0.0f), 1.0f, 1e-3);
	// Optical depth 1 leaves 1/e of the radiance.
	BOOST_CHECK_CLOSE(fogTransmittance(0.0001f, 10000.0f), 0.36788f, 0.01);
	// Two legs multiply, they do not add.
	const float a = fogTransmittance(0.0002f, 3000.0f);
	const float b = fogTransmittance(0.0002f, 5000.0f);
	BOOST_CHECK_CLOSE(a * b, fogTransmittance(0.0002f, 8000.0f), 1e-3);
}

BOOST_AUTO_TEST_CASE(height_fog_matches_numeric_integral)
{
	const float sigma0 = 0.0001f, H = 2500.0f;
	// Level ray at sea level is the homogeneous case.
	BOOST_CHECK_CLOSE(heightFogTransmittance(sigma0, H, glm::vec3(0.0f), glm::vec3(10000.0f, 0.0f, 0.0f)),
		fogTransmittance(sigma0, 10000.0f), 1e-2);

	// Slanted ray: midpoint-rule integral of sigma0 * exp(-y / H) along it.
	const glm::vec3 a(0.0f, 3000.0f, 0.0f), b(20000.0f, 100.0f, -5000.0f);
	const float len = glm::length(b - a);
	double od = 0.0;
	const int n = 10000;
	for (int i = 0; i < n; ++i) {
		const float t = (static_cast<float>(i) + 0.5f) / n;
		od += sigma0 * std::exp(-(a.y + (b.y - a.y) * t) / H) * len / n;
	}
	const float expected = static_cast<float>(std::exp(-od));
	BOOST_CHECK_CLOSE(heightFogTransmittance(sigma0, H, a, b), expected, 0.1);
	// Symmetric in the direction of travel.
	BOOST_CHECK_CLOSE(heightFogTransmittance(sigma0, H, b, a), expected, 0.1);
	// Thinner air up high: more light survives than at sea level over the same distance.
	BOOST_CHECK_GT(expected, fogTransmittance(sigma0, len));
}

BOOST_AUTO_TEST_SUITE_END()
