#include <echelon/data/craft_spec.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <string>

using namespace ech;

BOOST_AUTO_TEST_SUITE(craft_spec)

BOOST_AUTO_TEST_CASE(loads_human_bf1_from_assets)
{
	const std::filesystem::path file = std::filesystem::path(ECH_TEST_ASSETS_DIR) / "data/crafts/human_bf1.json";
	const CraftSpec spec = loadCraftSpec(file);

	BOOST_TEST(spec.name == "Human_BF1");
	BOOST_TEST(spec.side == "HUMANS");
	BOOST_TEST(spec.sensorsRangeM == 6000.0f);
	BOOST_TEST(spec.flight.maxSpeedMps > 0.0f);
	BOOST_TEST(spec.flight.thrustFromAltitude.points().size() == 4u);
	BOOST_TEST(spec.flight.thrustFromAltitude.sample(0.0f) == 2.2f, boost::test_tools::tolerance(1e-5f));
	BOOST_TEST(spec.flight.turnFromSpeed.sample(0.5f) == 1.0f, boost::test_tools::tolerance(1e-5f));

	BOOST_TEST(spec.hull.name == "HULL");
	BOOST_TEST(spec.hull.critical);
	BOOST_REQUIRE_EQUAL(spec.hull.parts.size(), 2u);
	BOOST_TEST(spec.hull.parts[0].name == "LE");
	BOOST_REQUIRE_EQUAL(spec.hull.parts[0].parts.size(), 1u);
	BOOST_TEST(spec.hull.parts[0].parts[0].name == "LW");
	BOOST_TEST(!spec.hull.parts[0].parts[0].critical);
}

BOOST_AUTO_TEST_CASE(missing_key_reports_path)
{
	const std::string json = R"({
		"name": "X", "side": "HUMANS",
		"flight": { "max_speed_mps": 100 },
		"hull": { "name": "HULL", "armor": 1 }
	})";
	BOOST_CHECK_EXCEPTION(parseCraftSpec(json, "x.json"), CraftSpecError, [](const CraftSpecError& e) {
		const std::string what = e.what();
		return what.find("x.json") != std::string::npos && what.find("flight.acceleration_mps2") != std::string::npos;
	});
}

BOOST_AUTO_TEST_CASE(wrong_type_reports_path)
{
	const std::string json = R"({
		"name": "X", "side": "HUMANS",
		"flight": {
			"max_speed_mps": "fast", "acceleration_mps2": 1, "max_turn_rate_dps": 1,
			"thrust_from_altitude": [[0, 1]], "turn_from_speed": [[0, 1]]
		},
		"hull": { "name": "HULL", "armor": 1 }
	})";
	BOOST_CHECK_EXCEPTION(parseCraftSpec(json), CraftSpecError, [](const CraftSpecError& e) {
		return std::string(e.what()).find("flight.max_speed_mps") != std::string::npos;
	});
}

BOOST_AUTO_TEST_CASE(malformed_curve_point_reports_index)
{
	const std::string json = R"({
		"name": "X", "side": "HUMANS",
		"flight": {
			"max_speed_mps": 1, "acceleration_mps2": 1, "max_turn_rate_dps": 1,
			"thrust_from_altitude": [[0, 1], [5]], "turn_from_speed": [[0, 1]]
		},
		"hull": { "name": "HULL", "armor": 1 }
	})";
	BOOST_CHECK_EXCEPTION(parseCraftSpec(json), CraftSpecError, [](const CraftSpecError& e) {
		return std::string(e.what()).find("thrust_from_altitude[1]") != std::string::npos;
	});
}

BOOST_AUTO_TEST_CASE(invalid_json_is_an_error)
{
	BOOST_CHECK_THROW(parseCraftSpec("{ not json"), CraftSpecError);
}

BOOST_AUTO_TEST_SUITE_END()
