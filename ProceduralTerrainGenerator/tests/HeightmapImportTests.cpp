#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lodepng.h>

#include "HeightmapImport.h"

static void WriteBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

//Non-square map covering the range, so swapped axes or rows are detected
static HeightField Ramp(int width, int height, heightmap::ValueRange range)
{
	HeightField map(width, height);
	const int last = width * height - 1;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			map.At(x, y) = range.min + (range.max - range.min) * static_cast<float>(y * width + x) / static_cast<float>(last);
		}
	}
	return map;
}

TEST_CASE("Importing height maps")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_import_tests";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);

	HeightField loaded;
	heightmap::ImportInfo info;
	std::string error;
	size_t clipped = 0;

	SUBCASE("An exported map comes back with the same samples")
	{
		for (heightmap::Format format : { heightmap::Format::PNG16, heightmap::Format::R16 }) {
			for (heightmap::ValueRange range : { heightmap::terrainRange, heightmap::noiseRange }) {
				const HeightField original = Ramp(7, 3, range);
				const std::filesystem::path path = folder / heightmap::FileName("map", format, 7, 3);
				REQUIRE_MESSAGE(heightmap::Export(path, original, format, range, clipped, error), error);

				REQUIRE_MESSAGE(heightmap::Import(path, range, loaded, info, error), error);
				CHECK(loaded.GetWidth() == 7);
				CHECK(loaded.GetHeight() == 3);
				CHECK(info.bitsPerSample == 16);
				CHECK_FALSE(info.convertedFromColor);
				size_t ignored = 0;
				CHECK(heightmap::Quantize(loaded, range, ignored) == heightmap::Quantize(original, range, ignored));
				CHECK(ignored == 0);
			}
		}
	}
	SUBCASE("The first and the last sample reach the ends of the range exactly")
	{
		const std::filesystem::path path = folder / "ends_2x2.r16";
		WriteBytes(path, { 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 });
		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::noiseRange, loaded, info, error), error);
		CHECK(loaded[0] == -1.0f);
		CHECK(loaded[1] == 1.0f);
	}
	SUBCASE("R16 is little-endian")
	{
		const std::filesystem::path path = folder / "order_2x2.r16";
		WriteBytes(path, { 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 });
		REQUIRE_MESSAGE(heightmap::Import(path, { 0.0f, 65535.0f }, loaded, info, error), error);
		CHECK(loaded[0] == 1.0f);
		CHECK(loaded[1] == 256.0f);
	}
	SUBCASE("8-bit greyscale PNG covers the whole range")
	{
		const std::filesystem::path path = folder / "grey8.png";
		std::vector<unsigned char> png;
		//Not multiples of 17, which the encoder would store in 4 bits
		REQUIRE(lodepng::encode(png, std::vector<unsigned char>{ 0, 255, 50, 200 }, 2, 2, LCT_GREY, 8) == 0);
		WriteBytes(path, png);

		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::terrainRange, loaded, info, error), error);
		CHECK(info.bitsPerSample == 8);
		CHECK_FALSE(info.convertedFromColor);
		CHECK(loaded.At(0, 0) == 0.0f);
		CHECK(loaded.At(1, 0) == 1.0f);
		CHECK(loaded.At(0, 1) == doctest::Approx(50.0f / 255.0f));
		CHECK(loaded.At(1, 1) == doctest::Approx(200.0f / 255.0f));
	}
	SUBCASE("Low bit depth greyscale PNG is read")
	{
		const std::filesystem::path path = folder / "grey1.png";
		lodepng::State state;
		state.info_raw.colortype = LCT_GREY;
		state.info_raw.bitdepth = 1;
		state.info_png.color.colortype = LCT_GREY;
		state.info_png.color.bitdepth = 1;
		state.encoder.auto_convert = 0;
		//lodepng takes bits without row padding: rows 01 and 10
		const std::vector<unsigned char> bits = { 0b01100000 };
		std::vector<unsigned char> png;
		REQUIRE(lodepng::encode(png, bits.data(), 2, 2, state) == 0);
		WriteBytes(path, png);

		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::terrainRange, loaded, info, error), error);
		CHECK(info.bitsPerSample == 1);
		CHECK(loaded.At(0, 0) == 0.0f);
		CHECK(loaded.At(1, 0) == 1.0f);
		CHECK(loaded.At(0, 1) == 1.0f);
		CHECK(loaded.At(1, 1) == 0.0f);
	}
	SUBCASE("Color PNG becomes the mean of its channels and is reported")
	{
		const std::filesystem::path path = folder / "color.png";
		std::vector<unsigned char> png;
		REQUIRE(lodepng::encode(png, std::vector<unsigned char>{ 255, 0, 0, 90, 90, 90, 0, 0, 0, 0, 0, 0 }, 2, 2, LCT_RGB, 8) == 0);
		WriteBytes(path, png);

		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::terrainRange, loaded, info, error), error);
		CHECK(info.convertedFromColor);
		CHECK(loaded[0] == doctest::Approx(85.0f / 255.0f));
		CHECK(loaded[1] == doctest::Approx(90.0f / 255.0f));
	}
	SUBCASE("16-bit color PNG keeps its precision")
	{
		const std::filesystem::path path = folder / "color16.png";
		//Big-endian samples; the mean of each pixel is 0x1234 and 0xFFFF
		std::vector<unsigned char> pixels = { 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		pixels.resize(4 * 6);
		std::vector<unsigned char> png;
		REQUIRE(lodepng::encode(png, pixels, 2, 2, LCT_RGB, 16) == 0);
		WriteBytes(path, png);

		REQUIRE_MESSAGE(heightmap::Import(path, { 0.0f, 65535.0f }, loaded, info, error), error);
		CHECK(info.bitsPerSample == 16);
		CHECK(loaded[0] == 4660.0f);
		CHECK(loaded[1] == 65535.0f);
	}
	SUBCASE("A square R16 file does not need the size in its name")
	{
		const std::filesystem::path path = folder / "square.raw";
		WriteBytes(path, std::vector<unsigned char>(16 * 2));
		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::terrainRange, loaded, info, error), error);
		CHECK(loaded.GetWidth() == 4);
		CHECK(loaded.GetHeight() == 4);
	}
	SUBCASE("A name that only looks like it has a size is treated as no size")
	{
		const std::filesystem::path path = folder / "max_box.r16";
		WriteBytes(path, std::vector<unsigned char>(4 * 2));
		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::terrainRange, loaded, info, error), error);
		CHECK(loaded.GetWidth() == 2);
		CHECK(loaded.GetHeight() == 2);
	}
	SUBCASE("The extension is not case sensitive")
	{
		const std::filesystem::path path = folder / "UPPER.R16";
		WriteBytes(path, std::vector<unsigned char>(4 * 2));
		CHECK_MESSAGE(heightmap::Import(path, heightmap::terrainRange, loaded, info, error), error);
	}
	SUBCASE("Invalid files are rejected and the map stays unchanged")
	{
		std::vector<unsigned char> bigPng;
		REQUIRE(lodepng::encode(bigPng, std::vector<unsigned char>(8193 * 2), 8193, 2, LCT_GREY, 8) == 0);
		std::vector<unsigned char> thinPng;
		REQUIRE(lodepng::encode(thinPng, std::vector<unsigned char>(10), 10, 1, LCT_GREY, 8) == 0);
		std::vector<unsigned char> goodPng;
		REQUIRE(lodepng::encode(goodPng, std::vector<unsigned char>(16), 4, 4, LCT_GREY, 8) == 0);

		struct Case {
			const char* name;
			const char* fileName;
			std::vector<unsigned char> bytes;
		};
		const Case cases[] = {
			{ "unsupported type", "map.bmp", { 0, 0 } },
			{ "no extension", "map", { 0, 0 } },
			{ "not a PNG", "broken.png", { 1, 2, 3, 4 } },
			{ "empty PNG", "empty.png", {} },
			{ "PNG cut short", "cut.png", std::vector<unsigned char>(goodPng.begin(), goodPng.begin() + goodPng.size() / 2) },
			{ "PNG too large", "big.png", bigPng },
			{ "PNG too small", "thin.png", thinPng },
			{ "empty R16", "empty.r16", {} },
			{ "odd R16 size", "odd.r16", std::vector<unsigned char>(5) },
			{ "not square without a size", "wide.r16", std::vector<unsigned char>(6 * 2) },
			{ "size in the name does not match", "map_3x3.r16", std::vector<unsigned char>(8 * 2) },
			{ "size in the name too large", "map_9000x2.r16", std::vector<unsigned char>(18000 * 2) },
			{ "single sample", "one.r16", std::vector<unsigned char>(2) },
		};

		HeightField existing(3, 3);
		for (const Case& c : cases) {
			CAPTURE(c.name);
			WriteBytes(folder / c.fileName, c.bytes);
			error.clear();
			CHECK_FALSE(heightmap::Import(folder / c.fileName, heightmap::terrainRange, existing, info, error));
			CHECK_FALSE(error.empty());
			CHECK(existing.GetWidth() == 3);
		}
	}
	SUBCASE("A missing file is an error")
	{
		CHECK_FALSE(heightmap::Import(folder / "missing.png", heightmap::terrainRange, loaded, info, error));
		CHECK_FALSE(error.empty());
	}
	SUBCASE("Listing returns supported files only, sorted")
	{
		WriteBytes(folder / "b.r16", { 0, 0 });
		WriteBytes(folder / "a.PNG", { 0, 0 });
		WriteBytes(folder / "c.raw", { 0, 0 });
		WriteBytes(folder / "notes.txt", { 0, 0 });
		const auto files = heightmap::ListImportable(folder);
		REQUIRE(files.size() == 3);
		CHECK(files[0].filename() == "a.PNG");
		CHECK(files[1].filename() == "b.r16");
		CHECK(files[2].filename() == "c.raw");

		CHECK(heightmap::ListImportable(folder / "missing").empty());
	}

	std::filesystem::remove_all(folder);
}
