#pragma once

#include <echelon/sim/flight.hpp>

#include <glm/vec2.hpp>

namespace ech {

class Window;

// Mouse + keyboard mapped to FlightControls, Echelon style: the cursor is a
// reticle, the nose chases it. Distance from screen centre sets the turn rate.
class Input {
public:
	explicit Input(Window& window);

	// Called once per frame after pollEvents().
	void update(float dt);

	const FlightControls& controls() const { return m_controls; }
	// Replaces this frame's controls (scripted shots, later replays).
	void overrideControls(const FlightControls& c) { m_controls = c; }
	glm::vec2 reticleNdc() const { return m_reticleNdc; } // [-1,1]^2, +Y up
	bool quitRequested() const { return m_quit; }
	bool resetRequested() const { return m_reset; }
	// Edge-triggered: 0, or 1..4 when that number key was pressed this frame.
	// The sandbox maps these onto damage states im0..im3.
	int damageSelect() const { return m_damageSelect; }

private:
	Window& m_window;
	FlightControls m_controls;
	glm::vec2 m_reticleNdc{0.0f};
	bool m_quit = false;
	bool m_reset = false;
	bool m_resetHeld = false;
	int m_damageSelect = 0;
	bool m_damageHeld[4] = {};
};

} // namespace ech
