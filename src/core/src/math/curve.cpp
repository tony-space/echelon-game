#include <echelon/math/curve.hpp>

#include <algorithm>

namespace ech {

Curve::Curve(std::initializer_list<Point> points)
	: Curve(std::vector<Point>(points))
{
}

Curve::Curve(std::vector<Point> points)
	: m_points(std::move(points))
{
	std::stable_sort(m_points.begin(), m_points.end(),
		[](const Point& a, const Point& b) { return a.first < b.first; });
}

float Curve::sample(float x) const
{
	if (m_points.empty())
		return 0.0f;
	if (x <= m_points.front().first)
		return m_points.front().second;
	if (x >= m_points.back().first)
		return m_points.back().second;

	auto hi = std::upper_bound(m_points.begin(), m_points.end(), x,
		[](float value, const Point& p) { return value < p.first; });
	auto lo = hi - 1;

	const float span = hi->first - lo->first;
	if (span <= 0.0f)
		return hi->second;
	const float t = (x - lo->first) / span;
	return lo->second + (hi->second - lo->second) * t;
}

} // namespace ech
