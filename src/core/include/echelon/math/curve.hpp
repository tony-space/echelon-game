#pragma once

#include <initializer_list>
#include <utility>
#include <vector>

namespace ech {

// Piecewise-linear curve y = f(x), the shape the original data files use for
// things like ThrustCFromAlt (0 2.2) (100 1) (1200 .85) (1500 .25).
// Outside the defined range the curve is clamped to its end values.
class Curve {
public:
	using Point = std::pair<float, float>;

	Curve() = default;
	Curve(std::initializer_list<Point> points);
	explicit Curve(std::vector<Point> points);

	float sample(float x) const;

	bool empty() const { return m_points.empty(); }
	const std::vector<Point>& points() const { return m_points; }

private:
	std::vector<Point> m_points; // sorted by x
};

} // namespace ech
