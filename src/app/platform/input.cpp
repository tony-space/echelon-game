#include "platform/input.hpp"

#include "platform/window.hpp"
#include "render/gl.hpp"

#include <algorithm>
#include <cmath>

namespace ech {

namespace {

constexpr float kDeadZone = 0.04f;    // fraction of half-screen
constexpr float kFullDeflection = 0.7f; // reticle distance for max turn rate
constexpr float kThrottleRate = 0.8f;   // per second on W/S

float shapeAxis(float v)
{
	const float a = std::abs(v);
	if (a < kDeadZone)
		return 0.0f;
	const float t = std::min((a - kDeadZone) / (kFullDeflection - kDeadZone), 1.0f);
	return std::copysign(t * t, v); // quadratic: fine control near centre
}

} // namespace

Input::Input(Window& window)
	: m_window(window)
{
	m_controls.throttle = 0.6f;
	glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
}

void Input::update(float dt)
{
	GLFWwindow* w = m_window.handle();

	int width = 0, height = 0;
	glfwGetWindowSize(w, &width, &height);
	double cx = 0.0, cy = 0.0;
	glfwGetCursorPos(w, &cx, &cy);

	if (width > 0 && height > 0) {
		m_reticleNdc.x = std::clamp(static_cast<float>(cx) / static_cast<float>(width) * 2.0f - 1.0f, -1.0f, 1.0f);
		m_reticleNdc.y = std::clamp(1.0f - static_cast<float>(cy) / static_cast<float>(height) * 2.0f, -1.0f, 1.0f);
	}

	m_controls.aimYaw = shapeAxis(m_reticleNdc.x);
	m_controls.aimPitch = shapeAxis(m_reticleNdc.y);

	float throttle = m_controls.throttle;
	if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS)
		throttle += kThrottleRate * dt;
	if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS)
		throttle -= kThrottleRate * dt;
	if (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		throttle = 1.0f;
	if (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		throttle = 0.0f;
	m_controls.throttle = std::clamp(throttle, 0.0f, 1.0f);

	m_quit = glfwGetKey(w, GLFW_KEY_ESCAPE) == GLFW_PRESS;

	const bool resetDown = glfwGetKey(w, GLFW_KEY_R) == GLFW_PRESS;
	m_reset = resetDown && !m_resetHeld;
	m_resetHeld = resetDown;

	m_damageSelect = 0;
	const int numberKeys[4] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4};
	for (int i = 0; i < 4; ++i) {
		const bool down = glfwGetKey(w, numberKeys[i]) == GLFW_PRESS;
		if (down && !m_damageHeld[i])
			m_damageSelect = i + 1;
		m_damageHeld[i] = down;
	}
}

} // namespace ech
