#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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

TEST_CASE("R16 keeps the size in the file name, PNG does not need it")
{
	CHECK(heightmap::FileName("alps", heightmap::Format::R16, 1025, 513) == "alps_1025x513.r16");
	CHECK(heightmap::FileName("alps_eroded", heightmap::Format::PNG16, 1025, 513) == "alps_eroded.png");
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
