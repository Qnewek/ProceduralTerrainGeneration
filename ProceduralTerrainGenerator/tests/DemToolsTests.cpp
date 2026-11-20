#include "doctest/doctest.h"

#include <chrono>
#include <cmath>
#include <vector>

#include "DemTools.h"

static HeightField MapOf(int width, int height, const std::vector<float>& values)
{
	HeightField map(width, height);
	for (size_t i = 0; i < map.Size(); i++) {
		map[i] = values[i];
	}
	return map;
}

TEST_CASE("FillNoData averages the known neighbours")
{
	HeightField map = MapOf(3, 3, { 0, 1, 0, 2, -1, 3, 0, 4, 0 });
	std::vector<uint8_t> missing(9, 0);
	missing[4] = 1;
	CHECK(dem::FillNoData(map, missing) == 1);
	CHECK(map.At(1, 1) == doctest::Approx(2.5f));
}

TEST_CASE("FillNoData fills a hole at the corner and the edge")
{
	HeightField map(4, 3);
	std::vector<uint8_t> missing(map.Size(), 0);
	for (size_t i = 0; i < map.Size(); i++) {
		map[i] = 7.0f;
	}
	for (glm::ivec2 p : { glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(3, 2) }) {
		missing[static_cast<size_t>(p.y) * 4 + p.x] = 1;
		map.At(p.x, p.y) = -9999.0f;
	}
	CHECK(dem::FillNoData(map, missing) == 3);
	for (size_t i = 0; i < map.Size(); i++) {
		CAPTURE(i);
		CHECK(map[i] == doctest::Approx(7.0f));
	}
}

TEST_CASE("FillNoData of a map with nothing known fails")
{
	HeightField map = MapOf(2, 2, { 1, 2, 3, 4 });
	const std::vector<uint8_t> missing(4, 1);
	CHECK(dem::FillNoData(map, missing) == -1);
	CHECK(map[0] == 1.0f);
	CHECK(map[3] == 4.0f);
}

TEST_CASE("Crop takes the given rectangle")
{
	HeightField map(4, 3);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 4; x++) {
			map.At(x, y) = static_cast<float>(y * 10 + x);
		}
	}
	const HeightField part = dem::Crop(map, glm::ivec2(1, 1), glm::ivec2(2, 2));
	REQUIRE(part.GetWidth() == 2);
	REQUIRE(part.GetHeight() == 2);
	CHECK(part[0] == 11.0f);
	CHECK(part[1] == 12.0f);
	CHECK(part[2] == 21.0f);
	CHECK(part[3] == 22.0f);
}

TEST_CASE("Downsample averages blocks, partial ones too")
{
	HeightField map(5, 3);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 5; x++) {
			map.At(x, y) = static_cast<float>(x);
		}
	}
	const HeightField small = dem::Downsample(map, 2);
	REQUIRE(small.GetWidth() == 3);
	REQUIRE(small.GetHeight() == 2);
	CHECK(small.At(0, 0) == doctest::Approx(0.5f));
	CHECK(small.At(1, 0) == doctest::Approx(2.5f));
	CHECK(small.At(2, 0) == doctest::Approx(4.0f));
	CHECK(small.At(2, 1) == doctest::Approx(4.0f));
	const HeightField same = dem::Downsample(map, 1);
	CHECK(same.GetWidth() == 5);
	CHECK(same.At(4, 2) == 4.0f);
}

TEST_CASE("FitDownsample keeps maps within 8192")
{
	CHECK(dem::FitDownsample(8192, 100) == 1);
	CHECK(dem::FitDownsample(8193, 100) == 2);
	CHECK(dem::FitDownsample(32768, 32768) == 4);
	CHECK(dem::FitDownsample(20000, 5) == 4);
}

TEST_CASE("Pixel size of geographic maps in metres")
{
	const glm::dvec2 degrees = dem::PixelSizeMetres(glm::dvec2(1.0 / 1200.0), true, 50.0);
	CHECK(degrees.x == doctest::Approx(59.63).epsilon(0.0002));
	CHECK(degrees.y == doctest::Approx(92.145).epsilon(0.0002));
	const glm::dvec2 projected = dem::PixelSizeMetres(glm::dvec2(30.0), false, 0.0);
	CHECK(projected.x == 30.0);
	CHECK(projected.y == 30.0);
}

TEST_CASE("Pixel size stays positive for a latitude beyond the poles")
{
	//A broken tiepoint or file name must not turn the terrain upside down (negative cosine)
	CHECK(dem::PixelSizeMetres(glm::dvec2(1.0 / 3600.0), true, 95.0).x > 0.0);
	CHECK(dem::PixelSizeMetres(glm::dvec2(1.0 / 3600.0), true, -120.0).x > 0.0);
}

TEST_CASE("FillNoData of a large hole runs in linear time")
{
	//Clipped mosaics have most of the file missing, far from any known sample: one pass per wave over all holes took minutes
	constexpr int side = 1500;
	HeightField map(side, side);
	std::vector<uint8_t> missing(map.Size(), 1);
	missing[0] = 0;
	map[0] = 7.0f;
	const auto start = std::chrono::steady_clock::now();
	CHECK(dem::FillNoData(map, missing) == side * side - 1);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	CHECK(ms < 2000.0);
	CHECK(map.At(side - 1, side - 1) == 7.0f);
	CHECK(map.At(side / 2, 3) == 7.0f);
}

TEST_CASE("FillNoData sums in double precision")
{
	HeightField map = MapOf(3, 1, { 3.0e38f, 0.0f, 3.0e38f });
	CHECK(dem::FillNoData(map, { 0, 1, 0 }) == 1);
	CHECK(std::isfinite(map[1]));
	CHECK(map[1] == doctest::Approx(3.0e38f));
}
