#include "doctest/doctest.h"

#include "Erosion.h"
#include "TestUtils.h"

#include <cmath>
#include <numeric>
#include <vector>

using erosion::Droplet;
using erosion::Erosion;
using erosion::ErosionConfig;

//Map with a constant slope: height = sx * x + sy * y
static std::vector<float> MakeSlope(int width, int height, float sx, float sy)
{
	std::vector<float> map(static_cast<size_t>(width) * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++)
			map[static_cast<size_t>(y) * width + x] = sx * x + sy * y;
	}
	return map;
}

static float Sum(const HeightField& field)
{
	return std::accumulate(field.Data(), field.Data() + field.Size(), 0.0f);
}

TEST_CASE("SetMap copies the map until DontChangeMap is called")
{
	std::vector<float> first = MakeSlope(4, 3, 1.0f, 0.0f);
	std::vector<float> second = MakeSlope(4, 3, 2.0f, 0.0f);

	Erosion erosion(4, 3);
	erosion.SetMap(first.data());
	first[1] = 100.0f;
	CHECK(erosion.GetField().At(1, 0) == 1.0f);

	erosion.DontChangeMap();
	erosion.SetMap(second.data());
	CHECK(erosion.GetField().At(1, 0) == 1.0f);

	erosion.ChangeMap();
	erosion.SetMap(second.data());
	CHECK(erosion.GetField().At(1, 0) == 2.0f);
}

TEST_CASE("IsOnMap excludes the last row and column")
{
	Erosion erosion(4, 3);
	CHECK(erosion.IsOnMap({ 0.0f, 0.0f }));
	CHECK(erosion.IsOnMap({ 2.9f, 1.9f }));
	CHECK_FALSE(erosion.IsOnMap({ 3.0f, 1.0f }));
	CHECK_FALSE(erosion.IsOnMap({ 1.0f, 2.0f }));
	CHECK_FALSE(erosion.IsOnMap({ -0.1f, 1.0f }));
	CHECK_FALSE(erosion.IsOnMap({ 1.0f, -0.1f }));
}

TEST_CASE("Height is interpolated bilinearly")
{
	std::vector<float> map = MakeSlope(4, 3, 1.0f, 10.0f);
	Erosion erosion(4, 3);
	erosion.SetMap(map.data());

	CHECK(erosion.GetInterpolatedGridHeight({ 2.0f, 1.0f }) == doctest::Approx(12.0f));
	CHECK(erosion.GetInterpolatedGridHeight({ 2.5f, 1.5f }) == doctest::Approx(17.5f));
	CHECK(erosion.GetElevationDifference({ 0.5f, 0.5f }, { 1.5f, 0.5f }) == doctest::Approx(1.0f));
}

TEST_CASE("Gradient matches the slope of the map")
{
	std::vector<float> map = MakeSlope(5, 4, 0.1f, 0.2f);
	Erosion erosion(5, 4);
	erosion.SetMap(map.data());

	erosion::vec2 gradient = erosion.GetGradient({ 1.5f, 1.5f });
	CHECK(gradient.x == doctest::Approx(0.1f));
	CHECK(gradient.y == doctest::Approx(0.2f));
}

TEST_CASE("DistributeSediment splits the sediment over the cell corners")
{
	std::vector<float> map(16, 0.0f);
	Erosion erosion(4, 4);
	erosion.SetMap(map.data());

	erosion.DistributeSediment({ 1.25f, 1.5f }, 1.0f);

	const HeightField& field = erosion.GetField();
	CHECK(field.At(1, 1) == doctest::Approx(0.375f));
	CHECK(field.At(2, 1) == doctest::Approx(0.125f));
	CHECK(field.At(1, 2) == doctest::Approx(0.375f));
	CHECK(field.At(2, 2) == doctest::Approx(0.125f));
	CHECK(Sum(field) == doctest::Approx(1.0f));
}

TEST_CASE("ErodeRadius removes from the map exactly what it returns")
{
	for (float blur : { 0.0f, 0.5f }) {
		CAPTURE(blur);
		std::vector<float> map(81, 1.0f);
		map[4 * 9 + 5] = 0.0f;	//New position of the droplet, only higher points get eroded

		Erosion erosion(9, 9);
		erosion.SetMap(map.data());
		float before = Sum(erosion.GetField());

		ErosionConfig config;
		config.blur = blur;
		float eroded = erosion.ErodeRadius(config, { 4.0f, 4.0f }, { 5.0f, 4.0f }, 0.5f);

		CHECK(eroded == doctest::Approx(0.5f * (1.0f - blur)));
		CHECK(before - Sum(erosion.GetField()) == doctest::Approx(eroded).epsilon(0.001));
	}
}

TEST_CASE("Droplet moves against the gradient")
{
	Droplet droplet({ 5.0f, 5.0f }, 1.0f, 1.0f, 1.0f);
	droplet.AdjustDirection({ 1.0f, 0.0f }, 0.0f);

	CHECK(droplet.GetDirection().x == doctest::Approx(-1.0f));
	CHECK(droplet.GetPosition().x == doctest::Approx(4.0f));
	CHECK(droplet.GetPosition().y == doctest::Approx(5.0f));
}

TEST_CASE("Droplet on flat ground moves by one cell in a random direction")
{
	Droplet droplet({ 5.0f, 5.0f }, 1.0f, 1.0f, 1.0f);
	droplet.AdjustDirection({ 0.0f, 0.0f }, 0.5f);

	float dx = droplet.GetPosition().x - 5.0f;
	float dy = droplet.GetPosition().y - 5.0f;
	CHECK(std::sqrt(dx * dx + dy * dy) == doctest::Approx(1.0f));
}

TEST_CASE("Droplet evaporates a fraction of its water")
{
	Droplet droplet({ 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f);
	droplet.Evaporate(0.25f);
	CHECK(droplet.GetWater() == doctest::Approx(0.75f));
}

TEST_CASE("Droplet going uphill drops at most the sediment it carries")
{
	Droplet droplet({ 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f);
	droplet.AdjustSediment(0.3f);

	CHECK(droplet.DropSediment(0.1f) == doctest::Approx(0.1f));
	CHECK(droplet.GetSediment() == doctest::Approx(0.2f));
	CHECK(droplet.DropSediment(1.0f) == doctest::Approx(0.2f));
	CHECK(droplet.GetSediment() == doctest::Approx(0.0f));
}

TEST_CASE("Droplet below its capacity gathers, above its capacity deposits")
{
	SUBCASE("gathers a part of the free capacity")
	{
		Droplet droplet({ 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f);
		CHECK(droplet.AdjustCapacity(0.0f, 0.5f, 0.5f, -0.2f) == doctest::Approx(-0.1f));
		CHECK(droplet.GetCapacity() == doctest::Approx(0.2f));
	}
	SUBCASE("gathers no more than the height difference")
	{
		Droplet droplet({ 0.0f, 0.0f }, 2.0f, 1.0f, 1.0f);
		CHECK(droplet.AdjustCapacity(0.0f, 1.0f, 0.5f, -0.2f) == doctest::Approx(-0.2f));
	}
	SUBCASE("deposits a part of the surplus")
	{
		Droplet droplet({ 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f);
		droplet.AdjustSediment(1.0f);
		CHECK(droplet.AdjustCapacity(0.0f, 0.5f, 0.5f, -0.2f) == doctest::Approx(0.4f));
		CHECK(droplet.GetSediment() == doctest::Approx(0.6f));
	}
}

//BUG: AdjustVelocity gets new height - old height, which is negative downhill, so droplets slow down going down
//and speed up going up. Below a certain speed the square root also turns NaN, and a NaN capacity makes ErodeRadius
//dig the neighbouring cells down to 0. Remove should_fail after the fix (see ROADMAP, B20)
TEST_CASE("Droplet speeds up going downhill" * doctest::should_fail())
{
	Droplet droplet({ 0.0f, 0.0f }, 1.0f, 1.0f, 1.0f);
	droplet.AdjustVelocity(-0.5f, 1.0f);
	CHECK(droplet.GetVelocity() > 1.0f);
}

TEST_CASE("Erode without droplets leaves the map untouched")
{
	std::vector<float> map = MakeSlope(TEST_WIDTH, TEST_HEIGHT, 0.01f, 0.005f);
	Erosion erosion(TEST_WIDTH, TEST_HEIGHT);
	erosion.SetMap(map.data());

	ErosionConfig config;
	config.dropletCount = 0;
	erosion.Erode(config, std::nullopt);

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		return erosion.GetField().At(x, y) != map[static_cast<size_t>(y) * TEST_WIDTH + x];
	}) == 0);
}

//Droplet positions are random, so only properties that hold for every run are checked
TEST_CASE("Erode changes the map and keeps heights finite and non-negative")
{
	std::vector<float> map = MakeSlope(TEST_WIDTH, TEST_HEIGHT, 0.01f, 0.005f);
	Erosion erosion(TEST_WIDTH, TEST_HEIGHT);
	erosion.SetMap(map.data());

	ErosionConfig config;
	config.dropletCount = 500;
	config.dropletLifetime = 32;
	erosion.Erode(config, std::nullopt);

	const HeightField& field = erosion.GetField();
	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		return !std::isfinite(field.At(x, y)) || field.At(x, y) < 0.0f;
	}) == 0);
	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		return field.At(x, y) != map[static_cast<size_t>(y) * TEST_WIDTH + x];
	}) > 0);
}
