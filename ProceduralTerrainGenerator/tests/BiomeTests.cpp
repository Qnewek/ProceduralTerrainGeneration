#include "doctest/doctest.h"

#include "Biome.h"

//Ids of the default biomes from biome::DefaultBiomes
constexpr int GRASSPLAINS = 0, DESERT = 1, SNOW = 2, SAND = 3, MOUNTAIN = 4, OCEAN = 5;

namespace {
	//Biome chosen for the levels as in biome_compute.shader: the first match in the order of the table, id 0 if none matches
	int FirstMatch(const std::vector<biome::Biome>& table, int T, int H, int C, int M, int W)
	{
		for (const biome::Biome& b : table) {
			if (b.VerifyBiome(T, H, C, M, W)) {
				return b.GetId();
			}
		}
		return 0;
	}
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
	const std::vector<biome::Biome> table = biome::DefaultBiomes();

	//Arguments: temperature, humidity, continentalness, mountainousness, weirdness
	CHECK(FirstMatch(table, 1, 1, 3, 0, 0) == GRASSPLAINS);
	CHECK(FirstMatch(table, 3, 0, 3, 0, 0) == DESERT);
	CHECK(FirstMatch(table, 0, 3, 3, 0, 0) == SNOW);
	CHECK(FirstMatch(table, 3, 3, 2, 6, 0) == SAND);
	CHECK(FirstMatch(table, 0, 0, 4, 4, 0) == MOUNTAIN);
	CHECK(FirstMatch(table, 0, 0, 0, 0, 0) == OCEAN);
	CHECK(FirstMatch(table, 3, 3, 1, 0, 0) == OCEAN);
}

TEST_CASE("Combinations without a biome fall back to id 0")
{
	//Hot and humid land - a known gap in the default table, to be redesigned with A6
	CHECK(FirstMatch(biome::DefaultBiomes(), 3, 3, 3, 0, 0) == GRASSPLAINS);
}

//With overlapping biomes the choice would depend on the order of the table (A6)
TEST_CASE("Default biomes do not overlap")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();

	int overlapping = 0;
	//Level counts of the default BiomeConfig
	for (int t = 0; t < 4; t++) {
		for (int h = 0; h < 4; h++) {
			for (int c = 0; c < 5; c++) {
				for (int m = 0; m < 7; m++) {
					int matches = 0;
					for (const biome::Biome& b : table) {
						if (b.VerifyBiome(t, h, c, m, 0))
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
