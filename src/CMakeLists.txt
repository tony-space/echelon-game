cmake_minimum_required(VERSION 3.22.1)

# Rendering-free part of the game: data, math, simulation. Unit-tested.
add_subdirectory(core)

# Windowed application: GLFW + OpenGL 4.1 renderer on top of the core.
add_subdirectory(app)
