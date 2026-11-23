#include "doctest/doctest.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "LandCover.h"
#include "TiffWriter.h"

using namespace testtiff;

namespace {
	std::filesystem::path Folder()
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_landcover_tests";
		std::filesystem::create_directories(folder);
		return folder;
	}

	//A land cover file of classes(x, y)
	std::filesystem::path WriteClasses(const std::string& name, int width, int height, const Geo& geo, const std::function<int(int, int)>& classes,
		uint32_t tile = 0)
	{
		std::vector<double> values(static_cast<size_t>(width) * height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				values[static_cast<size_t>(y) * width + x] = classes(x, y);
			}
		}
		const std::filesystem::path path = Folder() / name;
		WriteTiff(path, ClassSpec(width, height, geo, tile), values);
		return path;
	}

	Geo Degrees(double left, double top, double pixel)
	{
		Geo geo;
		geo.pixel = pixel;
		geo.epsg = 4326;
		geo.leftX = left;
		geo.topY = top;
		return geo;
	}

	int At(const landcover::ClassMap& map, int x, int y)
	{
		return map.classes[static_cast<size_t>(y) * map.width + x];
	}
}

TEST_CASE("Land cover in degrees under a map in degrees")
{
	const std::filesystem::path file = WriteClasses("halves.tif", 40, 40, Degrees(19.0, 51.0, 0.001), [](int x, int) { return x < 20 ? 10 : 30; });
	const geo::GeoReference map{ 4326, { 19.010, 50.990 }, { 0.002, -0.002 } };
	const landcover::ClassMap result = landcover::Compute({ file }, 10, 10, map);
	REQUIRE(result.width == 10);
	REQUIRE(result.height == 10);
	REQUIRE(result.classes.size() == 100);
	for (int y = 0; y < 10; y++) {
		for (int x = 0; x < 10; x++) {
			CHECK(At(result, x, y) == (x < 5 ? 10 : 30));
		}
	}
	REQUIRE(result.files.size() == 1);
	CHECK(result.files[0].error.empty());
	CHECK(result.files[0].skipped.empty());
	CHECK_FALSE(result.files[0].stretched);
	CHECK(result.files[0].coverage == doctest::Approx(1.0f));
	CHECK(result.counts[10] == 50);
	CHECK(result.counts[30] == 50);
}

TEST_CASE("Most frequent class wins in a map pixel")
{
	//Map pixel = 4 x 4 file pixels; block (0, 0): 9 x 40 and 7 x 20, block (1, 0): 8 x 20 and 8 x 40, block (2, 0): 16 x 50
	auto classes = [](int x, int y) {
		const int block = x / 4, i = (y % 4) * 4 + x % 4;
		if (y >= 4) {
			return 70;
		}
		if (block == 0) {
			return i < 9 ? 40 : 20;
		}
		if (block == 1) {
			return i < 8 ? 40 : 20;
		}
		return 50;
	};
	const std::filesystem::path file = WriteClasses("majority.tif", 12, 8, Degrees(19.0, 51.0, 0.001), classes);
	//A raster limit above the file: the default (2 x the map side = 6) would reduce it first
	const landcover::ClassMap result = landcover::Compute({ file }, 3, 2, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.004, -0.004 } }, 64);
	REQUIRE(result.classes.size() == 6);
	CHECK(At(result, 0, 0) == 40);
	CHECK(At(result, 1, 0) == 20);
	CHECK(At(result, 2, 0) == 50);
	CHECK(At(result, 0, 1) == 70);
}

TEST_CASE("Map in UTM over a file in degrees")
{
	const std::filesystem::path file = WriteClasses("meridian.tif", 200, 200, Degrees(20.9, 52.1, 0.001), [](int x, int) { return x < 100 ? 10 : 30; });
	const geo::Projection utm = *geo::TransverseMercatorOf(32634);
	const glm::dvec2 centre = geo::ToMetres(utm, { 21.0, 52.0 });
	const geo::GeoReference map{ 32634, { centre.x - 1000.0, centre.y + 1000.0 }, { 100.0, -100.0 } };
	const landcover::ClassMap result = landcover::Compute({ file }, 20, 20, map);
	REQUIRE(result.classes.size() == 400);
	for (int y = 0; y < 20; y++) {
		int wrong = 0;
		for (int x = 0; x < 20; x++) {
			const glm::dvec2 lonLat = *geo::PixelToDegrees(map, { x + 0.5, y + 0.5 });
			wrong += At(result, x, y) != (lonLat.x < 21.0 ? 10 : 30);
		}
		CHECK(wrong <= 1);
	}
	CHECK(result.files[0].coverage == doctest::Approx(1.0f));
}

TEST_CASE("Two files cover the two halves of the map")
{
	const std::filesystem::path west = WriteClasses("west.tif", 20, 20, Degrees(19.00, 51.0, 0.001), [](int, int) { return 10; });
	const std::filesystem::path east = WriteClasses("east.tif", 20, 20, Degrees(19.02, 51.0, 0.001), [](int, int) { return 50; });
	const landcover::ClassMap result = landcover::Compute({ west, east }, 10, 5, geo::GeoReference{ 4326, { 19.01, 50.995 }, { 0.002, -0.002 } });
	REQUIRE(result.classes.size() == 50);
	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 10; x++) {
			CHECK(At(result, x, y) == (x < 5 ? 10 : 50));
		}
	}
	CHECK(result.counts[0] == 0);
	REQUIRE(result.files.size() == 2);
	CHECK(result.files[0].coverage == doctest::Approx(0.5f));
	CHECK(result.files[1].coverage == doctest::Approx(0.5f));
}

TEST_CASE("Map without georeference stretches the first file")
{
	const std::filesystem::path plain = WriteClasses("plain.tif", 8, 4, Geo{}, [](int x, int) { return 10 * (x / 2 + 1); });
	const std::filesystem::path other = WriteClasses("plain_other.tif", 8, 4, Geo{}, [](int, int) { return 90; });
	const landcover::ClassMap result = landcover::Compute({ plain, other }, 4, 2, std::nullopt);
	REQUIRE(result.classes.size() == 8);
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 4; x++) {
			CHECK(At(result, x, y) == 10 * (x + 1));
		}
	}
	REQUIRE(result.files.size() == 2);
	CHECK(result.files[0].stretched);
	CHECK(result.files[0].coverage == doctest::Approx(1.0f));
	CHECK_FALSE(result.files[1].skipped.empty());
	CHECK(result.files[1].coverage == 0.0f);

	//A system the application does not know is stretched too
	const landcover::ClassMap unknown = landcover::Compute({ plain }, 4, 2, geo::GeoReference{ 0, { 100.0, 200.0 }, { 2.0, -2.0 } });
	CHECK(unknown.files[0].stretched);
	CHECK(At(unknown, 3, 1) == 40);
}

TEST_CASE("File without georeference is skipped under a georeferenced map")
{
	const std::filesystem::path plain = WriteClasses("plain_skip.tif", 8, 4, Geo{}, [](int, int) { return 90; });
	const std::filesystem::path placed = WriteClasses("placed.tif", 20, 20, Degrees(19.0, 51.0, 0.001), [](int, int) { return 20; });
	const landcover::ClassMap result = landcover::Compute({ plain, placed }, 5, 5, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.002, -0.002 } });
	REQUIRE(result.files.size() == 2);
	CHECK_FALSE(result.files[0].skipped.empty());
	CHECK(result.files[1].skipped.empty());
	CHECK(result.counts[20] == 25);
	CHECK(result.counts[90] == 0);
}

TEST_CASE("No data and outside the files give 0")
{
	const std::filesystem::path file = WriteClasses("nodata.tif", 10, 10, Degrees(19.0, 51.0, 0.001), [](int x, int) { return x < 5 ? 0 : 60; });
	const landcover::ClassMap result = landcover::Compute({ file }, 15, 10, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.001, -0.001 } });
	REQUIRE(result.classes.size() == 150);
	for (int x = 0; x < 15; x++) {
		CHECK(At(result, x, 3) == (x >= 5 && x < 10 ? 60 : 0));
	}
	CHECK(result.counts[0] == 100);
	CHECK(result.counts[60] == 50);
	CHECK(result.files[0].coverage == doctest::Approx(50.0f / 150.0f));
}

TEST_CASE("A large window is reduced by blocks")
{
	//Every 20 x 20 block of the file has one class except 16 pixels where 4 x 4 samples of a map pixel would fall without the reduction:
	//read whole, the map would take that minority class; reduced by 10 x 10 blocks first, the majority stays
	auto onSample = [](int v) { const int r = v % 20; return r == 2 || r == 7 || r == 12 || r == 17; };
	auto classes = [&](int x, int y) {
		if (onSample(x) && onSample(y)) {
			return 99;
		}
		return 10 + 10 * ((x / 20 + y / 20) % 5);
	};
	const std::filesystem::path file = WriteClasses("large.tif", 200, 200, Degrees(19.0, 51.0, 0.001), classes, 64);
	const landcover::ClassMap result = landcover::Compute({ file }, 10, 10, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.02, -0.02 } }, 20);
	REQUIRE(result.classes.size() == 100);
	for (int y = 0; y < 10; y++) {
		for (int x = 0; x < 10; x++) {
			CHECK(At(result, x, y) == 10 + 10 * ((x + y) % 5));
		}
	}
	//Read whole, the same file gives the minority class
	const landcover::ClassMap whole = landcover::Compute({ file }, 10, 10, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.02, -0.02 } }, 400);
	CHECK(At(whole, 3, 4) == 99);
}

TEST_CASE("Unreadable file reports an error")
{
	const landcover::ClassMap result = landcover::Compute({ Folder() / "missing.tif" }, 4, 3, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.001, -0.001 } });
	REQUIRE(result.files.size() == 1);
	CHECK_FALSE(result.files[0].error.empty());
	REQUIRE(result.classes.size() == 12);
	CHECK(result.counts[0] == 12);

	//A height map is not a land cover file
	TiffSpec spec;
	spec.width = 4;
	spec.height = 4;
	spec.geo = Degrees(19.0, 51.0, 0.001);
	const std::filesystem::path heights = Folder() / "heights.tif";
	WriteTiff(heights, spec, std::vector<double>(16, 100.0));
	const landcover::ClassMap wrong = landcover::Compute({ heights }, 4, 3, geo::GeoReference{ 4326, { 19.0, 51.0 }, { 0.001, -0.001 } });
	CHECK_FALSE(wrong.files[0].error.empty());
}

//Measure (run with --no-skip): 8192^2 map over a 16384^2 tiled Deflate file
TEST_CASE("Land cover timing" * doctest::skip())
{
	constexpr int side = 16384, tileSide = 512;
	const std::filesystem::path path = Folder() / "timing.tif";
	TIFF* tif = TIFFOpenW(path.wstring().c_str(), "w");
	REQUIRE(tif);
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(side));
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(side));
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, uint16_t(1));
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, uint16_t(8));
	TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, uint16_t(SAMPLEFORMAT_UINT));
	TIFFSetField(tif, TIFFTAG_COMPRESSION, uint16_t(COMPRESSION_DEFLATE));
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, uint16_t(PHOTOMETRIC_MINISBLACK));
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, uint16_t(PLANARCONFIG_CONTIG));
	TIFFSetField(tif, TIFFTAG_TILEWIDTH, static_cast<uint32_t>(tileSide));
	TIFFSetField(tif, TIFFTAG_TILELENGTH, static_cast<uint32_t>(tileSide));
	std::vector<unsigned char> tile(static_cast<size_t>(tileSide) * tileSide);
	for (int ty = 0; ty < side; ty += tileSide) {
		for (int tx = 0; tx < side; tx += tileSide) {
			for (int y = 0; y < tileSide; y++) {
				for (int x = 0; x < tileSide; x++) {
					tile[static_cast<size_t>(y) * tileSide + x] = static_cast<unsigned char>(10 * (1 + ((tx + x) / 37 + (ty + y) / 53) % 10));
				}
			}
			REQUIRE(TIFFWriteTile(tif, tile.data(), static_cast<uint32_t>(tx), static_cast<uint32_t>(ty), 0, 0) > 0);
		}
	}
	TIFFClose(tif);

	const auto start = std::chrono::steady_clock::now();
	const landcover::ClassMap result = landcover::Compute({ path }, 8192, 8192, std::nullopt);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	MESSAGE("Land cover 8192^2 over 16384^2: " << ms << " ms");
	CHECK(result.files[0].coverage > 0.99f);
	std::filesystem::remove(path);
}
