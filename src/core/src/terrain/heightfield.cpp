#include <echelon/terrain/heightfield.hpp>

#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>

namespace ech {

namespace {

constexpr std::uint32_t kEterrPlainVersion = 1;
constexpr std::uint32_t kEterrZlibVersion = 2;
constexpr std::size_t kZlibHeaderBytes = 12; // 'ETER', version, uncompressed size
constexpr std::uint32_t kMaxUncompressedBytes = 512u * 1024u * 1024u;
constexpr std::size_t kHeaderBytes = 4 + 5 * 4 + 2 * 4;
constexpr std::size_t kCellBytes = 16;

// zlib wrapper (RFC 1950), the same bytes Python's zlib.compress writes.
std::vector<std::byte> inflateZlib(std::span<const std::byte> src, std::uint32_t rawSize)
{
	if (rawSize == 0 || rawSize > kMaxUncompressedBytes || src.empty() ||
		src.size() > static_cast<std::size_t>(std::numeric_limits<mz_ulong>::max()))
		throw HeightfieldError("eterr: bad compressed size");
	std::vector<std::byte> raw(rawSize);
	auto got = static_cast<mz_ulong>(rawSize);
	const int rc = mz_uncompress(reinterpret_cast<unsigned char*>(raw.data()), &got,
		reinterpret_cast<const unsigned char*>(src.data()), static_cast<mz_ulong>(src.size()));
	if (rc != MZ_OK || got != rawSize)
		throw HeightfieldError(std::format("eterr: inflate failed ({})", mz_error(rc) != nullptr ? mz_error(rc) : "unknown"));
	return raw;
}

class ByteReader {
public:
	explicit ByteReader(std::span<const std::byte> bytes)
		: m_bytes(bytes)
	{
	}

	template <class T>
	T pod(std::size_t offset) const
	{
		if (offset + sizeof(T) > m_bytes.size())
			throw HeightfieldError("eterr: truncated");
		T v;
		std::memcpy(&v, m_bytes.data() + offset, sizeof(T));
		return v;
	}

	void copy(std::size_t offset, void* dst, std::size_t size) const
	{
		if (offset + size > m_bytes.size())
			throw HeightfieldError("eterr: truncated");
		std::memcpy(dst, m_bytes.data() + offset, size);
	}

private:
	std::span<const std::byte> m_bytes;
};

} // namespace

Heightfield::Heightfield(int width, int height, float spacing, float heightScale, std::vector<std::int16_t> heights,
	std::vector<std::uint8_t> flags, int cellsX, int cellsY, std::vector<TerrainCell> cells)
	: m_width(width)
	, m_height(height)
	, m_cellsX(cellsX)
	, m_cellsY(cellsY)
	, m_spacing(spacing)
	, m_heightScale(heightScale)
	, m_heights(std::move(heights))
	, m_flags(std::move(flags))
	, m_cells(std::move(cells))
{
	const auto samples = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	if (width <= 0 || height <= 0 || m_heights.size() != samples || m_flags.size() != samples)
		throw HeightfieldError("heightfield: sample arrays do not match the size");
	if (cellsX <= 0 || cellsY <= 0 || m_cells.size() != static_cast<std::size_t>(cellsX) * cellsY)
		throw HeightfieldError("heightfield: cell array does not match the size");
}

// .eterr v1, or v2 (zlib-wrapped v1). See tools/terrain_export.py.
Heightfield Heightfield::parse(std::span<const std::byte> bytes)
{
	const ByteReader r(bytes);
	char magic[4];
	r.copy(0, magic, 4);
	if (std::memcmp(magic, "ETER", 4) != 0)
		throw HeightfieldError("eterr: bad magic");
	const auto version = r.pod<std::uint32_t>(4);
	if (version == kEterrZlibVersion) {
		const auto rawSize = r.pod<std::uint32_t>(8);
		return parse(inflateZlib(bytes.subspan(kZlibHeaderBytes), rawSize));
	}
	if (version != kEterrPlainVersion)
		throw HeightfieldError(std::format(
			"eterr: version {} (expected {} or {}); re-run tools/terrain_export.py", version, kEterrPlainVersion,
			kEterrZlibVersion));
	const auto width = static_cast<int>(r.pod<std::uint32_t>(8));
	const auto height = static_cast<int>(r.pod<std::uint32_t>(12));
	const auto cellsX = static_cast<int>(r.pod<std::uint32_t>(16));
	const auto cellsY = static_cast<int>(r.pod<std::uint32_t>(20));
	const float spacing = r.pod<float>(24);
	const float heightScale = r.pod<float>(28);
	if (width <= 0 || height <= 0 || cellsX <= 0 || cellsY <= 0 || width > 65536 || height > 65536)
		throw HeightfieldError("eterr: bad dimensions");

	const auto samples = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	std::size_t pos = kHeaderBytes;
	std::vector<std::int16_t> heights(samples);
	r.copy(pos, heights.data(), samples * sizeof(std::int16_t));
	pos += samples * sizeof(std::int16_t);
	std::vector<std::uint8_t> flags(samples);
	r.copy(pos, flags.data(), samples);
	pos += samples;

	std::vector<TerrainCell> cells(static_cast<std::size_t>(cellsX) * cellsY);
	for (std::size_t k = 0; k < cells.size(); ++k, pos += kCellBytes) {
		TerrainCell& c = cells[k];
		c.waterRaw = r.pod<std::int16_t>(pos + 2);
		c.layerA = r.pod<std::uint8_t>(pos + 8);
		c.layerB = r.pod<std::uint8_t>(pos + 9);
		c.mask = r.pod<std::uint32_t>(pos + 12);
	}
	return Heightfield(width, height, spacing, heightScale, std::move(heights), std::move(flags), cellsX, cellsY,
		std::move(cells));
}

Heightfield Heightfield::load(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary | std::ios::ate);
	if (!in)
		throw HeightfieldError(std::format("cannot open '{}'", file.string()));
	const auto size = static_cast<std::size_t>(in.tellg());
	std::vector<std::byte> bytes(size);
	in.seekg(0);
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
	if (!in)
		throw HeightfieldError(std::format("cannot read '{}'", file.string()));
	try {
		return parse(bytes);
	} catch (const HeightfieldError& e) {
		throw HeightfieldError(std::format("{}: {}", file.string(), e.what()));
	}
}

std::size_t Heightfield::index(int i, int j) const
{
	i = std::clamp(i, 0, m_width - 1);
	j = std::clamp(j, 0, m_height - 1);
	return static_cast<std::size_t>(j) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(i);
}

glm::vec3 Heightfield::samplePosition(int i, int j) const
{
	return glm::vec3(static_cast<float>(i) * m_spacing, sampleHeight(i, j), -static_cast<float>(j) * m_spacing);
}

const TerrainCell& Heightfield::cell(int ci, int cj) const
{
	ci = std::clamp(ci, 0, m_cellsX - 1);
	cj = std::clamp(cj, 0, m_cellsY - 1);
	return m_cells[static_cast<std::size_t>(cj) * static_cast<std::size_t>(m_cellsX) + static_cast<std::size_t>(ci)];
}

std::uint8_t Heightfield::layerAt(int i, int j) const
{
	i = std::clamp(i, 0, m_width - 1);
	j = std::clamp(j, 0, m_height - 1);
	const int ci = std::min(i / kCellSamples, m_cellsX - 1);
	const int cj = std::min(j / kCellSamples, m_cellsY - 1);
	return cell(ci, cj).layerAt(i - ci * kCellSamples, j - cj * kCellSamples);
}

float Heightfield::heightAt(float x, float z) const
{
	const float u = x / m_spacing;
	const float v = -z / m_spacing;
	const int i = static_cast<int>(std::floor(u));
	const int j = static_cast<int>(std::floor(v));
	const float fx = u - static_cast<float>(i);
	const float fz = v - static_cast<float>(j);

	const float h00 = sampleHeight(i, j);
	const float h10 = sampleHeight(i + 1, j);
	const float h01 = sampleHeight(i, j + 1);
	const float h11 = sampleHeight(i + 1, j + 1);

	if (flags(i, j) & kDiagonalFlag) {
		if (fx >= fz)
			return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
		return h00 + (h01 - h00) * fz + (h11 - h01) * fx;
	}
	if (fx + fz <= 1.0f)
		return h00 + (h10 - h00) * fx + (h01 - h00) * fz;
	return h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);
}

} // namespace ech
