cmake_minimum_required(VERSION 3.22.1)

set(SRC
	main.cpp
	color_test.cpp
	curve_test.cpp
	craft_spec_test.cpp
	flight_test.cpp
	frame_test.cpp
	fixed_step_test.cpp
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SRC})

add_executable(master-test-suite ${SRC})

target_compile_definitions(master-test-suite PRIVATE ECH_TEST_ASSETS_DIR="${ECH_ASSETS_DIR}")
target_link_libraries(master-test-suite
	PRIVATE echelon_core Boost::unit_test_framework ECH_CXX_FLAGS)

set_target_properties(master-test-suite PROPERTIES
	UNITY_BUILD ${ECH_UNITY_BUILD}
	FOLDER tests)

add_test(NAME master-test-suite COMMAND master-test-suite --log_level=test_suite)
