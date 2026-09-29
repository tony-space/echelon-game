#include <echelon/core/log.hpp>

#include <cstdio>

namespace ech::log {

namespace {

const char* prefix(Level level)
{
	switch (level) {
	case Level::Info: return "[info] ";
	case Level::Warn: return "[warn] ";
	case Level::Error: return "[error]";
	}
	return "[?]    ";
}

} // namespace

void write(Level level, std::string_view message)
{
	std::FILE* out = level == Level::Info ? stdout : stderr;
	std::fprintf(out, "%s %.*s\n", prefix(level), static_cast<int>(message.size()), message.data());
	std::fflush(out);
}

} // namespace ech::log
