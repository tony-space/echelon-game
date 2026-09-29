#include <echelon/math/curve.hpp>

#include <boost/test/unit_test.hpp>

using ech::Curve;

BOOST_AUTO_TEST_SUITE(curve)

BOOST_AUTO_TEST_CASE(empty_curve_returns_zero)
{
	Curve c;
	BOOST_TEST(c.empty());
	BOOST_TEST(c.sample(123.0f) == 0.0f);
}

BOOST_AUTO_TEST_CASE(interpolates_between_points)
{
	// ThrustCFromAlt from the original BF1 data.
	Curve c{{0.0f, 2.2f}, {100.0f, 1.0f}, {1200.0f, 0.85f}, {1500.0f, 0.25f}};
	BOOST_TEST(c.sample(0.0f) == 2.2f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(c.sample(50.0f) == 1.6f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(c.sample(100.0f) == 1.0f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(c.sample(1350.0f) == 0.55f, boost::test_tools::tolerance(1e-5f));
}

BOOST_AUTO_TEST_CASE(clamps_outside_range)
{
	Curve c{{0.0f, 0.7f}, {0.3f, 1.0f}, {0.9f, 1.0f}, {1.0f, 0.7f}};
	BOOST_TEST(c.sample(-5.0f) == 0.7f);
	BOOST_TEST(c.sample(7.0f) == 0.7f);
}

BOOST_AUTO_TEST_CASE(sorts_unordered_input)
{
	Curve c{{10.0f, 1.0f}, {0.0f, 0.0f}};
	BOOST_TEST(c.sample(5.0f) == 0.5f, boost::test_tools::tolerance(1e-5f));
}

BOOST_AUTO_TEST_SUITE_END()
