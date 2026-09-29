cmake_minimum_required(VERSION 3.22.1)

set(SRC
	include/echelon/core/log.hpp
	include/echelon/math/color.hpp
	include/echelon/math/curve.hpp
	include/echelon/math/frame.hpp
	include/echelon/data/craft_spec.hpp
	include/echelon/sim/flight.hpp
	include/echelon/sim/fixed_step.hpp
	include/echelon/terrain/heightfield.hpp

	src/core/log.cpp
	src/math/curve.cpp
	src/data/craft_spec.cpp
	src/sim/flight.cpp
	src/terrain/heightfield.cpp
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SRC})

add_library(echelon_core STATIC ${SRC})

target_include_directories(echelon_core PUBLIC include)
target_link_libraries(echelon_core
	PUBLIC glm::glm Boost::json
	PRIVATE ECH_CXX_FLAGS)

set_target_properties(echelon_core PROPERTIES
	UNITY_BUILD ${ECH_UNITY_BUILD}
	FOLDER echelon)
