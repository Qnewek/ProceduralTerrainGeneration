#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lodepng.h>
#include <tiffio.h>

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
		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ValueRange{ 0.0f, 65535.0f }, loaded, info, error), error);
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

		REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ValueRange{ 0.0f, 65535.0f }, loaded, info, error), error);
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

#include "TiffWriter.h"

using namespace testtiff;

TEST_CASE("GeoTIFF int16 with Deflate in metres")
{
	TiffSpec spec;
	spec.width = 7;
	spec.height = 5;
	spec.compression = COMPRESSION_ADOBE_DEFLATE;
	spec.geo.pixel = 30.0;
	std::vector<double> values;
	for (int y = 0; y < spec.height; y++) {
		for (int x = 0; x < spec.width; x++) {
			values.push_back(y * 100 + x - 50);
		}
	}
	const std::filesystem::path path = TiffFolder() / "deflate.tif";
	WriteTiff(path, spec, values);

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	REQUIRE(map.GetWidth() == 7);
	REQUIRE(map.GetHeight() == 5);
	CHECK(info.metres);
	CHECK(info.bitsPerSample == 16);
	CHECK_FALSE(info.floatingPoint);
	REQUIRE(info.pixelSize.has_value());
	CHECK(info.pixelSize->x == 30.0);
	CHECK(info.pixelSize->y == 30.0);
	CHECK_FALSE(info.geographic);
	for (size_t i = 0; i < map.Size(); i++) {
		CAPTURE(i);
		CHECK(map[i] == static_cast<float>(values[i]));
	}
}

TEST_CASE("GeoTIFF float32 tiled, tiles past the edge")
{
	TiffSpec spec;
	spec.width = 20;
	spec.height = 17;
	spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
	spec.bits = 32;
	spec.compression = COMPRESSION_LZW;
	spec.tile = 16;
	std::vector<double> values;
	for (int y = 0; y < spec.height; y++) {
		for (int x = 0; x < spec.width; x++) {
			values.push_back(x * 0.5 + y);
		}
	}
	const std::filesystem::path path = TiffFolder() / "tiled.tif";
	WriteTiff(path, spec, values);

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	REQUIRE(map.GetWidth() == 20);
	REQUIRE(map.GetHeight() == 17);
	CHECK(info.floatingPoint);
	CHECK_FALSE(info.pixelSize.has_value());
	int wrong = 0;
	for (size_t i = 0; i < map.Size(); i++) {
		wrong += map[i] != static_cast<float>(values[i]);
	}
	CHECK(wrong == 0);
}

TEST_CASE("GeoTIFF NoData is filled")
{
	TiffSpec spec;
	spec.width = 4;
	spec.height = 4;
	spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
	spec.bits = 32;
	spec.geo.noData = "-9999";
	std::vector<double> values(16, 5.0);
	values[0] = -9999.0;
	values[1 * 4 + 3] = -9999.0;
	const std::filesystem::path path = TiffFolder() / "nodata.tif";
	WriteTiff(path, spec, values);

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.noDataFilled == 2);
	for (size_t i = 0; i < map.Size(); i++) {
		CAPTURE(i);
		CHECK(map[i] == doctest::Approx(5.0f));
	}
}

TEST_CASE("GeoTIFF in degrees")
{
	TiffSpec spec;
	spec.width = 6;
	spec.height = 10;
	spec.geo.pixel = 1.0 / 3600.0;
	spec.geo.geographic = true;
	spec.geo.topY = 50.0;
	const std::filesystem::path path = TiffFolder() / "degrees.tif";
	WriteTiff(path, spec, std::vector<double>(60, 100.0));

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.geographic);
	CHECK(info.latitude == doctest::Approx(50.0 - 5.0 / 3600.0).epsilon(1e-9));
	REQUIRE(info.pixelSize.has_value());
	CHECK(info.pixelSize->x == doctest::Approx(1.0 / 3600.0));
}

TEST_CASE("Inspect reads the header only")
{
	TiffSpec spec;
	spec.width = 7;
	spec.height = 5;
	spec.geo.pixel = 30.0;
	const std::filesystem::path path = TiffFolder() / "inspect.tif";
	WriteTiff(path, spec, std::vector<double>(35, 1.0));

	heightmap::FileHeader header;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Inspect(path, header, error), error);
	CHECK(header.width == 7);
	CHECK(header.height == 5);
	CHECK(header.info.bitsPerSample == 16);
	CHECK(header.info.metres);
	REQUIRE(header.info.pixelSize.has_value());
	CHECK(header.info.pixelSize->x == 30.0);
}

TEST_CASE("Inspect of PNG and R16 reads only the header")
{
	const std::filesystem::path folder = TiffFolder();
	size_t clipped = 0;
	std::string error;
	const std::filesystem::path png = folder / "header.png";
	REQUIRE_MESSAGE(heightmap::Export(png, Ramp(9, 4, heightmap::terrainRange), heightmap::Format::PNG16, heightmap::terrainRange, clipped, error), error);
	//Image data cut off: the header is still whole
	std::filesystem::resize_file(png, 60);

	heightmap::FileHeader header;
	REQUIRE_MESSAGE(heightmap::Inspect(png, header, error), error);
	CHECK(header.width == 9);
	CHECK(header.height == 4);
	CHECK(header.info.bitsPerSample == 16);
	CHECK_FALSE(header.info.metres);
	HeightField map;
	heightmap::ImportInfo info;
	CHECK_FALSE(heightmap::Import(png, heightmap::ImportOptions{}, map, info, error));

	const std::filesystem::path r16 = folder / "header_3x2.r16";
	WriteBytes(r16, std::vector<unsigned char>(12, 0));
	REQUIRE_MESSAGE(heightmap::Inspect(r16, header, error), error);
	CHECK(header.width == 3);
	CHECK(header.height == 2);
	CHECK(header.info.bitsPerSample == 16);
}

TEST_CASE("GeoTIFF with three samples per pixel is refused")
{
	TiffSpec spec;
	spec.width = 4;
	spec.height = 4;
	spec.samplesPerPixel = 3;
	spec.sampleFormat = SAMPLEFORMAT_UINT;
	const std::filesystem::path path = TiffFolder() / "rgb.tif";
	WriteTiff(path, spec, std::vector<double>(16, 1.0));

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	CHECK_FALSE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error));
	CHECK(error.find("sample") != std::string::npos);
}

TEST_CASE("Import crops then downsamples")
{
	TiffSpec spec;
	spec.width = 8;
	spec.height = 6;
	spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
	spec.bits = 32;
	std::vector<double> values;
	for (int y = 0; y < spec.height; y++) {
		for (int x = 0; x < spec.width; x++) {
			values.push_back(x);
		}
	}
	const std::filesystem::path path = TiffFolder() / "crop.tif";
	WriteTiff(path, spec, values);

	heightmap::ImportOptions options;
	options.crop = heightmap::PixelRect{ 2, 1, 4, 4 };
	options.downsample = 2;
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, options, map, info, error), error);
	REQUIRE(map.GetWidth() == 2);
	REQUIRE(map.GetHeight() == 2);
	CHECK(map.At(0, 0) == doctest::Approx(2.5f));
	CHECK(map.At(1, 0) == doctest::Approx(4.5f));

	options.crop = heightmap::PixelRect{ 6, 0, 4, 4 };
	CHECK_FALSE(heightmap::Import(path, options, map, info, error));
	CHECK(error.find("crop") != std::string::npos);
}

static void WriteText(const std::filesystem::path& path, const std::string& text)
{
	std::ofstream file(path, std::ios::binary);
	file << text;
}

//SRTM tile: big-endian int16, rows from north
static void WriteHgt(const std::filesystem::path& path, int side, const std::vector<int>& values)
{
	std::vector<unsigned char> bytes;
	for (int v : values) {
		const uint16_t s = static_cast<uint16_t>(static_cast<int16_t>(v));
		bytes.push_back(static_cast<unsigned char>(s >> 8));
		bytes.push_back(static_cast<unsigned char>(s & 0xFF));
	}
	REQUIRE(values.size() == static_cast<size_t>(side) * side);
	WriteBytes(path, bytes);
}

TEST_CASE("HGT 3 arc-seconds")
{
	constexpr int side = 1201;
	std::vector<int> values(static_cast<size_t>(side) * side);
	for (int y = 0; y < side; y++) {
		for (int x = 0; x < side; x++) {
			values[static_cast<size_t>(y) * side + x] = (y * side + x) % 3000;
		}
	}
	values[static_cast<size_t>(600) * side + 600] = -32768;
	const std::filesystem::path path = TiffFolder() / "N50E019.hgt";
	WriteHgt(path, side, values);

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	REQUIRE(map.GetWidth() == side);
	REQUIRE(map.GetHeight() == side);
	CHECK(info.metres);
	CHECK(info.geographic);
	REQUIRE(info.pixelSize.has_value());
	CHECK(info.pixelSize->x == doctest::Approx(1.0 / 1200.0));
	CHECK(info.pixelSize->y == doctest::Approx(1.0 / 1200.0));
	CHECK(info.latitude == doctest::Approx(50.5));
	CHECK(info.noDataFilled == 1);
	CHECK(map.At(0, 0) == 0.0f);
	CHECK(map.At(5, 0) == 5.0f);
	CHECK(map.At(0, 1) == static_cast<float>(1201 % 3000));
	CHECK(map.At(1200, 1200) == static_cast<float>((1200 * side + 1200) % 3000));

	heightmap::FileHeader header;
	REQUIRE_MESSAGE(heightmap::Inspect(path, header, error), error);
	CHECK(header.width == side);
	CHECK(header.info.geographic);

	const std::filesystem::path south = TiffFolder() / "S10W020.hgt";
	WriteHgt(south, side, std::vector<int>(values.size(), 7));
	REQUIRE_MESSAGE(heightmap::Import(south, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.latitude == doctest::Approx(-9.5));

	const std::filesystem::path broken = TiffFolder() / "N01E001.hgt";
	WriteBytes(broken, std::vector<unsigned char>(1000 * 1000 * 2, 0));
	CHECK_FALSE(heightmap::Import(broken, heightmap::ImportOptions{}, map, info, error));
}

TEST_CASE("ASC with xllcenter and no NODATA")
{
	const std::filesystem::path path = TiffFolder() / "center.asc";
	WriteText(path, "ncols 3\nnrows 2\nxllcenter 1000.0\nyllcenter 2000.0\ncellsize 25\n1 2 3\n4 5 6\n");
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	REQUIRE(map.GetWidth() == 3);
	REQUIRE(map.GetHeight() == 2);
	CHECK(map.At(0, 0) == 1.0f);
	CHECK(map.At(2, 0) == 3.0f);
	CHECK(map.At(0, 1) == 4.0f);
	CHECK(info.metres);
	REQUIRE(info.pixelSize.has_value());
	CHECK(info.pixelSize->x == 25.0);
	CHECK_FALSE(info.geographic);
}

TEST_CASE("ASC with NODATA_value")
{
	const std::filesystem::path path = TiffFolder() / "nodata.asc";
	WriteText(path, "NCOLS 3\nNROWS 3\nXLLCORNER 0\nYLLCORNER 0\nCELLSIZE 10\nNODATA_value -9999\n2 2 2\n2 -9999 2\n2 2 2\n");
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.noDataFilled == 1);
	CHECK(map.At(1, 1) == doctest::Approx(2.0f));
}

TEST_CASE("ASC in degrees is recognised")
{
	const std::filesystem::path path = TiffFolder() / "degrees.asc";
	WriteText(path, "ncols 4\nnrows 4\nxllcorner 19\nyllcorner 50\ncellsize 0.000833333\n1 1 1 1\n1 1 1 1\n1 1 1 1\n1 1 1 1\n");
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.geographic);
	CHECK(info.latitude == doctest::Approx(50.0 + 2.0 * 0.000833333));
}

TEST_CASE("GeoTIFF float with huge sentinels and no NoData tag")
{
	//Processed DEMs often mark holes with -FLT_MAX without saying so: they must not reach the map (inf / NaN on the GPU)
	TiffSpec spec;
	spec.width = 4;
	spec.height = 4;
	spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
	spec.bits = 32;
	std::vector<double> values(16, 5.0);
	values[5] = -3.4e38;
	values[6] = -3.4e38;
	const std::filesystem::path path = TiffFolder() / "sentinel.tif";
	WriteTiff(path, spec, values);

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.noDataFilled == 2);
	CHECK(map[5] == doctest::Approx(5.0f));
	CHECK(map[6] == doctest::Approx(5.0f));
}

TEST_CASE("A crop of NoData only fails")
{
	//Missing samples are filled within the kept part: a part with nothing known has no heights to give
	TiffSpec spec;
	spec.width = 6;
	spec.height = 4;
	spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
	spec.bits = 32;
	spec.geo.noData = "-9999";
	std::vector<double> values(24, 10.0);
	for (int y = 0; y < 4; y++) {
		values[static_cast<size_t>(y) * 6] = values[static_cast<size_t>(y) * 6 + 1] = -9999.0;
	}
	const std::filesystem::path path = TiffFolder() / "nodata_crop.tif";
	WriteTiff(path, spec, values);

	heightmap::ImportOptions options;
	options.crop = heightmap::PixelRect{ 0, 0, 2, 4 };
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	CHECK_FALSE(heightmap::Import(path, options, map, info, error));
	CHECK(error.find("NoData") != std::string::npos);

	options.crop = heightmap::PixelRect{ 1, 0, 3, 4 };
	REQUIRE_MESSAGE(heightmap::Import(path, options, map, info, error), error);
	CHECK(info.noDataFilled == 4);
	CHECK(map.At(0, 0) == doctest::Approx(10.0f));
}

TEST_CASE("ASC 1 arc-minute grid is geographic")
{
	const std::filesystem::path path = TiffFolder() / "arcminute.asc";
	WriteText(path, "ncols 4\nnrows 4\nxllcorner -180\nyllcorner -90\ncellsize 0.0166666667\n1 1 1 1\n1 1 1 1\n1 1 1 1\n1 1 1 1\n");
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.geographic);
}

TEST_CASE("HGT named beyond the poles keeps latitude 0")
{
	const std::filesystem::path path = TiffFolder() / "N95E000.hgt";
	WriteHgt(path, 1201, std::vector<int>(1201 * 1201, 1));
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK(info.latitude == 0.0);
}

TEST_CASE("A map larger than 8192 needs a crop or downsample")
{
	TiffSpec spec;
	spec.width = 8200;
	spec.height = 4;
	const std::filesystem::path path = TiffFolder() / "wide.tif";
	WriteTiff(path, spec, std::vector<double>(8200 * 4, 3.0));

	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	CHECK_FALSE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error));
	CHECK(error.find("8192") != std::string::npos);

	heightmap::ImportOptions options;
	options.downsample = 2;
	REQUIRE_MESSAGE(heightmap::Import(path, options, map, info, error), error);
	CHECK(map.GetWidth() == 4100);
	CHECK(map.GetHeight() == 2);
}

//--- Georeference (stage D3) ---

namespace {
	void CheckGeo(const std::optional<geo::GeoReference>& g, int epsg, glm::dvec2 origin, glm::dvec2 pixel, double tolerance = 1e-9)
	{
		REQUIRE(g.has_value());
		CHECK(g->epsg == epsg);
		CHECK(std::abs(g->origin.x - origin.x) <= tolerance);
		CHECK(std::abs(g->origin.y - origin.y) <= tolerance);
		CHECK(std::abs(g->pixel.x - pixel.x) <= tolerance);
		CHECK(std::abs(g->pixel.y - pixel.y) <= tolerance);
	}

	std::filesystem::path WriteUtmTiff(const std::string& name, bool pixelIsPoint)
	{
		TiffSpec spec;
		spec.width = 6;
		spec.height = 10;
		spec.geo.pixel = 30.0;
		spec.geo.epsg = 32634;
		spec.geo.leftX = 400000.0;
		spec.geo.topY = 5800000.0;
		spec.geo.pixelIsPoint = pixelIsPoint;
		const std::filesystem::path path = TiffFolder() / name;
		WriteTiff(path, spec, std::vector<double>(60, 100.0));
		return path;
	}
}

TEST_CASE("GeoTIFF georeference in UTM")
{
	const std::filesystem::path path = WriteUtmTiff("utm.tif", false);
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CheckGeo(info.georeference, 32634, { 400000.0, 5800000.0 }, { 30.0, -30.0 });

	heightmap::FileHeader header;
	REQUIRE_MESSAGE(heightmap::Inspect(path, header, error), error);
	CheckGeo(header.info.georeference, 32634, { 400000.0, 5800000.0 }, { 30.0, -30.0 });
}

TEST_CASE("Pixel is point moves the corner by half a pixel")
{
	const std::filesystem::path path = WriteUtmTiff("utm_point.tif", true);
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CheckGeo(info.georeference, 32634, { 399985.0, 5800015.0 }, { 30.0, -30.0 });
}

TEST_CASE("Crop and downsample move the georeference")
{
	const std::filesystem::path path = WriteUtmTiff("utm_crop.tif", false);
	heightmap::ImportOptions options;
	options.crop = heightmap::PixelRect{ 2, 4, 4, 6 };
	options.downsample = 2;
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, options, map, info, error), error);
	CHECK(map.GetWidth() == 2);
	CHECK(map.GetHeight() == 3);
	CheckGeo(info.georeference, 32634, { 400060.0, 5799880.0 }, { 60.0, -60.0 });
}

TEST_CASE("HGT georeference from its name")
{
	constexpr int side = 1201;
	const std::filesystem::path path = TiffFolder() / "N50E019.hgt";
	WriteHgt(path, side, std::vector<int>(static_cast<size_t>(side) * side, 200));
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CheckGeo(info.georeference, 4326, { 19.0 - 1.0 / 2400.0, 51.0 + 1.0 / 2400.0 }, { 1.0 / 1200.0, -1.0 / 1200.0 }, 1e-12);

	const std::filesystem::path west = TiffFolder() / "S10W020.hgt";
	WriteHgt(west, side, std::vector<int>(static_cast<size_t>(side) * side, 7));
	REQUIRE_MESSAGE(heightmap::Import(west, heightmap::ImportOptions{}, map, info, error), error);
	CheckGeo(info.georeference, 4326, { -20.0 - 1.0 / 2400.0, -9.0 + 1.0 / 2400.0 }, { 1.0 / 1200.0, -1.0 / 1200.0 }, 1e-12);
}

TEST_CASE("ASC with a prj in PUWG 1992")
{
	const std::filesystem::path path = TiffFolder() / "puwg.asc";
	WriteText(path, "ncols 4\nnrows 3\nxllcorner 500000\nyllcorner 300000\ncellsize 1\n1 2 3 4\n5 6 7 8\n9 10 11 12\n");
	WriteText(TiffFolder() / "puwg.prj", "PROJCS[\"ETRF2000-PL / CS92\",GEOGCS[\"ETRF2000-PL\",DATUM[\"ETRF2000_Poland\"]],PROJECTION[\"Transverse_Mercator\"]]");
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CheckGeo(info.georeference, 2180, { 500000.0, 300003.0 }, { 1.0, -1.0 });
}

TEST_CASE("Coordinate system from a prj")
{
	CHECK(heightmap::SystemFromPrj("PROJCS[\"WGS_1984_UTM_Zone_34N\",GEOGCS[\"GCS_WGS_1984\"]]") == 32634);
	CHECK(heightmap::SystemFromPrj("PROJCS[\"WGS 84 / UTM zone 23S\",GEOGCS[\"WGS 84\"]]") == 32723);
	CHECK(heightmap::SystemFromPrj("PROJCS[\"ETRS89 / UTM zone 33N\",GEOGCS[\"ETRS89\"]]") == 25833);
	CHECK(heightmap::SystemFromPrj("PROJCS[\"ETRS_1989_Poland_CS92\",GEOGCS[\"GCS_ETRS_1989\"]]") == 2180);
	CHECK(heightmap::SystemFromPrj("PROJCS[\"ETRF2000-PL / CS2000/21\",GEOGCS[\"ETRF2000-PL\"]]") == 2178);
	CHECK(heightmap::SystemFromPrj("PROJCS[\"ETRS_1989_Poland_CS2000_Zone_6\",GEOGCS[\"GCS_ETRS_1989\"]]") == 2177);
	CHECK(heightmap::SystemFromPrj("GEOGCS[\"GCS_WGS_1984\",DATUM[\"D_WGS_1984\"]]") == 4326);
	CHECK(heightmap::SystemFromPrj("PROJCS[\"ETRS89 / LAEA Europe\",GEOGCS[\"ETRS89\"]]") == 0);
	CHECK(heightmap::SystemFromPrj("") == 0);
}

TEST_CASE("ASC without a prj in metres has no system")
{
	const std::filesystem::path path = TiffFolder() / "noprj.asc";
	std::filesystem::remove(TiffFolder() / "noprj.prj");
	WriteText(path, "ncols 3\nnrows 2\nxllcenter 1000.0\nyllcenter 2000.0\ncellsize 25\n1 2 3\n4 5 6\n");
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CheckGeo(info.georeference, 0, { 987.5, 2037.5 }, { 25.0, -25.0 });

	heightmap::FileHeader header;
	REQUIRE_MESSAGE(heightmap::Inspect(path, header, error), error);
	CheckGeo(header.info.georeference, 0, { 987.5, 2037.5 }, { 25.0, -25.0 });

	heightmap::ImportOptions options;
	options.epsg = 2180;
	REQUIRE_MESSAGE(heightmap::Import(path, options, map, info, error), error);
	CheckGeo(info.georeference, 2180, { 987.5, 2037.5 }, { 25.0, -25.0 });

	//The choice does not replace a system the file names
	const std::filesystem::path utm = WriteUtmTiff("utm_choice.tif", false);
	REQUIRE_MESSAGE(heightmap::Import(utm, options, map, info, error), error);
	CHECK(info.georeference->epsg == 32634);
}

TEST_CASE("PNG has no georeference")
{
	const std::filesystem::path path = TiffFolder() / "nogeo.png";
	std::vector<unsigned char> png;
	REQUIRE(lodepng::encode(png, std::vector<unsigned char>{ 0, 255, 50, 200 }, 2, 2, LCT_GREY, 8) == 0);
	WriteBytes(path, png);
	HeightField map;
	heightmap::ImportInfo info;
	std::string error;
	REQUIRE_MESSAGE(heightmap::Import(path, heightmap::ImportOptions{}, map, info, error), error);
	CHECK_FALSE(info.georeference.has_value());
}
