cmake_minimum_required(VERSION 3.22.1)

# ---- GLM (header-only math) ----
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
add_subdirectory(glm)

# ---- GLFW (window, GL context, input) ----
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)
set_property(TARGET glfw PROPERTY FOLDER ext)

# ---- stb (image loading) ----
add_subdirectory(stb)

# ---- GLEW (OpenGL function loader, prebuilt binaries) ----
# Layout: glew-2.1.0/lib/Release/<platform>/...  Add new platforms here.
set(ECH_GLEW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/glew-2.1.0")

add_library(glew STATIC IMPORTED GLOBAL)
target_include_directories(glew INTERFACE "${ECH_GLEW_ROOT}/include")
target_compile_definitions(glew INTERFACE GLEW_STATIC GLEW_NO_GLU)

if (WIN32)
	set_target_properties(glew PROPERTIES
		IMPORTED_LOCATION "${ECH_GLEW_ROOT}/lib/Release/x64/glew32s.lib")
	target_link_libraries(glew INTERFACE opengl32)
elseif (APPLE)
	set_target_properties(glew PROPERTIES
		IMPORTED_LOCATION "${ECH_GLEW_ROOT}/lib/Release/mac-m1/libGLEW.a")
	find_library(ECH_OPENGL_FRAMEWORK OpenGL REQUIRED)
	target_link_libraries(glew INTERFACE ${ECH_OPENGL_FRAMEWORK})
else()
	# No prebuilt Linux binary in the tree yet; drop libGLEW.a into
	# lib/Release/linux-x64 when available.
	set_target_properties(glew PROPERTIES
		IMPORTED_LOCATION "${ECH_GLEW_ROOT}/lib/Release/linux-x64/libGLEW.a")
	find_package(OpenGL REQUIRED)
	target_link_libraries(glew INTERFACE OpenGL::GL)
endif()

# ---- Boost (modular checkout with CMake support) ----
if (NOT ECH_BOOST_SOURCE_DIR OR NOT EXISTS "${ECH_BOOST_SOURCE_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"Boost source tree not found at '${ECH_BOOST_SOURCE_DIR}'. "
		"Set ECH_BOOST_SOURCE_DIR or BOOST_ROOT to a modular Boost checkout.")
endif()

set(BOOST_INCLUDE_LIBRARIES json test CACHE STRING "" FORCE)
set(BOOST_ENABLE_CMAKE ON CACHE BOOL "" FORCE)
set(BOOST_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)
set(BOOST_INSTALL_LAYOUT system CACHE STRING "" FORCE)
set(BUILD_TESTING OFF)
add_subdirectory("${ECH_BOOST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ext/boost" EXCLUDE_FROM_ALL)
