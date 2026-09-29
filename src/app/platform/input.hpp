#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ech {

class Window;

// Sandbox controls for the free camera: hold the right mouse button to look
// around, WASD to move, E/Space up, Q/C down, Shift faster, Ctrl slower,
// mouse wheel changes the base speed.
class Input {
public:
	explicit Input(Window& window);

	// Called once per frame after pollEvents().
	void update();

	// Camera-relative move direction: x right, y up, z forward; components in [-1, 1].
	glm::vec3 move() const { return m_move; }
	float speedFactor() const { return m_speedFactor; } // Shift / Ctrl modifier
	// Mouse movement in pixels since the last frame while looking (+x right, +y down).
	glm::vec2 lookDelta() const { return m_lookDelta; }
	// Mouse wheel notches since the last frame.
	float scroll() const { return m_scroll; }

	bool quitRequested() const { return m_quit; }
	bool resetRequested() const { return m_reset; }
	// Edge-triggered: 0, or 1..4 when that number key was pressed this frame.
	// The sandbox maps these onto damage states im0..im3.
	int damageSelect() const { return m_damageSelect; }

private:
	Window& m_window;
	glm::vec3 m_move{0.0f};
	float m_speedFactor = 1.0f;
	glm::vec2 m_lookDelta{0.0f};
	float m_scroll = 0.0f;
	bool m_looking = false;
	double m_lastCursorX = 0.0;
	double m_lastCursorY = 0.0;
	bool m_quit = false;
	bool m_reset = false;
	bool m_resetHeld = false;
	int m_damageSelect = 0;
	bool m_damageHeld[4] = {};
};

} // namespace ech
