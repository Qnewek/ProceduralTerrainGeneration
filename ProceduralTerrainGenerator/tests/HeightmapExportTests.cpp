#include "doctest/doctest.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <lodepng.h>

#include "HeightmapExport.h"

static std::vector<unsigned char> ReadBytes(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

//Non-square map with values depending on the position, so swapped axes or rows are detected
static HeightField PositionMap(int width, int height)
{
	HeightField map(width, height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			map.At(x, y) = static_cast<float>(y * width + x) / static_cast<float>(width * height - 1);
		}
	}
	return map;
}

TEST_CASE("Quantize maps the whole range to 16 bits")
{
	HeightField map(4, 1);
	map[0] = -1.0f;
	map[1] = 0.0f;
	map[2] = 1.0f;
	map[3] = 0.5f;

	size_t clipped = 99;
	const std::vector<uint16_t> values = heightmap::Quantize(map, heightmap::noiseRange, clipped);
	CHECK(clipped == 0);
	CHECK(values[0] == 0);
	CHECK(values[1] == 32768);
	CHECK(values[2] == 65535);
	CHECK(values[3] == 49151);
}

TEST_CASE("Quantize clips values outside the range and NaN")
{
	HeightField map(4, 1);
	map[0] = -0.5f;
	map[1] = 1.5f;
	map[2] = std::numeric_limits<float>::quiet_NaN();
	map[3] = 0.5f;

	size_t clipped = 0;
	const std::vector<uint16_t> values = heightmap::Quantize(map, heightmap::terrainRange, clipped);
	CHECK(clipped == 3);
	CHECK(values[0] == 0);
	CHECK(values[1] == 65535);
	CHECK(values[2] == 0);
	CHECK(values[3] == 32768);
}

TEST_CASE("R16 keeps the size in the file name, PNG and EXR do not need it")
{
	CHECK(heightmap::FileName("alps", heightmap::Format::R16, 1025, 513) == "alps_1025x513.r16");
	CHECK(heightmap::FileName("alps_eroded", heightmap::Format::PNG16, 1025, 513) == "alps_eroded.png");
	CHECK(heightmap::FileName("alps", heightmap::Format::EXR32, 1025, 513) == "alps.exr");
}

TEST_CASE("MinMax skips non-finite values")
{
	HeightField map(4, 1);
	map[0] = 0.25f;
	map[1] = std::numeric_limits<float>::quiet_NaN();
	map[2] = -0.5f;
	map[3] = std::numeric_limits<float>::infinity();
	const heightmap::ValueRange range = heightmap::MinMax(map);
	CHECK(range.min == -0.5f);
	CHECK(range.max == 0.25f);

	HeightField invalid(1, 1);
	invalid[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(heightmap::MinMax(invalid).min == 0.0f);
	CHECK(heightmap::MinMax(invalid).max == 0.0f);
}

//Reads what an OpenEXR reader needs from a single-part scanline file: the attributes and the scanline blocks
namespace {
	struct ExrReader {
		const std::vector<unsigned char>& bytes;
		size_t pos = 0;

		uint32_t U32() { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(bytes.at(pos++)) << (8 * i); return v; }
		int32_t I32() { return static_cast<int32_t>(U32()); }
		uint64_t U64() { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(bytes.at(pos++)) << (8 * i); return v; }
		float F32() { const uint32_t bits = U32(); float f; std::memcpy(&f, &bits, 4); return f; }
		std::string String() { std::string s; while (bytes.at(pos) != 0) s += static_cast<char>(bytes[pos++]); pos++; return s; }
	};

	struct ExrAttribute {
		std::string type;
		size_t offset = 0;	//of the value
		int32_t size = 0;
	};
}

TEST_CASE("EXR is a valid uncompressed float file with the scaled values, top row first")
{
	const int width = 5, height = 3;
	HeightField map = PositionMap(width, height);
	map.At(1, 2) = std::numeric_limits<float>::quiet_NaN();
	const float scale = 256.0f;

	size_t invalid = 0;
	const std::vector<unsigned char> bytes = heightmap::EncodeExr(map, scale, heightmap::terrainRange, invalid);
	CHECK(invalid == 1);

	ExrReader r{ bytes };
	REQUIRE(r.U32() == 20000630u);
	CHECK(r.U32() == 2u);

	std::map<std::string, ExrAttribute> attributes;
	for (std::string name = r.String(); !name.empty(); name = r.String()) {
		ExrAttribute a;
		a.type = r.String();
		a.size = r.I32();
		a.offset = r.pos;
		attributes[name] = a;
		r.pos += static_cast<size_t>(a.size);
	}
	for (const char* required : { "channels", "compression", "dataWindow", "displayWindow", "lineOrder", "pixelAspectRatio", "screenWindowCenter", "screenWindowWidth" }) {
		CHECK_MESSAGE(attributes.count(required) == 1, required);
	}
	const size_t tableStart = r.pos;

	//One FLOAT channel "Y", sampling 1 x 1
	r.pos = attributes["channels"].offset;
	CHECK(attributes["channels"].type == "chlist");
	CHECK(r.String() == "Y");
	CHECK(r.I32() == 2);
	r.pos += 4;
	CHECK(r.I32() == 1);
	CHECK(r.I32() == 1);
	CHECK(r.String().empty());

	CHECK(bytes.at(attributes["compression"].offset) == 0);
	r.pos = attributes["dataWindow"].offset;
	CHECK(r.I32() == 0);
	CHECK(r.I32() == 0);
	CHECK(r.I32() == width - 1);
	CHECK(r.I32() == height - 1);

	r.pos = tableStart;
	std::vector<uint64_t> offsets;
	for (int y = 0; y < height; y++) {
		offsets.push_back(r.U64());
	}
	int wrong = 0;
	for (int y = 0; y < height; y++) {
		r.pos = static_cast<size_t>(offsets[y]);
		CHECK(r.I32() == y);
		CHECK(r.I32() == width * 4);
		for (int x = 0; x < width; x++) {
			const float expected = (x == 1 && y == 2) ? heightmap::terrainRange.min * scale : map.At(x, y) * scale;
			wrong += r.F32() != expected;
		}
	}
	CHECK(wrong == 0);
	CHECK(r.pos == bytes.size());
}

TEST_CASE("Exported files")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_export_tests";
	std::filesystem::remove_all(folder);
	const int width = 5, height = 3;
	const HeightField map = PositionMap(width, height);
	size_t clipped = 0;
	std::string error;
	const std::vector<uint16_t> expected = heightmap::Quantize(map, heightmap::terrainRange, clipped);

	SUBCASE("R16 holds little-endian values row by row, without a header")
	{
		const std::filesystem::path path = folder / "map.r16";
		REQUIRE_MESSAGE(heightmap::Export(path, map, heightmap::Format::R16, heightmap::terrainRange, clipped, error), error);

		const std::vector<unsigned char> bytes = ReadBytes(path);
		REQUIRE(bytes.size() == expected.size() * 2);
		int wrong = 0;
		for (size_t i = 0; i < expected.size(); i++) {
			wrong += (bytes[i * 2] | (bytes[i * 2 + 1] << 8)) != expected[i];
		}
		CHECK(wrong == 0);
	}
	SUBCASE("PNG decodes to the same size and values, top row first")
	{
		const std::filesystem::path path = folder / "map.png";
		REQUIRE_MESSAGE(heightmap::Export(path, map, heightmap::Format::PNG16, heightmap::terrainRange, clipped, error), error);

		std::vector<unsigned char> decoded;
		unsigned decodedWidth = 0, decodedHeight = 0;
		REQUIRE(lodepng::decode(decoded, decodedWidth, decodedHeight, path.string(), LCT_GREY, 16) == 0);
		CHECK(decodedWidth == width);
		CHECK(decodedHeight == height);
		REQUIRE(decoded.size() == expected.size() * 2);
		int wrong = 0;
		for (size_t i = 0; i < expected.size(); i++) {
			wrong += ((decoded[i * 2] << 8) | decoded[i * 2 + 1]) != expected[i];
		}
		CHECK(wrong == 0);
	}
	SUBCASE("PNG stays 16-bit greyscale when the map has few distinct values")
	{
		HeightField flat(8, 8);
		const std::filesystem::path path = folder / "flat.png";
		REQUIRE_MESSAGE(heightmap::Export(path, flat, heightmap::Format::PNG16, heightmap::terrainRange, clipped, error), error);

		std::vector<unsigned char> decoded;
		unsigned decodedWidth = 0, decodedHeight = 0;
		lodepng::State state;
		state.decoder.color_convert = 0;
		REQUIRE(lodepng::decode(decoded, decodedWidth, decodedHeight, state, ReadBytes(path)) == 0);
		CHECK(state.info_png.color.colortype == LCT_GREY);
		CHECK(state.info_png.color.bitdepth == 16);
	}
	SUBCASE("EXR is written as encoded, values outside the 16-bit range are kept")
	{
		HeightField outside = map;
		outside[0] = 2.0f;
		const std::filesystem::path path = folder / "map.exr";
		REQUIRE_MESSAGE(heightmap::Export(path, outside, heightmap::Format::EXR32, heightmap::terrainRange, clipped, error, 64.0f), error);
		CHECK(clipped == 0);
		size_t invalid = 0;
		CHECK(ReadBytes(path) == heightmap::EncodeExr(outside, 64.0f, heightmap::terrainRange, invalid));
	}
	SUBCASE("Clipped cells are reported")
	{
		HeightField outside(2, 1);
		outside[0] = 2.0f;
		REQUIRE_MESSAGE(heightmap::Export(folder / "outside.r16", outside, heightmap::Format::R16, heightmap::terrainRange, clipped, error), error);
		CHECK(clipped == 1);
	}
	SUBCASE("An empty map is not written")
	{
		const HeightField empty;
		CHECK_FALSE(heightmap::Export(folder / "empty.png", empty, heightmap::Format::PNG16, heightmap::terrainRange, clipped, error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(std::filesystem::exists(folder / "empty.png"));
	}
	SUBCASE("Exporting again replaces the file")
	{
		const std::filesystem::path path = folder / "map.r16";
		REQUIRE_MESSAGE(heightmap::Export(path, map, heightmap::Format::R16, heightmap::terrainRange, clipped, error), error);
		const HeightField smaller(2, 1);
		REQUIRE_MESSAGE(heightmap::Export(path, smaller, heightmap::Format::R16, heightmap::terrainRange, clipped, error), error);
		CHECK(std::filesystem::file_size(path) == 4);
		CHECK_FALSE(std::filesystem::exists(folder / "map.r16.tmp"));
	}

	std::filesystem::remove_all(folder);
}

TEST_CASE("Quantize handles a flat range without division by zero or NaN")
{
	HeightField flat(3, 2);
	flat.At(1, 1) = 5.0f;
	size_t clipped = 99;
	const std::vector<uint16_t> out = heightmap::Quantize(flat, { 5.0f, 5.0f }, clipped);
	REQUIRE(out.size() == 6);
	for (uint16_t v : out) {
		CHECK(v == 0);
	}
	CHECK(clipped == 5);
}
