cmake_minimum_required(VERSION 3.22.1)

project(Echelon LANGUAGES C CXX)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(ECH_UNITY_BUILD "Enable Unity build" OFF)
option(ECH_USE_PRECOMPILED_HEADERS "Use precompiled headers" OFF)
option(ECH_TESTS "Enable Tests" ON)

# Modular Boost checkout (superproject with CMake support).
# Override with -DECH_BOOST_SOURCE_DIR=<path> or the BOOST_ROOT env variable.
if (DEFINED ENV{BOOST_ROOT})
	set(ECH_BOOST_DEFAULT "$ENV{BOOST_ROOT}")
elseif (WIN32)
	set(ECH_BOOST_DEFAULT "C:/Utils/boost/boost-1.90.0")
else()
	set(ECH_BOOST_DEFAULT "")
endif()
set(ECH_BOOST_SOURCE_DIR "${ECH_BOOST_DEFAULT}" CACHE PATH "Path to the Boost source tree")

message(STATUS "Generating Unity build: ${ECH_UNITY_BUILD}")
message(STATUS "Generating precompiled headers: ${ECH_USE_PRECOMPILED_HEADERS}")
message(STATUS "Generating tests: ${ECH_TESTS}")
message(STATUS "Boost source dir: ${ECH_BOOST_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ECH_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")

# third parties
add_subdirectory(ext)

include(cxxflags.cmake)

add_subdirectory(src)

if (ECH_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

include(InstallRequiredSystemLibraries)
