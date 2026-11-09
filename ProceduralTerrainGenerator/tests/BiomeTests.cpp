#include "doctest/doctest.h"

#include <algorithm>

#include "Biome.h"

//Ids (indices) of the default biomes from biome::DefaultBiomes
constexpr int GRASSPLAINS = 0, DESERT = 1, SNOW = 2, SAND = 3, SNOWY_PEAKS = 4, MOUNTAIN = 5, OCEAN = 6;

namespace {
	//Values in the order of biome::Parameter
	biome::ParameterValues Values(float c, float m, float w, float t, float h)
	{
		return { c, m, w, t, h };
	}
}

TEST_CASE("Distance is 0 inside the ranges and grows with how far a value lies outside")
{
	biome::Biome b;
	b.ranges[biome::CONTINENTALNESS] = { 0.0f, 0.5f };
	b.ranges[biome::TEMPERATURE] = { -0.5f, 0.0f };

	CHECK(b.Distance(Values(0.25f, 0.0f, 0.0f, -0.25f, 0.0f)) == 0.0f);
	//On the border of a range
	CHECK(b.Distance(Values(0.5f, 0.0f, 0.0f, -0.5f, 0.0f)) == 0.0f);
	//Below and above a range
	CHECK(b.Distance(Values(-0.3f, 0.0f, 0.0f, -0.25f, 0.0f)) == doctest::Approx(0.3f));
	CHECK(b.Distance(Values(0.25f, 0.0f, 0.0f, 0.4f, 0.0f)) == doctest::Approx(0.4f));
	//Outside two ranges: Euclidean length
	CHECK(b.Distance(Values(0.8f, 0.0f, 0.0f, 0.4f, 0.0f)) == doctest::Approx(0.5f));
}

TEST_CASE("Default biomes are chosen by their parameters")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();

	//Arguments: continentalness, mountainousness, weirdness, temperature, humidity
	CHECK(biome::Nearest(table, Values(0.5f, -0.5f, 0.0f, -0.25f, 0.0f)) == GRASSPLAINS);
	CHECK(biome::Nearest(table, Values(0.5f, -0.5f, 0.0f, 0.5f, -0.8f)) == DESERT);
	CHECK(biome::Nearest(table, Values(0.5f, -0.5f, 0.0f, -0.8f, 0.5f)) == SNOW);
	CHECK(biome::Nearest(table, Values(-0.1f, 0.8f, 0.0f, 0.5f, 0.5f)) == SAND);
	CHECK(biome::Nearest(table, Values(0.6f, 0.6f, 0.0f, 0.0f, 0.0f)) == MOUNTAIN);
	CHECK(biome::Nearest(table, Values(0.6f, 0.6f, 0.0f, -0.7f, 0.0f)) == SNOWY_PEAKS);
	CHECK(biome::Nearest(table, Values(-0.9f, 0.0f, 0.0f, 0.0f, 0.0f)) == OCEAN);
}

TEST_CASE("Climate: temperature falls with height above the water, humidity rises at the coast")
{
	const biome::Climate climate{ 1.0f, 0.3f, 0.1f };
	const biome::ParameterValues values = Values(0.1f, 0.2f, 0.3f, 0.4f, -0.2f);
	const float water = 0.2f;

	//At and below the water surface: temperature kept, full coastal humidity
	for (float height : { 0.0f, water }) {
		const biome::ParameterValues v = biome::ApplyClimate(values, height, water, climate);
		CHECK(v[biome::TEMPERATURE] == doctest::Approx(0.4f));
		CHECK(v[biome::HUMIDITY] == doctest::Approx(0.1f));
	}
	//Halfway through the coast range: smoothstep gives half of the humidity
	biome::ParameterValues v = biome::ApplyClimate(values, water + 0.05f, water, climate);
	CHECK(v[biome::TEMPERATURE] == doctest::Approx(0.35f));
	CHECK(v[biome::HUMIDITY] == doctest::Approx(-0.05f));
	//Inland and high: no added humidity, temperature down by lapseRate * height above the water
	v = biome::ApplyClimate(values, water + 0.5f, water, climate);
	CHECK(v[biome::TEMPERATURE] == doctest::Approx(-0.1f));
	CHECK(v[biome::HUMIDITY] == doctest::Approx(-0.2f));
	//The terrain noises are kept
	CHECK(v[biome::CONTINENTALNESS] == 0.1f);
	CHECK(v[biome::MOUNTAINOUSNESS] == 0.2f);
	CHECK(v[biome::WEIRDNESS] == 0.3f);
	//Clamped to the noise range
	v = biome::ApplyClimate(Values(0.0f, 0.0f, 0.0f, -0.9f, 0.9f), 0.0f, 0.5f, biome::Climate{ 5.0f, 0.5f, 0.1f });
	CHECK(v[biome::HUMIDITY] == 1.0f);
	v = biome::ApplyClimate(Values(0.0f, 0.0f, 0.0f, -0.9f, 0.9f), 1.0f, 0.0f, biome::Climate{ 5.0f, 0.5f, 0.1f });
	CHECK(v[biome::TEMPERATURE] == -1.0f);
}

TEST_CASE("Climate: high mountains of the default table get snow")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();
	const biome::Climate climate;
	//A mountain in a mild climate: grey low down, snowy high up
	const biome::ParameterValues mountain = Values(0.6f, 0.6f, 0.0f, 0.0f, 0.0f);
	CHECK(biome::Nearest(table, biome::ApplyClimate(mountain, 0.3f, 0.1f, climate)) == MOUNTAIN);
	CHECK(biome::Nearest(table, biome::ApplyClimate(mountain, 0.9f, 0.1f, climate)) == SNOWY_PEAKS);
}

TEST_CASE("Every point gets the nearest biome, also where no biome contains it")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();
	//Hot and humid flat land lies in no default biome; grassplains are nearest (temperature 0.3 above its range)
	const biome::ParameterValues values = Values(0.5f, -0.5f, 0.0f, 0.3f, 0.2f);
	for (const biome::Biome& b : table) {
		CHECK(b.Distance(values) > 0.0f);
	}
	CHECK(biome::Nearest(table, values) == GRASSPLAINS);
}

TEST_CASE("On equal distances the first biome of the table wins")
{
	biome::Biome low, high;
	low.ranges[biome::TEMPERATURE] = { -1.0f, -0.5f };
	high.ranges[biome::TEMPERATURE] = { 0.5f, 1.0f };
	const biome::ParameterValues middle = Values(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	CHECK(biome::Nearest({ low, high }, middle) == 0);
	CHECK(biome::Nearest({ high, low }, middle) == 0);
}

TEST_CASE("Blend weight falls smoothly from 1 at the nearest biome to 0 at the blend width")
{
	CHECK(biome::BlendWeight(0.3f, 0.3f, 0.1f) == 1.0f);
	CHECK(biome::BlendWeight(0.35f, 0.3f, 0.1f) == doctest::Approx(0.5f));
	CHECK(biome::BlendWeight(0.4f, 0.3f, 0.1f) == 0.0f);
	CHECK(biome::BlendWeight(0.9f, 0.3f, 0.1f) == 0.0f);
	//Weights only fall with distance
	float previous = 1.0f;
	for (int i = 1; i <= 20; i++) {
		const float weight = biome::BlendWeight(0.3f + 0.005f * i, 0.3f, 0.1f);
		CHECK(weight <= previous);
		previous = weight;
	}
	//Width 0: only the nearest biome
	CHECK(biome::BlendWeight(0.3f, 0.3f, 0.0f) == 1.0f);
	CHECK(biome::BlendWeight(0.31f, 0.3f, 0.0f) == 0.0f);
}

//Overlapping biomes would share an area; there the first one of the table always wins and the other only blends in
TEST_CASE("Default biomes do not overlap")
{
	const std::vector<biome::Biome> table = biome::DefaultBiomes();
	int overlapping = 0;
	for (size_t a = 0; a < table.size(); a++) {
		for (size_t b = a + 1; b < table.size(); b++) {
			bool overlap = true;
			for (int p = 0; p < biome::PARAMETER_COUNT && overlap; p++) {
				//Shared borders do not count
				overlap = std::min(table[a].ranges[p].max, table[b].ranges[p].max) > std::max(table[a].ranges[p].min, table[b].ranges[p].min);
			}
			if (overlap) {
				CAPTURE(table[a].name);
				CAPTURE(table[b].name);
				overlapping++;
			}
		}
	}
	CHECK(overlapping == 0);
}
