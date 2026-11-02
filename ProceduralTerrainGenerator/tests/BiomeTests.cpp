#include "doctest/doctest.h"

#include "BiomeGenerator.h"
#include "TerrainGenerator.h"
#include "TestUtils.h"

//Ids of the default biomes from BiomeGenerator::Initialize
constexpr int GRASSPLAINS = 0, DESERT = 1, SNOW = 2, SAND = 3, MOUNTAIN = 4, OCEAN = 5;
constexpr int BIOME_COUNT = 6;

TEST_CASE("DetermineLevel finds the half-open range of a value")
{
	BiomeGenerator gen;
	const std::vector<float> levels = { -1.0f, -0.5f, 0.0f, 0.5f, 1.01f };

	CHECK(gen.DetermineLevel(levels, -1.0f) == 0);
	CHECK(gen.DetermineLevel(levels, -0.75f) == 0);
	CHECK(gen.DetermineLevel(levels, -0.5f) == 1);
	CHECK(gen.DetermineLevel(levels, 0.0f) == 2);
	CHECK(gen.DetermineLevel(levels, 0.49f) == 2);
	CHECK(gen.DetermineLevel(levels, 0.5f) == 3);
	CHECK(gen.DetermineLevel(levels, 1.0f) == 3);
}

TEST_CASE("DetermineLevel reports values outside the boundaries")
{
	BiomeGenerator gen;
	const std::vector<float> levels = { -1.0f, 0.0f, 1.01f };

	CHECK(gen.DetermineLevel(levels, -1.5f) == -1);
	CHECK(gen.DetermineLevel(levels, 1.01f) == -2);
}

TEST_CASE("VerifyBiome uses half-open ranges on every parameter")
{
	const biome::Biome b(7, "Test", { 1, 2 }, { 0, 2 }, { 3, 5 }, { 0, 1 }, { 0, 1 }, glm::vec3(0.0f), 1.0f);

	CHECK(b.VerifyBiome(1, 1, 4, 0, 0));
	CHECK_FALSE(b.VerifyBiome(2, 1, 4, 0, 0));
	CHECK_FALSE(b.VerifyBiome(1, 2, 4, 0, 0));
	CHECK_FALSE(b.VerifyBiome(1, 1, 5, 0, 0));
	CHECK_FALSE(b.VerifyBiome(1, 1, 4, 1, 0));
	CHECK_FALSE(b.VerifyBiome(1, 1, 4, 0, 1));
}

TEST_CASE("Default biomes are chosen by their parameters")
{
	BiomeGenerator gen;
	REQUIRE(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));

	//Arguments: temperature, humidity, continentalness, mountainousness, weirdness
	CHECK(gen.DetermineBiome(1, 1, 3, 0, 0) == GRASSPLAINS);
	CHECK(gen.DetermineBiome(3, 0, 3, 0, 0) == DESERT);
	CHECK(gen.DetermineBiome(0, 3, 3, 0, 0) == SNOW);
	CHECK(gen.DetermineBiome(3, 3, 2, 6, 0) == SAND);
	CHECK(gen.DetermineBiome(0, 0, 4, 4, 0) == MOUNTAIN);
	CHECK(gen.DetermineBiome(0, 0, 0, 0, 0) == OCEAN);
	CHECK(gen.DetermineBiome(3, 3, 1, 0, 0) == OCEAN);
}

TEST_CASE("Combinations without a biome fall back to id 0")
{
	BiomeGenerator gen;
	REQUIRE(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));

	//Hot and humid land - a known gap in the default table, to be redesigned with A6
	CHECK(gen.DetermineBiome(3, 3, 3, 0, 0) == GRASSPLAINS);
}

//The first match in an unordered_map wins, so overlapping biomes would make the choice depend on the hash order (A6)
TEST_CASE("Default biomes do not overlap")
{
	BiomeGenerator gen;
	REQUIRE(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));

	int overlapping = 0;
	//Level counts of the default BiomeConfig
	for (int t = 0; t < 4; t++) {
		for (int h = 0; h < 4; h++) {
			for (int c = 0; c < 5; c++) {
				for (int m = 0; m < 7; m++) {
					int matches = 0;
					for (int id = 0; id < BIOME_COUNT; id++) {
						if (gen.GetBiome(id).VerifyBiome(t, h, c, m, 0))
							matches++;
					}
					if (matches > 1)
						overlapping++;
				}
			}
		}
	}
	CHECK(overlapping == 0);
}

TEST_CASE("SetBiomes rejects an empty list")
{
	BiomeGenerator gen;
	std::vector<biome::Biome> empty;
	CHECK_FALSE(gen.SetBiomes(empty));
}

TEST_CASE("Biomify assigns every cell the biome of its levels")
{
	const TerrainConfig terrainConfig = TerrainConfig::Default();
	const BiomeConfig config = BiomeConfig::Default();

	TerrainGenerator terrain;
	BiomeGenerator gen;
	terrain.Initialize(TEST_WIDTH, TEST_HEIGHT);
	REQUIRE(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));
	REQUIRE(terrain.GenerateNoises(terrainConfig));
	REQUIRE(gen.GenerateComponentNoises(config));

	noise::SimplexNoiseClass& continentalness = terrain.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS);
	noise::SimplexNoiseClass& mountainousness = terrain.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS);
	noise::SimplexNoiseClass& weirdness = terrain.GetSelectedNoise(WorldGenParameter::WEIRDNESS);

	CHECK_FALSE(gen.IsGenerated());
	REQUIRE(gen.Biomify(config, continentalness, mountainousness, weirdness));
	CHECK(gen.IsGenerated());

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		int T = gen.DetermineLevel(config.temperatureLevels, gen.GetNoiseByParameter(BiomeParameter::TEMPERATURE).GetVal(x, y));
		int H = gen.DetermineLevel(config.humidityLevels, gen.GetNoiseByParameter(BiomeParameter::HUMIDITY).GetVal(x, y));
		int C = gen.DetermineLevel(config.continentalnessLevels, continentalness.GetVal(x, y));
		int M = gen.DetermineLevel(config.mountainousnessLevels, mountainousness.GetVal(x, y));
		int W = gen.DetermineLevel(config.weirdnessLevels, weirdness.GetVal(x, y));
		return gen.GetBiomeAt(x, y) != gen.DetermineBiome(T, H, C, M, W);
	}) == 0);

	CHECK(CountCells(TEST_WIDTH, TEST_HEIGHT, [&](int x, int y) {
		int id = gen.GetBiomeAt(x, y);
		return id < 0 || id >= BIOME_COUNT;
	}) == 0);
}

TEST_CASE("Biomify rejects noises of a different size")
{
	TerrainGenerator terrain;
	BiomeGenerator gen;
	terrain.Initialize(TEST_WIDTH, TEST_WIDTH);
	REQUIRE(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));

	CHECK_FALSE(gen.Biomify(BiomeConfig::Default(), terrain.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS),
		terrain.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS), terrain.GetSelectedNoise(WorldGenParameter::WEIRDNESS)));
	CHECK_FALSE(gen.IsGenerated());
}

TEST_CASE("GetBiomeAt rejects coordinates outside the map")
{
	BiomeGenerator gen;
	CHECK(gen.GetBiomeAt(0, 0) == -1);

	REQUIRE(gen.Initialize(TEST_WIDTH, TEST_HEIGHT));
	CHECK(gen.GetBiomeAt(TEST_WIDTH, 0) == -1);
	CHECK(gen.GetBiomeAt(0, TEST_HEIGHT) == -1);
}
