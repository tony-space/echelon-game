#include <echelon/data/craft_spec.hpp>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace ech {

namespace json = boost::json;

namespace {

// Tracks the JSON path so errors read like "human_bf1.json: flight.max_speed_mps: expected number".
class Reader {
public:
	Reader(std::string_view source, const json::value& root)
		: m_source(source), m_root(root)
	{
	}

	[[noreturn]] void fail(std::string_view path, std::string_view what) const
	{
		throw CraftSpecError(std::format("{}: {}: {}", m_source, path, what));
	}

	const json::object& object(const json::value& v, std::string_view path) const
	{
		if (const auto* o = v.if_object())
			return *o;
		fail(path, "expected object");
	}

	const json::array& array(const json::value& v, std::string_view path) const
	{
		if (const auto* a = v.if_array())
			return *a;
		fail(path, "expected array");
	}

	const json::value& required(const json::object& o, std::string_view path, std::string_view key) const
	{
		if (const auto* v = o.if_contains(key))
			return *v;
		fail(join(path, key), "missing required key");
	}

	float number(const json::value& v, std::string_view path) const
	{
		if (v.is_number())
			return static_cast<float>(v.to_number<double>());
		fail(path, "expected number");
	}

	float number(const json::object& o, std::string_view path, std::string_view key) const
	{
		return number(required(o, path, key), join(path, key));
	}

	float numberOr(const json::object& o, std::string_view path, std::string_view key, float fallback) const
	{
		const auto* v = o.if_contains(key);
		return v ? number(*v, join(path, key)) : fallback;
	}

	std::string string(const json::object& o, std::string_view path, std::string_view key) const
	{
		const json::value& v = required(o, path, key);
		if (const auto* s = v.if_string())
			return std::string(*s);
		fail(join(path, key), "expected string");
	}

	bool boolOr(const json::object& o, std::string_view path, std::string_view key, bool fallback) const
	{
		const auto* v = o.if_contains(key);
		if (!v)
			return fallback;
		if (const auto* b = v->if_bool())
			return *b;
		fail(join(path, key), "expected boolean");
	}

	Curve curve(const json::object& o, std::string_view path, std::string_view key) const
	{
		const std::string here = join(path, key);
		const json::array& arr = array(required(o, path, key), here);
		if (arr.empty())
			fail(here, "curve needs at least one [x, y] point");

		std::vector<Curve::Point> points;
		points.reserve(arr.size());
		for (std::size_t i = 0; i < arr.size(); ++i) {
			const std::string at = std::format("{}[{}]", here, i);
			const json::array& pt = array(arr[i], at);
			if (pt.size() != 2)
				fail(at, "expected [x, y]");
			points.emplace_back(number(pt[0], at + "[0]"), number(pt[1], at + "[1]"));
		}
		return Curve(std::move(points));
	}

	HullPart hullPart(const json::value& v, std::string_view path) const
	{
		const json::object& o = object(v, path);
		HullPart part;
		part.name = string(o, path, "name");
		part.armor = number(o, path, "armor");
		part.critical = boolOr(o, path, "critical", false);
		if (const auto* children = o.if_contains("parts")) {
			const std::string here = join(path, "parts");
			const json::array& arr = array(*children, here);
			for (std::size_t i = 0; i < arr.size(); ++i)
				part.parts.push_back(hullPart(arr[i], std::format("{}[{}]", here, i)));
		}
		return part;
	}

	static std::string join(std::string_view path, std::string_view key)
	{
		return path.empty() ? std::string(key) : std::format("{}.{}", path, key);
	}

private:
	std::string_view m_source;
	const json::value& m_root;
};

} // namespace

CraftSpec parseCraftSpec(std::string_view text, std::string_view sourceName)
{
	boost::system::error_code ec;
	json::value root = json::parse(text, ec);
	if (ec)
		throw CraftSpecError(std::format("{}: invalid JSON: {}", sourceName, ec.message()));

	Reader r(sourceName, root);
	const json::object& top = r.object(root, "");

	CraftSpec spec;
	spec.name = r.string(top, "", "name");
	spec.side = r.string(top, "", "side");
	spec.sensorsRangeM = r.numberOr(top, "", "sensors_range_m", 0.0f);

	const json::object& flight = r.object(r.required(top, "", "flight"), "flight");
	spec.flight.maxSpeedMps = r.number(flight, "flight", "max_speed_mps");
	spec.flight.accelerationMps2 = r.number(flight, "flight", "acceleration_mps2");
	spec.flight.maxTurnRateDps = r.number(flight, "flight", "max_turn_rate_dps");
	spec.flight.maxPitchDeg = r.numberOr(flight, "flight", "max_pitch_deg", spec.flight.maxPitchDeg);
	spec.flight.maxBankDeg = r.numberOr(flight, "flight", "max_bank_deg", spec.flight.maxBankDeg);
	spec.flight.thrustFromAltitude = r.curve(flight, "flight", "thrust_from_altitude");
	spec.flight.turnFromSpeed = r.curve(flight, "flight", "turn_from_speed");

	spec.hull = r.hullPart(r.required(top, "", "hull"), "hull");
	return spec;
}

CraftSpec loadCraftSpec(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
		throw CraftSpecError(std::format("{}: cannot open file", file.string()));
	std::ostringstream buf;
	buf << in.rdbuf();
	return parseCraftSpec(buf.str(), file.filename().string());
}

} // namespace ech
