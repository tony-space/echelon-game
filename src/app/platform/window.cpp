#include "platform/window.hpp"

#include "render/gl.hpp"

#include <echelon/core/log.hpp>

#include <format>

namespace ech {

namespace {

void onGlfwError(int code, const char* description)
{
	log::error("GLFW error {}: {}", code, description ? description : "?");
}

const char* glString(GLenum name)
{
	const auto* s = reinterpret_cast<const char*>(glGetString(name));
	return s ? s : "?";
}

} // namespace

Window::Window(const WindowDesc& desc)
{
	glfwSetErrorCallback(onGlfwError);
	if (!glfwInit())
		throw WindowError("glfwInit failed");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE); // required for core 4.1 on macOS
	glfwWindowHint(GLFW_SAMPLES, 4);
	// Lighting is computed in linear RGB; the back buffer encodes to sRGB on write.
	glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

	m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
	if (!m_window) {
		glfwTerminate();
		throw WindowError("glfwCreateWindow failed: OpenGL 4.1 core context unavailable");
	}

	glfwMakeContextCurrent(m_window);
	glfwSwapInterval(desc.vsync ? 1 : 0);

	const GLenum glewStatus = glewInit();
	if (glewStatus != GLEW_OK) {
		const auto* msg = reinterpret_cast<const char*>(glewGetErrorString(glewStatus));
		glfwDestroyWindow(m_window);
		glfwTerminate();
		throw WindowError(std::format("glewInit failed: {}", msg ? msg : "?"));
	}
	// Core profiles report GL_INVALID_ENUM from GLEW's extension probing; clear it.
	while (glGetError() != GL_NO_ERROR) {
	}

	if (!GLEW_VERSION_4_1) {
		glfwDestroyWindow(m_window);
		glfwTerminate();
		throw WindowError(std::format("OpenGL 4.1 required, driver reports {}", glString(GL_VERSION)));
	}

	log::info("OpenGL {} | {} | {}", glString(GL_VERSION), glString(GL_RENDERER), glString(GL_VENDOR));
	log::info("GLSL {}", glString(GL_SHADING_LANGUAGE_VERSION));

	// Shaders output linear RGB; the back buffer encodes to sRGB on write.
	// Do not gate this on GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING: WGL drivers
	// report GL_LINEAR for the window even when the pixel format is sRGB-capable
	// and the encode works (verified on this machine).
	glEnable(GL_FRAMEBUFFER_SRGB);
	GLint encoding = 0;
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
		&encoding);
	while (glGetError() != GL_NO_ERROR) {
	}
	log::info("back buffer: GL_FRAMEBUFFER_SRGB on (driver reports encoding {})",
		encoding == GL_SRGB ? "sRGB" : encoding == GL_LINEAR ? "linear" : "unknown");
}

Window::~Window()
{
	if (m_window)
		glfwDestroyWindow(m_window);
	glfwTerminate();
}

bool Window::shouldClose() const
{
	return glfwWindowShouldClose(m_window) != 0;
}

void Window::requestClose()
{
	glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::pollEvents()
{
	glfwPollEvents();
}

void Window::swapBuffers()
{
	glfwSwapBuffers(m_window);
}

int Window::framebufferWidth() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	return w;
}

int Window::framebufferHeight() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	return h;
}

float Window::aspect() const
{
	const int h = framebufferHeight();
	return h > 0 ? static_cast<float>(framebufferWidth()) / static_cast<float>(h) : 1.0f;
}

} // namespace ech
