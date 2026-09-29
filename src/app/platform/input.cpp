#include "platform/input.hpp"

#include "platform/window.hpp"
#include "render/gl.hpp"

namespace ech {

Input::Input(Window& window)
	: m_window(window)
{
}

void Input::update()
{
	GLFWwindow* w = m_window.handle();
	const auto down = [w](int key) { return glfwGetKey(w, key) == GLFW_PRESS; };

	const bool look = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	double cx = 0.0, cy = 0.0;
	if (look != m_looking) {
		// A disabled cursor gives unbounded relative motion; restore it on release.
		glfwSetInputMode(w, GLFW_CURSOR, look ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		glfwGetCursorPos(w, &m_lastCursorX, &m_lastCursorY);
		m_looking = look;
	}
	m_lookDelta = glm::vec2(0.0f);
	if (m_looking) {
		glfwGetCursorPos(w, &cx, &cy);
		m_lookDelta = glm::vec2(static_cast<float>(cx - m_lastCursorX), static_cast<float>(cy - m_lastCursorY));
		m_lastCursorX = cx;
		m_lastCursorY = cy;
	}

	m_move = glm::vec3(0.0f);
	if (down(GLFW_KEY_W))
		m_move.z += 1.0f;
	if (down(GLFW_KEY_S))
		m_move.z -= 1.0f;
	if (down(GLFW_KEY_D))
		m_move.x += 1.0f;
	if (down(GLFW_KEY_A))
		m_move.x -= 1.0f;
	if (down(GLFW_KEY_E) || down(GLFW_KEY_SPACE))
		m_move.y += 1.0f;
	if (down(GLFW_KEY_Q) || down(GLFW_KEY_C))
		m_move.y -= 1.0f;

	m_speedFactor = 1.0f;
	if (down(GLFW_KEY_LEFT_SHIFT))
		m_speedFactor *= 8.0f;
	if (down(GLFW_KEY_LEFT_CONTROL))
		m_speedFactor *= 0.125f;

	m_scroll = static_cast<float>(m_window.consumeScroll());

	m_quit = down(GLFW_KEY_ESCAPE);

	const bool resetDown = down(GLFW_KEY_R);
	m_reset = resetDown && !m_resetHeld;
	m_resetHeld = resetDown;

	m_damageSelect = 0;
	const int numberKeys[4] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4};
	for (int i = 0; i < 4; ++i) {
		const bool pressed = down(numberKeys[i]);
		if (pressed && !m_damageHeld[i])
			m_damageSelect = i + 1;
		m_damageHeld[i] = pressed;
	}
}

} // namespace ech
