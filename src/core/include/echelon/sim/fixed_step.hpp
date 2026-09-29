#pragma once

#include <algorithm>

namespace ech {

// Accumulates wall-clock time and hands out fixed simulation steps, so the
// flight model integrates at a constant rate regardless of frame rate.
class FixedStep {
public:
	explicit FixedStep(float stepSeconds = 1.0f / 120.0f, int maxStepsPerFrame = 8)
		: m_step(stepSeconds), m_maxSteps(maxStepsPerFrame)
	{
	}

	float step() const { return m_step; }

	// Feed frame time; then call consume() until it returns false.
	void advance(float frameSeconds)
	{
		m_accumulator += std::max(frameSeconds, 0.0f);
		m_stepsThisFrame = 0;
	}

	bool consume()
	{
		if (m_accumulator < m_step)
			return false;
		if (m_stepsThisFrame >= m_maxSteps) {
			m_accumulator = 0.0f; // stalled; drop backlog instead of spiralling
			return false;
		}
		m_accumulator -= m_step;
		++m_stepsThisFrame;
		return true;
	}

	// Fraction of a step left over, for render interpolation later.
	float alpha() const { return m_accumulator / m_step; }

private:
	float m_step;
	int m_maxSteps;
	float m_accumulator = 0.0f;
	int m_stepsThisFrame = 0;
};

} // namespace ech
