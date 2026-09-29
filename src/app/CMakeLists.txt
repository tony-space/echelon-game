cmake_minimum_required(VERSION 3.22.1)

set(SRC
	main.cpp
	app.hpp
	app.cpp
	platform/window.hpp
	platform/window.cpp
	platform/input.hpp
	platform/input.cpp
	render/gl.hpp
	render/shader.hpp
	render/shader.cpp
	render/mesh.hpp
	render/mesh.cpp
	render/model.hpp
	render/model.cpp
	render/texture.hpp
	render/texture.cpp
	render/camera.hpp
	render/frame.hpp
	render/terrain_renderer.hpp
	render/terrain_renderer.cpp
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SRC})

add_executable(echelon ${SRC})

target_include_directories(echelon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(echelon PRIVATE ECH_DEFAULT_ASSETS_DIR="${ECH_ASSETS_DIR}")
target_link_libraries(echelon
	PRIVATE echelon_core glfw glew STB glm::glm ECH_CXX_FLAGS)

set_target_properties(echelon PROPERTIES
	UNITY_BUILD ${ECH_UNITY_BUILD}
	FOLDER echelon
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT echelon)
