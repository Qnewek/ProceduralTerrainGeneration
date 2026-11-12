#include "Biome.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace biome {
	float Biome::Distance(const ParameterValues& values) const
	{
		float sum = 0.0f;
		for (int i = 0; i < PARAMETER_COUNT; i++) {
			const float outside = std::max({ ranges[i].min - values[i], values[i] - ranges[i].max, 0.0f });
			sum += outside * outside;
		}
		return std::sqrt(sum);
	}

	int Nearest(const std::vector<Biome>& table, const ParameterValues& values)
	{
		int nearest = 0;
		float best = table[0].Distance(values);
		for (int i = 1; i < static_cast<int>(table.size()); i++) {
			const float distance = table[i].Distance(values);
			if (distance < best) {
				best = distance;
				nearest = i;
			}
		}
		return nearest;
	}

	float BlendWeight(float distance, float nearestDistance, float blendWidth)
	{
		if (blendWidth <= 0.0f) {
			return distance <= nearestDistance ? 1.0f : 0.0f;
		}
		const float t = std::clamp((distance - nearestDistance) / blendWidth, 0.0f, 1.0f);
		//1 - smoothstep(0, 1, t)
		return 1.0f - t * t * (3.0f - 2.0f * t);
	}

	ParameterValues ApplyClimate(ParameterValues values, float height, float waterHeight, const Climate& climate)
	{
		const float above = std::max(height - waterHeight, 0.0f);
		const float t = std::clamp(above / climate.coastRange, 0.0f, 1.0f);
		const float coast = 1.0f - t * t * (3.0f - 2.0f * t);
		values[TEMPERATURE] = std::clamp(values[TEMPERATURE] - climate.lapseRate * above, -1.0f, 1.0f);
		values[HUMIDITY] = std::clamp(values[HUMIDITY] + climate.coastHumidity * coast, -1.0f, 1.0f);
		values[ELEVATION] = Elevation(height, waterHeight);
		return values;
	}

	float Elevation(float height, float waterHeight)
	{
		const float relative = height - waterHeight;
		const float side = relative >= 0.0f ? 1.0f - waterHeight : waterHeight;
		return std::clamp(relative / std::max(side, 1e-6f), -1.0f, 1.0f);
	}

	float ModifyHeight(const std::vector<Biome>& table, const ParameterValues& values, float height, float waterHeight, float blendWidth,
		const std::vector<float>& detail)
	{
		const float nearest = table[Nearest(table, values)].Distance(values);
		float total = 0.0f, amplitude = 0.0f, offset = 0.0f, details = 0.0f;
		for (size_t b = 0; b < table.size(); b++) {
			const float weight = BlendWeight(table[b].Distance(values), nearest, blendWidth);
			const TerrainModifier& m = table[b].terrain;
			total += weight;
			amplitude += weight * m.amplitude;
			offset += weight * m.offset;
			details += weight * m.detailAmplitude * detail[b];
		}
		//total >= 1: the nearest biome has weight 1
		const float modified = waterHeight + (height - waterHeight) * amplitude / total + (offset + details) / total;
		return std::clamp(modified, 0.0f, 1.0f);
	}

	MaterialWeights MixMaterials(const std::vector<Biome>& table, const ParameterValues& values, float blendWidth, float patchiness,
		float contrast, const MaterialWeights& patch)
	{
		const float nearest = table[Nearest(table, values)].Distance(values);
		MaterialWeights mixed{};
		for (const Biome& b : table) {
			const float weight = BlendWeight(b.Distance(values), nearest, blendWidth);
			float layerSum = 0.0f;
			for (const MaterialLayer& layer : b.materials) {
				layerSum += layer.weight;
			}
			if (weight <= 0.0f || layerSum <= 0.0f) {
				continue;
			}
			for (const MaterialLayer& layer : b.materials) {
				mixed[layer.material] += weight * layer.weight / layerSum;
			}
		}
		//Softmax in the log space: the largest score is subtracted, so exp cannot overflow
		const float k = patchNoiseScale * patchiness;
		const float c = 1.0f + (contrast - 1.0f) * patchiness;
		MaterialWeights score{};
		float best = -std::numeric_limits<float>::infinity();
		for (int m = 0; m < maxMaterials; m++) {
			if (mixed[m] > 0.0f) {
				score[m] = c * (std::log(mixed[m]) + k * patch[m]);
				best = std::max(best, score[m]);
			}
		}
		MaterialWeights result{};
		float sum = 0.0f;
		for (int m = 0; m < maxMaterials; m++) {
			if (mixed[m] > 0.0f) {
				result[m] = std::exp(score[m] - best);
				sum += result[m];
			}
		}
		//sum >= 1: the best material has exp(0)
		for (float& w : result) {
			w /= sum;
		}
		return result;
	}

	std::vector<Biome> DefaultBiomes()
	{
		//Table of 2026-11-13: the ranges split the whole parameter space (no point lies outside every biome, no two biomes overlap),
		//thresholds calibrated on the default world (shares of the land in docs/ROADMAP.md). Water, shore, hills and mountains are
		//chosen by the elevation, so the table does not depend on the continentalness spline or the sea level; continentalness is unused
		const Range all{ -1.0f, 1.0f };
		//Elevation: deep sea below seaFloor, shore up to shoreTop, mountains from highland (on rough terrain)
		const float seaFloor = -0.4f, shoreTop = 0.022f, highland = 0.22f;
		//Temperature (after the lapse rate) and humidity bands; hot lands have their own humidity borders
		const float frozen = -0.78f, cold = -0.42f, hot = 0.25f;
		const float dry = -0.45f, wet = 0.2f, hotDry = -0.3f, hotWet = 0.3f;
		//Mountainousness: rough terrain (hills, mountains) and a rocky shore; weirdness: jagged peaks
		const float rough = 0.05f, rockyShore = 0.3f, jagged = 0.6f;
		//Ranges with continentalness unused: mountainousness, weirdness, temperature, humidity, elevation
		const auto ranges = [&](Range m, Range w, Range t, Range h, Range e) { return std::array<Range, PARAMETER_COUNT>{ all, m, w, t, h, e }; };
		const Range shore{ 0.0f, shoreTop }, inland{ shoreTop, 1.0f }, smooth{ -1.0f, rough }, hilly{ rough, 1.0f };
		//Terrain modifiers (2026-11-13): every biome its own relief, in heights of [0, 1] (x heightScale = world units). Neighbouring
		//land biomes keep similar amplitudes (0.85 - 1), since a difference makes a ramp at their border; the character comes from the
		//detail noise, whose slope grows with detailAmplitude / detailSize: flat plains, long steppe swells, snow drifts, tundra hummocks,
		//rolling forest, knobby taiga, rugged jungle, dunes, hills, rough mountains and jagged peaks. Amplitude <= 1 on high land,
		//so tops are not clipped at 1. Mountain and Snowy peaks share one modifier, so the snow line does not change the shape
		const TerrainModifier deepSea{ 1.0f, 0.0f, 0.006f, 128.0f, false };
		const TerrainModifier shelf{ 0.85f, 0.0f, 0.002f, 64.0f, false };
		const TerrainModifier beach{ 0.6f, 0.0f, 0.0f, 16.0f, false };
		const TerrainModifier stony{ 1.0f, 0.0f, 0.01f, 12.0f, true };
		const TerrainModifier snowyShore{ 0.7f, 0.0f, 0.002f, 16.0f, false };
		const TerrainModifier plains{ 0.85f, 0.0f, 0.001f, 128.0f, false };
		const TerrainModifier swells{ 0.85f, 0.0f, 0.004f, 200.0f, false };
		const TerrainModifier savanna{ 0.85f, 0.0f, 0.003f, 96.0f, false };
		const TerrainModifier tundra{ 0.85f, 0.0f, 0.003f, 20.0f, false };
		const TerrainModifier drifts{ 0.85f, 0.0f, 0.006f, 80.0f, false };
		const TerrainModifier forest{ 0.9f, 0.0f, 0.008f, 48.0f, false };
		const TerrainModifier taiga{ 0.9f, 0.0f, 0.008f, 28.0f, false };
		const TerrainModifier jungle{ 1.0f, 0.0f, 0.014f, 24.0f, false };
		const TerrainModifier dunes{ 0.85f, 0.0f, 0.012f, 14.0f, true };
		const TerrainModifier hills{ 0.95f, 0.0f, 0.01f, 96.0f, false };
		const TerrainModifier rock{ 1.0f, 0.0f, 0.03f, 40.0f, true };
		const TerrainModifier jaggedRock{ 1.0f, 0.0f, 0.045f, 20.0f, true };
		//Materials, indices of the default material list. With the patches a weight is about the share of the area;
		//the deep sea floor is dark while there is no water surface, so the sea does not look like a beach
		enum { GRASS, DRY_GRASS, FOREST_FLOOR, DIRT, SAND, BEACH, CLIFF_ROCK, SANDSTONE, SCREE, SNOW, TUNDRA };
		using Layers = std::array<MaterialLayer, materialLayers>;
		return {
			{ "Deep ocean", glm::vec3(0.1f, 0.2f, 0.55f), ranges(all, all, all, all, { -1.0f, seaFloor }), deepSea,
				Layers{ { { DIRT, 0.6f }, { SCREE, 0.4f } } } },
			{ "Ocean", glm::vec3(0.2f, 0.4f, 0.85f), ranges(all, all, all, all, { seaFloor, 0.0f }), shelf,
				Layers{ { { SAND, 0.5f }, { BEACH, 0.3f }, { DIRT, 0.2f } } } },
			{ "Beach", glm::vec3(0.93f, 0.82f, 0.55f), ranges({ -1.0f, rockyShore }, all, { frozen, 1.0f }, all, shore), beach,
				Layers{ { { BEACH, 0.7f }, { SAND, 0.3f } } } },
			{ "Stony shore", glm::vec3(0.55f, 0.55f, 0.5f), ranges({ rockyShore, 1.0f }, all, { frozen, 1.0f }, all, shore), stony,
				Layers{ { { SCREE, 0.5f }, { CLIFF_ROCK, 0.3f }, { BEACH, 0.2f } } } },
			{ "Snowy shore", glm::vec3(0.8f, 0.85f, 0.9f), ranges(all, all, { -1.0f, frozen }, all, shore), snowyShore,
				Layers{ { { SNOW, 0.5f }, { SCREE, 0.3f }, { BEACH, 0.2f } } } },
			{ "Tundra", glm::vec3(0.55f, 0.6f, 0.45f), ranges(smooth, all, { -1.0f, frozen }, { -1.0f, wet }, inland), tundra,
				Layers{ { { TUNDRA, 0.6f }, { DRY_GRASS, 0.2f }, { SCREE, 0.2f } } } },
			{ "Snowfields", glm::vec3(0.95f, 0.95f, 0.95f), ranges(smooth, all, { -1.0f, frozen }, { wet, 1.0f }, inland), drifts,
				Layers{ { { SNOW, 0.6f }, { TUNDRA, 0.4f } } } },
			{ "Taiga", glm::vec3(0.15f, 0.4f, 0.3f), ranges(smooth, all, { frozen, cold }, { dry, 1.0f }, inland), taiga,
				Layers{ { { FOREST_FLOOR, 0.55f }, { GRASS, 0.25f }, { TUNDRA, 0.2f } } } },
			{ "Steppe", glm::vec3(0.75f, 0.7f, 0.4f), ranges(smooth, all, { frozen, hot }, { -1.0f, dry }, inland), swells,
				Layers{ { { DRY_GRASS, 0.6f }, { DIRT, 0.25f }, { GRASS, 0.15f } } } },
			{ "Plains", glm::vec3(0.2f, 0.8f, 0.2f), ranges(smooth, all, { cold, hot }, { dry, wet }, inland), plains,
				Layers{ { { GRASS, 0.6f }, { DRY_GRASS, 0.2f }, { FOREST_FLOOR, 0.12f }, { DIRT, 0.08f } } } },
			{ "Forest", glm::vec3(0.1f, 0.55f, 0.15f), ranges(smooth, all, { cold, hot }, { wet, 1.0f }, inland), forest,
				Layers{ { { FOREST_FLOOR, 0.6f }, { GRASS, 0.3f }, { DIRT, 0.1f } } } },
			{ "Desert", glm::vec3(0.95f, 0.1f, 0.1f), ranges(smooth, all, { hot, 1.0f }, { -1.0f, hotDry }, inland), dunes,
				Layers{ { { SAND, 0.75f }, { SANDSTONE, 0.25f } } } },
			{ "Savanna", glm::vec3(0.8f, 0.6f, 0.2f), ranges(smooth, all, { hot, 1.0f }, { hotDry, hotWet }, inland), savanna,
				Layers{ { { DRY_GRASS, 0.6f }, { DIRT, 0.2f }, { SAND, 0.1f }, { GRASS, 0.1f } } } },
			{ "Jungle", glm::vec3(0.0f, 0.35f, 0.1f), ranges(smooth, all, { hot, 1.0f }, { hotWet, 1.0f }, inland), jungle,
				Layers{ { { GRASS, 0.5f }, { FOREST_FLOOR, 0.5f } } } },
			{ "Hills", glm::vec3(0.45f, 0.65f, 0.3f), ranges(hilly, all, { frozen, 1.0f }, all, { shoreTop, highland }), hills,
				Layers{ { { GRASS, 0.45f }, { DRY_GRASS, 0.2f }, { SCREE, 0.2f }, { DIRT, 0.15f } } } },
			{ "Mountain", glm::vec3(0.5f, 0.5f, 0.5f), ranges(hilly, { -1.0f, jagged }, { frozen, 1.0f }, all, { highland, 1.0f }), rock,
				Layers{ { { CLIFF_ROCK, 0.5f }, { SCREE, 0.3f }, { DRY_GRASS, 0.2f } } } },
			{ "Rocky peaks", glm::vec3(0.35f, 0.33f, 0.32f), ranges(hilly, { jagged, 1.0f }, { frozen, 1.0f }, all, { highland, 1.0f }), jaggedRock,
				Layers{ { { CLIFF_ROCK, 0.7f }, { SCREE, 0.3f } } } },
			{ "Snowy peaks", glm::vec3(0.85f, 0.9f, 1.0f), ranges(hilly, all, { -1.0f, frozen }, all, inland), rock,
				Layers{ { { SNOW, 0.6f }, { CLIFF_ROCK, 0.3f }, { SCREE, 0.1f } } } },
		};
	}
}
