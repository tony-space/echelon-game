#include <echelon/math/frame.hpp>
#include <echelon/sim/flight.hpp>

#include <boost/test/unit_test.hpp>

using namespace ech;

BOOST_AUTO_TEST_SUITE(frame)

// Nose of a level craft points down -Z, so "in front" means a negative Z.
BOOST_AUTO_TEST_CASE(view_delta_sits_in_front_of_the_nose)
{
	// gdata.dat Human_BF1: ViewDelta (0, -.05, 4.45)
	const glm::vec3 v = d3dToEngine(glm::vec3(0.0f, -0.05f, 4.45f));
	BOOST_TEST(v.x == 0.0f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(v.y == -0.05f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(v.z < 0.0f);
	BOOST_TEST(v.z == -4.45f, boost::test_tools::tolerance(1e-5f));
}

BOOST_AUTO_TEST_CASE(back_view_is_above_and_behind)
{
	// gdata.dat Human_BF1: BackViewDelta (0, 1, -6.)
	const glm::vec3 v = d3dToEngine(glm::vec3(0.0f, 1.0f, -6.0f));
	BOOST_TEST(v.y == 1.0f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(v.z > 0.0f);
	BOOST_TEST(v.z == 6.0f, boost::test_tools::tolerance(1e-5f));
}

BOOST_AUTO_TEST_CASE(glass_node_keeps_its_height)
{
	// objects.dat ha_bf1 glass node, D3D frame, parent-relative.
	const glm::vec3 v = d3dToEngine(glm::vec3(-0.04f, -0.35f, 3.97f));
	BOOST_TEST(v.x == -0.04f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(v.y == -0.35f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(v.z == -3.97f, boost::test_tools::tolerance(1e-5f));
}

// Mesh vertices store up as -Y. A point above the craft (source Y negative)
// must come out with positive engine Y, and the nose (+Z) must face -Z.
BOOST_AUTO_TEST_CASE(mesh_vertices_flip_up_and_nose)
{
	const glm::vec3 above = meshToEngine(glm::vec3(1.0f, -2.0f, 3.0f));
	BOOST_TEST(above.x == 1.0f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(above.y == 2.0f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(above.z == -3.0f, boost::test_tools::tolerance(1e-5f));

	FlightState level;
	const glm::vec3 nose = meshToEngine(glm::vec3(0.0f, 0.0f, 1.0f));
	BOOST_TEST(glm::dot(nose, level.forward()) > 0.99f);
}

BOOST_AUTO_TEST_SUITE_END()
