#include "doctest/doctest.h"

#include <cstring>
#include <vector>

#include "LandStats.h"

namespace {
	//Map from rows of characters: '#' = land (1), '.' = water (0); not square
	HeightField FromRows(const std::vector<const char*>& rows)
	{
		const int height = static_cast<int>(rows.size());
		const int width = static_cast<int>(std::strlen(rows[0]));
		HeightField map(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				map.At(x, y) = rows[y][x] == '#' ? 1.0f : 0.0f;
			}
		}
		return map;
	}
}

TEST_CASE("LandStats: water share and landmasses connected through edges")
{
	//Three landmasses: 6 texels on the left, 2 touching only at a corner count as two
	const HeightField map = FromRows({
		"##......",
		"##....#.",
		"##.....#",
		"........",
		"........",
	});
	const LandStats stats = land::Measure(map, 0.5f);
	CHECK(stats.waterShare == doctest::Approx(32.0f / 40.0f));
	CHECK(stats.landmasses == 3);
	CHECK(stats.largestShare == doctest::Approx(6.0f / 8.0f));
	//1% of 40 texels rounds down to 0, so every landmass is a continent
	CHECK(stats.continentalShare == doctest::Approx(1.0f));
}

TEST_CASE("LandStats: the water height is the border, heights equal to it are land")
{
	HeightField map(3, 2);
	map.At(0, 0) = 0.29f;
	map.At(1, 0) = 0.3f;
	map.At(2, 0) = 0.31f;
	map.At(0, 1) = map.At(1, 1) = map.At(2, 1) = 0.0f;
	const LandStats stats = land::Measure(map, 0.3f);
	CHECK(stats.waterShare == doctest::Approx(4.0f / 6.0f));
	CHECK(stats.landmasses == 1);
}

TEST_CASE("LandStats: small islands are not continents")
{
	//200 x 100 = 20000 texels, a continent needs 200: one block of 400 and 10 single islands
	HeightField map(200, 100);
	for (int y = 10; y < 30; y++) {
		for (int x = 10; x < 30; x++) {
			map.At(x, y) = 1.0f;
		}
	}
	for (int i = 0; i < 10; i++) {
		map.At(100 + i * 5, 80) = 1.0f;
	}
	const LandStats stats = land::Measure(map, 0.5f);
	CHECK(stats.landmasses == 11);
	CHECK(stats.continentalShare == doctest::Approx(400.0f / 410.0f));
	CHECK(stats.largestShare == doctest::Approx(400.0f / 410.0f));
}

TEST_CASE("LandStats: all water and an empty map")
{
	const LandStats water = land::Measure(HeightField(4, 3), 0.5f);
	CHECK(water.waterShare == doctest::Approx(1.0f));
	CHECK(water.landmasses == 0);
	CHECK(water.largestShare == 0.0f);
	const LandStats empty = land::Measure(HeightField(), 0.5f);
	CHECK(empty.landmasses == 0);
	CHECK(empty.waterShare == 0.0f);
}
