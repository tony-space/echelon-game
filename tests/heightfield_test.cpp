#include <echelon/terrain/heightfield.hpp>

#include <miniz.h>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <vector>

using namespace ech;

namespace {

// 2 x 2 samples = one quad; heights 0, 10, 20, 30 (raw 0, 50, 100, 150 at 0.2).
Heightfield quad(bool diagonal00to11)
{
	std::vector<std::int16_t> h = {0, 50, 100, 150}; // (0,0) (1,0) (0,1) (1,1)
	std::vector<std::uint8_t> f(4, 0);
	if (diagonal00to11)
		f[0] = Heightfield::kDiagonalFlag;
	return Heightfield(2, 2, 64.0f, 0.2f, std::move(h), std::move(f), 1, 1, std::vector<TerrainCell>(1));
}

template <class T>
void put(std::vector<std::byte>& out, const T& v)
{
	const auto* p = reinterpret_cast<const std::byte*>(&v);
	out.insert(out.end(), p, p + sizeof(T));
}

} // namespace

BOOST_AUTO_TEST_SUITE(heightfield)

BOOST_AUTO_TEST_CASE(samples_map_to_engine_axes)
{
	const Heightfield hf = quad(true);
	// j runs along the original +Z, which is -Z in the engine.
	const glm::vec3 p = hf.samplePosition(1, 1);
	BOOST_CHECK_CLOSE(p.x, 64.0f, 1e-4);
	BOOST_CHECK_CLOSE(p.y, 30.0f, 1e-4);
	BOOST_CHECK_CLOSE(p.z, -64.0f, 1e-4);
	BOOST_CHECK_CLOSE(hf.heightAt(64.0f, -64.0f), 30.0f, 1e-3);
	BOOST_CHECK_SMALL(hf.heightAt(0.0f, 0.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(planar_quad_is_the_same_on_both_splits)
{
	// h = 10 * i + 20 * j is a plane: every triangle of either split lies on it.
	for (const bool diagonal : {true, false}) {
		const Heightfield hf = quad(diagonal);
		BOOST_CHECK_CLOSE(hf.heightAt(48.0f, -16.0f), 10.0f * 0.75f + 20.0f * 0.25f, 1e-3);
		BOOST_CHECK_CLOSE(hf.heightAt(16.0f, -48.0f), 10.0f * 0.25f + 20.0f * 0.75f, 1e-3);
		BOOST_CHECK_CLOSE(hf.heightAt(32.0f, -32.0f), 15.0f, 1e-3);
	}
}

BOOST_AUTO_TEST_CASE(non_planar_quad_depends_on_the_split)
{
	// Only (1,1) is raised: the 00-11 split carries it to the centre, the
	// 10-01 split keeps the centre on the flat triangle.
	std::vector<std::int16_t> h = {0, 0, 0, 100};
	auto make = [&](std::uint8_t flag) {
		return Heightfield(2, 2, 64.0f, 0.2f, h, {flag, 0, 0, 0}, 1, 1, std::vector<TerrainCell>(1));
	};
	BOOST_CHECK_CLOSE(make(Heightfield::kDiagonalFlag).heightAt(32.0f, -32.0f), 10.0f, 1e-3);
	BOOST_CHECK_SMALL(make(0).heightAt(31.0f, -31.0f), 1e-4f);
}

BOOST_AUTO_TEST_CASE(cell_mask_selects_second_layer_per_corner)
{
	TerrainCell c;
	c.layerA = 3;
	c.layerB = 7;
	c.mask = (1u << (0 * 5 + 1)) | (1u << (2 * 5 + 3)); // corners (1,0) and (3,2)
	BOOST_CHECK_EQUAL(c.layerAt(0, 0), 3);
	BOOST_CHECK_EQUAL(c.layerAt(1, 0), 7);
	BOOST_CHECK_EQUAL(c.layerAt(3, 2), 7);
	BOOST_CHECK_EQUAL(c.layerAt(2, 3), 3);
	c.layerB = TerrainCell::kNoLayer;
	BOOST_CHECK_EQUAL(c.layerAt(1, 0), 3);
}

BOOST_AUTO_TEST_CASE(parses_eterr)
{
	std::vector<std::byte> bytes;
	const char magic[4] = {'E', 'T', 'E', 'R'};
	for (char ch : magic)
		bytes.push_back(static_cast<std::byte>(ch));
	put<std::uint32_t>(bytes, 1);
	put<std::uint32_t>(bytes, 4); // width
	put<std::uint32_t>(bytes, 4); // height
	put<std::uint32_t>(bytes, 1); // cellsX
	put<std::uint32_t>(bytes, 1); // cellsY
	put<float>(bytes, 64.0f);
	put<float>(bytes, 0.2f);
	for (std::int16_t k = 0; k < 16; ++k)
		put<std::int16_t>(bytes, static_cast<std::int16_t>(k * 5));
	for (int k = 0; k < 16; ++k)
		bytes.push_back(std::byte{0});
	put<std::uint16_t>(bytes, 3);
	put<std::int16_t>(bytes, 300); // water level raw
	put<std::int16_t>(bytes, 0);
	put<std::uint16_t>(bytes, 0);
	bytes.push_back(std::byte{5});
	bytes.push_back(std::byte{9});
	put<std::uint16_t>(bytes, 101);
	put<std::uint32_t>(bytes, 1u << (1 * 5 + 2)); // corner (2, 1)

	const Heightfield hf = Heightfield::parse(bytes);
	BOOST_CHECK_EQUAL(hf.width(), 4);
	BOOST_CHECK_EQUAL(hf.rawHeight(3, 2), 11 * 5);
	BOOST_CHECK_CLOSE(hf.sampleHeight(3, 2), 11.0f, 1e-3);
	BOOST_CHECK_EQUAL(hf.cell(0, 0).waterRaw, 300);
	BOOST_CHECK(hf.cell(0, 0).hasWater());
	BOOST_CHECK_EQUAL(hf.layerAt(0, 0), 5);
	BOOST_CHECK_EQUAL(hf.layerAt(2, 1), 9);

	bytes[4] = std::byte{3};
	BOOST_CHECK_THROW(Heightfield::parse(bytes), HeightfieldError);
	bytes[4] = std::byte{1};
	bytes.resize(bytes.size() - 1);
	BOOST_CHECK_THROW(Heightfield::parse(bytes), HeightfieldError);
}

BOOST_AUTO_TEST_CASE(parses_zlib_wrapped_eterr)
{
	std::vector<std::byte> plain;
	const char magic[4] = {'E', 'T', 'E', 'R'};
	for (char ch : magic)
		plain.push_back(static_cast<std::byte>(ch));
	put<std::uint32_t>(plain, 1);
	put<std::uint32_t>(plain, 2);
	put<std::uint32_t>(plain, 2);
	put<std::uint32_t>(plain, 1);
	put<std::uint32_t>(plain, 1);
	put<float>(plain, 64.0f);
	put<float>(plain, 0.2f);
	for (std::int16_t k = 0; k < 4; ++k)
		put<std::int16_t>(plain, static_cast<std::int16_t>(k * 10));
	for (int k = 0; k < 4; ++k)
		plain.push_back(std::byte{0x80});
	for (int k = 0; k < 16; ++k)
		plain.push_back(std::byte{0});

	std::vector<unsigned char> compressed(mz_compressBound(static_cast<mz_ulong>(plain.size())));
	auto compressedSize = static_cast<mz_ulong>(compressed.size());
	BOOST_REQUIRE_EQUAL(mz_compress2(compressed.data(), &compressedSize,
							reinterpret_cast<const unsigned char*>(plain.data()), static_cast<mz_ulong>(plain.size()),
							9),
		MZ_OK);

	std::vector<std::byte> wrapped;
	for (char ch : magic)
		wrapped.push_back(static_cast<std::byte>(ch));
	put<std::uint32_t>(wrapped, 2);
	put<std::uint32_t>(wrapped, static_cast<std::uint32_t>(plain.size()));
	for (mz_ulong i = 0; i < compressedSize; ++i)
		wrapped.push_back(static_cast<std::byte>(compressed[i]));

	const Heightfield hf = Heightfield::parse(wrapped);
	BOOST_CHECK_EQUAL(hf.width(), 2);
	BOOST_CHECK_EQUAL(hf.rawHeight(1, 1), 30);
	BOOST_CHECK_EQUAL(hf.flags(0, 0), Heightfield::kDiagonalFlag);

	wrapped.back() = std::byte{0};
	BOOST_CHECK_THROW(Heightfield::parse(wrapped), HeightfieldError);
}

BOOST_AUTO_TEST_SUITE_END()
