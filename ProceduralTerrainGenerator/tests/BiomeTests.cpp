#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>

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
	CHECK(biome::Nearest(table, Values(0.17f, 0.8f, 0.0f, 0.5f, 0.5f)) == SAND);
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

TEST_CASE("Modifiers: inside one biome its modifier alone changes the height")
{
	biome::Biome flat, other;
	flat.ranges[biome::TEMPERATURE] = { -1.0f, 0.0f };
	flat.terrain = { 0.5f, 0.1f, 0.2f, 16.0f, false };
	other.ranges[biome::TEMPERATURE] = { 0.5f, 1.0f };
	other.terrain = { 3.0f, -0.3f, 0.0f, 16.0f, false };
	const std::vector<biome::Biome> table = { flat, other };
	const std::vector<float> detail = { 0.5f, -1.0f };
	//T = -0.5: deep inside "flat", "other" is 1.0 away, far beyond the blend width
	const biome::ParameterValues values = Values(0.0f, 0.0f, 0.0f, -0.5f, 0.0f);
	//water + (h - water) * 0.5 + 0.1 + 0.2 * 0.5
	CHECK(biome::ModifyHeight(table, values, 0.6f, 0.2f, 0.1f, detail) == doctest::Approx(0.2f + 0.4f * 0.5f + 0.1f + 0.1f));
	//The water surface stays where it is for amplitude alone
	std::vector<biome::Biome> onlyAmplitude = table;
	onlyAmplitude[0].terrain = { 0.5f, 0.0f, 0.0f, 16.0f, false };
	CHECK(biome::ModifyHeight(onlyAmplitude, values, 0.2f, 0.2f, 0.1f, detail) == doctest::Approx(0.2f));
	//Neutral modifiers keep the height
	std::vector<biome::Biome> neutral = { biome::Biome{}, biome::Biome{} };
	CHECK(biome::ModifyHeight(neutral, values, 0.37f, 0.2f, 0.1f, detail) == doctest::Approx(0.37f));
	//Clamped to [0, 1]
	CHECK(biome::ModifyHeight(table, values, 3.0f, 0.2f, 0.1f, detail) == 1.0f);
}

TEST_CASE("Modifiers: the height changes continuously across a biome border")
{
	biome::Biome low, high;
	low.ranges[biome::TEMPERATURE] = { -1.0f, 0.0f };
	low.terrain = { 0.3f, 0.0f, 0.0f, 16.0f, false };
	high.ranges[biome::TEMPERATURE] = { 0.0f, 1.0f };
	high.terrain = { 2.0f, 0.1f, 0.0f, 16.0f, false };
	const std::vector<biome::Biome> table = { low, high };
	const std::vector<float> detail = { 0.0f, 0.0f };
	//Walking over the border at T = 0 in small steps: no step larger than a fraction of the full difference
	const float lowHeight = biome::ModifyHeight(table, Values(0.0f, 0.0f, 0.0f, -0.5f, 0.0f), 0.6f, 0.2f, 0.2f, detail);
	const float highHeight = biome::ModifyHeight(table, Values(0.0f, 0.0f, 0.0f, 0.5f, 0.0f), 0.6f, 0.2f, 0.2f, detail);
	float previous = lowHeight, largestStep = 0.0f;
	for (int i = -100; i <= 100; i++) {
		const float h = biome::ModifyHeight(table, Values(0.0f, 0.0f, 0.0f, 0.005f * i, 0.0f), 0.6f, 0.2f, 0.2f, detail);
		largestStep = std::max(largestStep, std::abs(h - previous));
		previous = h;
	}
	CHECK(highHeight > lowHeight);
	CHECK(largestStep < (highHeight - lowHeight) * 0.1f);
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

namespace {
	float Sum(const biome::MaterialWeights& weights)
	{
		float sum = 0.0f;
		for (float w : weights) {
			sum += w;
		}
		return sum;
	}

	//Two biomes split by temperature at 0: cold = material 2, warm = 70% material 5 + 30% material 9 (weights given unnormalized)
	std::vector<biome::Biome> TwoMaterialBiomes()
	{
		biome::Biome cold, warm;
		cold.ranges[biome::TEMPERATURE] = { -1.0f, 0.0f };
		cold.materials = { { { 2, 3.0f } } };
		warm.ranges[biome::TEMPERATURE] = { 0.0f, 1.0f };
		warm.materials = { { { 5, 7.0f }, { 9, 3.0f } } };
		return { cold, warm };
	}
}

TEST_CASE("Materials: a biome alone gives its normalized mix")
{
	const std::vector<biome::Biome> table = TwoMaterialBiomes();
	const biome::MaterialWeights noPatches{};

	const biome::MaterialWeights warm = biome::MixMaterials(table, Values(0.0f, 0.0f, 0.0f, 0.8f, 0.0f), 0.1f, 0.0f, 4.0f, noPatches);
	CHECK(warm[5] == doctest::Approx(0.7f));
	CHECK(warm[9] == doctest::Approx(0.3f));
	CHECK(warm[2] == 0.0f);
	CHECK(Sum(warm) == doctest::Approx(1.0f));

	const biome::MaterialWeights cold = biome::MixMaterials(table, Values(0.0f, 0.0f, 0.0f, -0.8f, 0.0f), 0.1f, 0.0f, 4.0f, noPatches);
	CHECK(cold[2] == doctest::Approx(1.0f));
	CHECK(Sum(cold) == doctest::Approx(1.0f));
}

TEST_CASE("Materials: biomes at a border mix and the mix changes continuously")
{
	const std::vector<biome::Biome> table = TwoMaterialBiomes();
	const biome::MaterialWeights noPatches{};

	//On the border both biomes are at distance 0: half of each
	const biome::MaterialWeights border = biome::MixMaterials(table, Values(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), 0.1f, 0.0f, 4.0f, noPatches);
	CHECK(border[2] == doctest::Approx(0.5f));
	CHECK(border[5] == doctest::Approx(0.35f));
	CHECK(border[9] == doctest::Approx(0.15f));

	//Small steps in temperature make small steps in the weights
	biome::MaterialWeights previous = biome::MixMaterials(table, Values(0.0f, 0.0f, 0.0f, -0.2f, 0.0f), 0.1f, 0.0f, 4.0f, noPatches);
	float largestStep = 0.0f;
	for (int i = 1; i <= 400; i++) {
		const float t = -0.2f + 0.001f * i;
		const biome::MaterialWeights w = biome::MixMaterials(table, Values(0.0f, 0.0f, 0.0f, t, 0.0f), 0.1f, 0.0f, 4.0f, noPatches);
		CHECK(Sum(w) == doctest::Approx(1.0f));
		for (int m = 0; m < biome::maxMaterials; m++) {
			largestStep = std::max(largestStep, std::abs(w[m] - previous[m]));
		}
		previous = w;
	}
	CHECK(largestStep < 0.05f);
}

TEST_CASE("Materials: patches are a softmax of log weight plus scaled noise")
{
	const std::vector<biome::Biome> table = TwoMaterialBiomes();
	const biome::ParameterValues warm = Values(0.0f, 0.0f, 0.0f, 0.8f, 0.0f);
	biome::MaterialWeights patch{};
	patch[5] = -0.2f;
	patch[9] = 0.3f;

	//Contrast 1: weights proportional to w * exp(k * patch), k = patchNoiseScale * patchiness
	const float k = biome::patchNoiseScale * 0.5f;
	const float e5 = 0.7f * std::exp(k * -0.2f), e9 = 0.3f * std::exp(k * 0.3f);
	const biome::MaterialWeights w = biome::MixMaterials(table, warm, 0.1f, 0.5f, 1.0f, patch);
	CHECK(w[5] == doctest::Approx(e5 / (e5 + e9)));
	CHECK(w[9] == doctest::Approx(e9 / (e5 + e9)));
	CHECK(w[2] == 0.0f);
	CHECK(Sum(w) == doctest::Approx(1.0f));

	//The minor material leads where its noise is high enough; contrast keeps the leader and sharpens the edge
	const biome::MaterialWeights soft = biome::MixMaterials(table, warm, 0.1f, 1.0f, 1.0f, patch);
	const biome::MaterialWeights sharp = biome::MixMaterials(table, warm, 0.1f, 1.0f, 8.0f, patch);
	CHECK(soft[9] > soft[5]);
	CHECK(sharp[9] > soft[9]);
	CHECK(sharp[9] > 0.99f);
	CHECK(Sum(sharp) == doctest::Approx(1.0f));

	//Even extreme scores stay finite (computed in the log space)
	biome::MaterialWeights extreme{};
	extreme[5] = 1.0f;
	extreme[9] = -1.0f;
	const biome::MaterialWeights e = biome::MixMaterials(table, warm, 0.1f, 1.0f, 100.0f, extreme);
	CHECK(e[5] == doctest::Approx(1.0f));
	CHECK(std::isfinite(e[9]));
}

TEST_CASE("Materials: with patches a material covers about its share of the area")
{
	//Patch values spread like the shader noise (about 1 / patchNoiseScale * 1.28 standard deviation is assumed there);
	//here a uniform spread in [-0.5, 0.5] per material, sampled on a grid, checks the order of magnitude
	const std::vector<biome::Biome> table = TwoMaterialBiomes();
	const biome::ParameterValues warm = Values(0.0f, 0.0f, 0.0f, 0.8f, 0.0f);
	int minorLeads = 0, samples = 0;
	for (int a = 0; a < 100; a++) {
		for (int b = 0; b < 100; b++) {
			biome::MaterialWeights patch{};
			patch[5] = -0.5f + (a + 0.5f) / 100.0f;
			patch[9] = -0.5f + (b + 0.5f) / 100.0f;
			const biome::MaterialWeights w = biome::MixMaterials(table, warm, 0.1f, 1.0f, 4.0f, patch);
			minorLeads += w[9] > w[5] ? 1 : 0;
			samples++;
		}
	}
	//Material 9 has 30% of the mix: it must lead on a clear part of the area, but less than the major one
	const float share = static_cast<float>(minorLeads) / samples;
	CHECK(share > 0.15f);
	CHECK(share < 0.5f);
}

TEST_CASE("Default biomes have valid materials")
{
	for (const biome::Biome& b : biome::DefaultBiomes()) {
		CAPTURE(b.name);
		float sum = 0.0f;
		for (const biome::MaterialLayer& layer : b.materials) {
			CHECK(layer.material >= 0);
			CHECK(layer.material < biome::maxMaterials);
			CHECK(layer.weight >= 0.0f);
			sum += layer.weight;
		}
		CHECK(sum > 0.0f);
	}
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
