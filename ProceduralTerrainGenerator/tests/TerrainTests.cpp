#include "doctest/doctest.h"

#include "TerrainGenerator.h"
#include "TestUtils.h"

static int CountOutOfRange(const HeightField& field, float min, float max)
{
	return CountCells(field.GetWidth(), field.GetHeight(), [&](int x, int y) {
		float value = field.At(x, y);
		return !(value >= min && value <= max);
	});
}

TEST_CASE("TerrainGenerator validates the size")
{
	TerrainGenerator gen;
	CHECK_FALSE(gen.Initialize(0, 10));
	CHECK_FALSE(gen.GenerateTerrain(TerrainConfig::Default(), 0.0f, 0.0f));

	CHECK(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));
	CHECK(gen.GetWidth() == TEST_WIDTH);
	CHECK(gen.GetHeight() == TEST_HEIGHT);
	CHECK(gen.GetHeightField().Size() == static_cast<size_t>(TEST_WIDTH) * TEST_HEIGHT);
}

TEST_CASE("Linear combine multiplies the normalized component noises")
{
	const TerrainConfig config = TerrainConfig::Default();
	TerrainGenerator gen;
	gen.Initialize(TEST_WIDTH, TEST_HEIGHT);
	REQUIRE(gen.GenerateNoises(config));
	REQUIRE(gen.GenerateTerrain(config, 0.0f, 0.0f));

	const HeightField& continentalness = gen.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS).GetField();
	const HeightField& mountainousness = gen.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS).GetField();
	const HeightField& weirdness = gen.GetSelectedNoise(WorldGenParameter::WEIRDNESS).GetField();

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		float c = (continentalness.At(x, y) + 1.0f) / 2.0f;
		float m = (mountainousness.At(x, y) + 1.0f) / 2.0f;
		float w = (weirdness.At(x, y) + 1.0f) / 2.0f;
		return gen.GetHeightField().At(x, y) != doctest::Approx(c * m * (1.0f - w));
	}) == 0);
}

TEST_CASE("Heights of both combine methods stay within [0, 1]")
{
	TerrainConfig config = TerrainConfig::Default();
	TerrainGenerator gen;
	gen.Initialize(TEST_WIDTH, TEST_HEIGHT);

	REQUIRE(gen.GenerateTerrain(config, 0.0f, 0.0f));
	CHECK(CountOutOfRange(gen.GetHeightField(), 0.0f, 1.0f) == 0);

	config.method = EvaluationMethod::SPLINE_COMBINE;
	REQUIRE(gen.GenerateTerrain(config, 0.0f, 0.0f));
	CHECK(CountOutOfRange(gen.GetHeightField(), 0.0f, 1.0f) == 0);
}

TEST_CASE("Spline combine multiplies the spline factors")
{
	TerrainConfig config = TerrainConfig::Default();
	config.method = EvaluationMethod::SPLINE_COMBINE;
	const SplinePoints half = { { -1.0, 0.0, 1.0 }, { 0.5, 0.5, 0.5 } };
	config.continentalnessSpline = half;
	config.mountainousnessSpline = half;
	config.weirdnessSpline = half;

	TerrainGenerator gen;
	gen.Initialize(TEST_WIDTH, TEST_HEIGHT);
	REQUIRE(gen.GenerateTerrain(config, 0.0f, 0.0f));
	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		return gen.GetHeightField().At(x, y) != doctest::Approx(0.125f);
	}) == 0);
}

TEST_CASE("An unimplemented combine method gives a flat map")
{
	TerrainConfig config = TerrainConfig::Default();
	config.method = EvaluationMethod::C;

	TerrainGenerator gen;
	gen.Initialize(TEST_WIDTH, TEST_HEIGHT);
	REQUIRE(gen.GenerateTerrain(config, 0.0f, 0.0f));
	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) { return gen.GetHeightField().At(x, y) != 0.0f; }) == 0);
}

TEST_CASE("Terrain is a function of the world position")
{
	const TerrainConfig config = TerrainConfig::Default();
	TerrainGenerator base, shifted;
	base.Initialize(TEST_WIDTH, TEST_HEIGHT);
	shifted.Initialize(TEST_WIDTH, TEST_HEIGHT);
	REQUIRE(base.GenerateTerrain(config, 0.0f, 0.0f));
	REQUIRE(shifted.GenerateTerrain(config, 10.0f, 5.0f));

	CHECK(CountCells(TEST_WIDTH - 10, TEST_HEIGHT - 5, [&](int x, int y) {
		return shifted.GetHeightField().At(x, y) != base.GetHeightField().At(x + 10, y + 5);
	}) == 0);
}

TEST_CASE("Resizing keeps generation working")
{
	const TerrainConfig config = TerrainConfig::Default();
	TerrainGenerator gen;
	gen.Initialize(TEST_WIDTH, TEST_HEIGHT);
	REQUIRE(gen.Resize(32, 16));
	REQUIRE(gen.GenerateNoises(config));
	REQUIRE(gen.GenerateTerrain(config, 0.0f, 0.0f));

	CHECK(gen.GetHeightField().GetWidth() == 32);
	CHECK(gen.GetHeightField().GetHeight() == 16);
	CHECK(gen.GetSelectedNoise(WorldGenParameter::WEIRDNESS).GetWidth() == 32);
}

TEST_CASE("A component noise below -1 is reported as a failure")
{
	//Ridge gain 10 gives 1 - |10|h| - 9|, which goes down to -8
	TerrainConfig config = TerrainConfig::Default();
	config.weirdness.RidgeGain = 10.0f;

	TerrainGenerator gen;
	gen.Initialize(TEST_WIDTH, TEST_HEIGHT);
	CHECK_FALSE(gen.GenerateTerrain(config, 0.0f, 0.0f));
}
