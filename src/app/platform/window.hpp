#pragma once

#include <stdexcept>
#include <string>

struct GLFWwindow;

namespace ech {

class WindowError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

struct WindowDesc {
	int width = 1280;
	int height = 720;
	std::string title = "Echelon";
	bool vsync = true;
};

// GLFW window with an OpenGL 4.1 core context and GLEW initialised.
// Throws WindowError if the context cannot be created.
class Window {
public:
	explicit Window(const WindowDesc& desc);
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	bool shouldClose() const;
	void requestClose();
	void pollEvents();
	void swapBuffers();

	int framebufferWidth() const;
	int framebufferHeight() const;
	float aspect() const;

	// Vertical mouse wheel notches accumulated since the previous call.
	double consumeScroll();

	GLFWwindow* handle() const { return m_window; }

private:
	static void onScroll(GLFWwindow* window, double dx, double dy);

	GLFWwindow* m_window = nullptr;
	double m_scroll = 0.0;
};

} // namespace ech
