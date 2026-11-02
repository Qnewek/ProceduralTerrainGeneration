#include "doctest/doctest.h"

#include "Noise.h"
#include "Simplex/SimplexNoise.h"
#include "TestUtils.h"

#include <cmath>

using noise::IslandType;
using noise::NoiseConfigParameters;
using noise::Options;
using noise::SimplexNoiseClass;

static NoiseConfigParameters MakeConfig(Options option = Options::NOTHING)
{
	NoiseConfigParameters config;
	config.option = option;
	config.octaves = 4;
	config.constrast = 1.5f;
	return config;
}

//Number of cells of a generated map outside [min, max]; NaN counts as outside
static int CountOutOfRange(const NoiseConfigParameters& config, float min, float max)
{
	SimplexNoiseClass noise;
	noise.Initialize(TEST_WIDTH, TEST_HEIGHT);
	noise.GenerateFractalNoise(config, 0.0f, 0.0f);
	return CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		float value = noise.GetVal(x, y);
		return !(value >= min && value <= max);
	});
}

TEST_CASE("Initialize and Resize validate the size")
{
	SimplexNoiseClass noise;
	CHECK_FALSE(noise.Resize(0, 10));
	CHECK_FALSE(noise.Resize(10, -1));

	CHECK(noise.Initialize(TEST_WIDTH, TEST_HEIGHT));
	CHECK(noise.GetWidth() == TEST_WIDTH);
	CHECK(noise.GetHeight() == TEST_HEIGHT);
	CHECK_FALSE(noise.Initialize(TEST_WIDTH, TEST_HEIGHT));
}

TEST_CASE("Generating without a map fails")
{
	SimplexNoiseClass noise;
	CHECK_FALSE(noise.GenerateFractalNoise(MakeConfig(), 0.0f, 0.0f));
}

TEST_CASE("GetVal rejects coordinates outside the map")
{
	SimplexNoiseClass noise;
	noise.Initialize(TEST_WIDTH, TEST_HEIGHT);
	CHECK(noise.GetVal(TEST_WIDTH, 0) == -2.0f);
	CHECK(noise.GetVal(0, TEST_HEIGHT) == -2.0f);
	CHECK(noise.GetVal(-1, 0) == -2.0f);
}

//BUG: originalPerm in SimplexNoise.cpp lists 247 values, the rest of the array is zero-initialized - 9 values are missing
//and 0 appears 10 times. Fixing it changes every generated world and checksum. Remove should_fail after the fix (see ROADMAP, B21)
TEST_CASE("makePermutation shuffles every byte value exactly once" * doctest::should_fail())
{
	uint8_t perm[256];
	SimplexNoise::makePermutation(623, perm);

	int counts[256] = {};
	for (uint8_t value : perm)
		counts[value]++;

	int wrong = 0;
	for (int count : counts) {
		if (count != 1)
			wrong++;
	}
	CHECK(wrong == 0);
}

TEST_CASE("The same seed gives the same map")
{
	SimplexNoiseClass first, second;
	first.Initialize(TEST_WIDTH, TEST_HEIGHT);
	second.Initialize(TEST_WIDTH, TEST_HEIGHT);
	first.GenerateFractalNoise(MakeConfig(), 0.0f, 0.0f);
	second.GenerateFractalNoise(MakeConfig(), 0.0f, 0.0f);

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) { return first.GetVal(x, y) != second.GetVal(x, y); }) == 0);
}

TEST_CASE("Different seeds give different maps")
{
	NoiseConfigParameters other = MakeConfig();
	other.seed = MakeConfig().seed + 1;

	SimplexNoiseClass first, second;
	first.Initialize(TEST_WIDTH, TEST_HEIGHT);
	second.Initialize(TEST_WIDTH, TEST_HEIGHT);
	first.GenerateFractalNoise(MakeConfig(), 0.0f, 0.0f);
	second.GenerateFractalNoise(other, 0.0f, 0.0f);

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) { return first.GetVal(x, y) != second.GetVal(x, y); }) > 0);
}

TEST_CASE("Going back to a previous seed restores its values")
{
	NoiseConfigParameters a = MakeConfig();
	NoiseConfigParameters b = MakeConfig();
	b.seed = a.seed + 1;

	SimplexNoiseClass noise;
	float expected = noise.PointNoise(a, 12.0f, 34.0f);
	noise.PointNoise(b, 12.0f, 34.0f);
	CHECK(noise.PointNoise(a, 12.0f, 34.0f) == expected);
}

TEST_CASE("Noises with different seeds do not affect each other")
{
	NoiseConfigParameters a = MakeConfig();
	NoiseConfigParameters b = MakeConfig();
	b.seed = a.seed + 1;

	SimplexNoiseClass first, second, alone;
	float expected = alone.PointNoise(a, 12.0f, 34.0f);
	second.PointNoise(b, 12.0f, 34.0f);
	CHECK(first.PointNoise(a, 12.0f, 34.0f) == expected);
}

TEST_CASE("The parallel map matches single samples at (x, y)")
{
	const NoiseConfigParameters config = MakeConfig();
	SimplexNoiseClass noise;
	noise.Initialize(TEST_WIDTH, TEST_HEIGHT);
	noise.GenerateFractalNoise(config, 0.0f, 0.0f);

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		return noise.GetVal(x, y) != noise.PointNoise(config, static_cast<float>(x), static_cast<float>(y));
	}) == 0);
}

TEST_CASE("Moving the origin only shifts the map")
{
	const NoiseConfigParameters config = MakeConfig();
	SimplexNoiseClass base, shifted;
	base.Initialize(TEST_WIDTH, TEST_HEIGHT);
	shifted.Initialize(TEST_WIDTH, TEST_HEIGHT);
	base.GenerateFractalNoise(config, 0.0f, 0.0f);
	shifted.GenerateFractalNoise(config, 10.0f, 5.0f);

	CHECK(CountCells(TEST_WIDTH - 10, TEST_HEIGHT - 5, [&](int x, int y) {
		return shifted.GetVal(x, y) != base.GetVal(x + 10, y + 5);
	}) == 0);
}

TEST_CASE("Noise values stay in the range of the chosen option")
{
	for (int seed : { 1, 623, 90210 }) {
		CAPTURE(seed);
		NoiseConfigParameters config = MakeConfig();
		config.seed = seed;

		config.option = Options::NOTHING;
		CHECK(CountOutOfRange(config, -1.0f, 1.0f) == 0);

		config.option = Options::REFIT_ALL;
		CHECK(CountOutOfRange(config, 0.0f, 1.0f) == 0);

		config.option = Options::FLATTEN_NEGATIVES;
		CHECK(CountOutOfRange(config, 0.0f, 1.0f) == 0);

		config.option = Options::REVERT_NEGATIVES;
		CHECK(CountOutOfRange(config, 0.0f, 1.0f) == 0);
	}
}

TEST_CASE("Ridge turns the noise into 1 - |h| for the default gain and offset")
{
	NoiseConfigParameters ridged = MakeConfig();
	ridged.Ridge = true;
	const NoiseConfigParameters plain = MakeConfig();

	SimplexNoiseClass noise;
	CHECK(CountOutOfRange(ridged, 0.0f, 1.0f) == 0);
	CHECK(noise.PointNoise(ridged, 12.0f, 34.0f) == doctest::Approx(1.0f - std::fabs(noise.PointNoise(plain, 12.0f, 34.0f))));
}

TEST_CASE("Every island type keeps the map within [0, 1]")
{
	for (IslandType type : { IslandType::CONE, IslandType::DIAGONAL, IslandType::EUCLIDEAN_SQUARED, IslandType::SQUARE_BUMP,
		IslandType::HYPERBOLOID, IslandType::SQUIRCLE, IslandType::TRIG }) {
		CAPTURE(static_cast<int>(type));
		NoiseConfigParameters config = MakeConfig();
		config.island = true;
		config.islandType = type;
		CHECK(CountOutOfRange(config, 0.0f, 1.0f) == 0);
	}
}

TEST_CASE("Redistribution is a power that keeps the sign")
{
	const NoiseConfigParameters plain = MakeConfig();
	NoiseConfigParameters squared = plain;
	squared.redistribution = 2.0f;

	SimplexNoiseClass noise;
	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		float base = noise.PointNoise(plain, static_cast<float>(x), static_cast<float>(y));
		float expected = base < 0.0f ? -base * base : base * base;
		return noise.PointNoise(squared, static_cast<float>(x), static_cast<float>(y)) != doctest::Approx(expected);
	}) == 0);
}

//A ridge gain above 1 gives negative values even with an option that removes negatives from the noise itself
TEST_CASE("Ridge with a fractional redistribution does not produce NaN")
{
	NoiseConfigParameters config = MakeConfig(Options::REVERT_NEGATIVES);
	config.Ridge = true;
	config.RidgeGain = 3.0f;
	config.redistribution = 1.5f;

	SimplexNoiseClass noise;
	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		return std::isnan(noise.PointNoise(config, static_cast<float>(x), static_cast<float>(y)));
	}) == 0);
}

TEST_CASE("Redistribution keeps the sign of negative ridge values with every option")
{
	for (Options option : { Options::REFIT_ALL, Options::FLATTEN_NEGATIVES, Options::REVERT_NEGATIVES }) {
		CAPTURE(static_cast<int>(option));
		NoiseConfigParameters plain = MakeConfig(option);
		plain.Ridge = true;
		plain.RidgeGain = 3.0f;
		NoiseConfigParameters squared = plain;
		squared.redistribution = 2.0f;

		SimplexNoiseClass noise;
		int negative = 0;
		CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
			float base = noise.PointNoise(plain, static_cast<float>(x), static_cast<float>(y));
			negative += base < 0.0f;
			float expected = base < 0.0f ? -base * base : base * base;
			return noise.PointNoise(squared, static_cast<float>(x), static_cast<float>(y)) != doctest::Approx(expected);
		}) == 0);
		CHECK(negative > 0);
	}
}

TEST_CASE("Symmetrical noise repeats every resolution cells")
{
	NoiseConfigParameters config = MakeConfig();
	config.symmetrical = true;
	config.resolution = 100;

	SimplexNoiseClass noise;
	CHECK(noise.PointNoise(config, 12.0f, 34.0f) == doctest::Approx(noise.PointNoise(config, 112.0f, 134.0f)).epsilon(0.01));
}
