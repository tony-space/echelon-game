#pragma once

// Single include point for OpenGL. GLEW must come before GLFW so GLFW does
// not pull in the system gl.h first.
#include <GL/glew.h>
#include <GLFW/glfw3.h>
