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
		return values;
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
		//Order of ranges: continentalness, mountainousness, weirdness, temperature, humidity.
		//Converted from the discrete levels used before 2026-11-08; Snowy peaks added with the climate (2026-11-09):
		//mountains cold enough after the lapse rate
		const Range all{ -1.0f, 1.0f };
		//Terrain modifiers (2026-11-09): flatter plains and snowfields, dunes in the desert, sharper and rougher mountains;
		//both mountain biomes alike, so the snow line does not change the shape
		const TerrainModifier plains{ 0.8f, 0.0f, 0.0f, 16.0f, false };
		const TerrainModifier snowfield{ 0.9f, 0.0f, 0.0f, 16.0f, false };
		const TerrainModifier dunes{ 1.0f, 0.0f, 0.012f, 10.0f, true };
		const TerrainModifier rock{ 1.15f, 0.0f, 0.015f, 20.0f, true };
		const TerrainModifier none;
		//Materials (2026-11-10), indices of the default material list. With the patches a weight is about the share of the area;
		//the sea floor is dark while there is no water surface, so the sea does not look like a beach
		enum { GRASS, DRY_GRASS, FOREST_FLOOR, DIRT, SAND, BEACH, CLIFF_ROCK, SANDSTONE, SCREE, SNOW, TUNDRA };
		using Layers = std::array<MaterialLayer, materialLayers>;
		const Layers meadow{ { { GRASS, 0.6f }, { DRY_GRASS, 0.2f }, { FOREST_FLOOR, 0.12f }, { DIRT, 0.08f } } };
		const Layers desert{ { { SAND, 0.75f }, { SANDSTONE, 0.25f } } };
		const Layers snowfields{ { { SNOW, 0.6f }, { TUNDRA, 0.4f } } };
		const Layers beach{ { { BEACH, 0.7f }, { SAND, 0.3f } } };
		const Layers peaks{ { { SNOW, 0.6f }, { CLIFF_ROCK, 0.3f }, { SCREE, 0.1f } } };
		const Layers mountain{ { { CLIFF_ROCK, 0.5f }, { SCREE, 0.3f }, { DRY_GRASS, 0.2f } } };
		const Layers seabed{ { { SCREE, 0.5f }, { DIRT, 0.5f } } };
		return {
			{ "Grassplains", glm::vec3(0.2f, 0.8f, 0.2f),	{ Range{ 0.03f, 1.0f }, Range{ -1.0f, -0.2f }, all, Range{ -0.5f, 0.0f }, Range{ -0.5f, 1.0f } }, plains, meadow },
			{ "Desert", glm::vec3(0.95f, 0.1f, 0.1f),		{ Range{ 0.03f, 1.0f }, Range{ -1.0f, 0.05f }, all, Range{ 0.0f, 1.0f }, Range{ -1.0f, -0.5f } }, dunes, desert },
			{ "Snow", glm::vec3(0.95f, 0.95f, 0.95f),		{ Range{ 0.03f, 1.0f }, Range{ -1.0f, 0.05f }, all, Range{ -1.0f, -0.5f }, all }, snowfield, snowfields },
			{ "Sand", glm::vec3(0.93f, 0.82f, 0.55f),		{ Range{ -0.2f, 0.03f }, all, all, all, all }, none, beach },
			{ "Snowy peaks", glm::vec3(0.85f, 0.9f, 1.0f),	{ Range{ 0.3f, 1.0f }, Range{ 0.05f, 1.0f }, all, Range{ -1.0f, -0.5f }, all }, rock, peaks },
			{ "Mountain", glm::vec3(0.5f, 0.5f, 0.5f),		{ Range{ 0.3f, 1.0f }, Range{ 0.05f, 1.0f }, all, Range{ -0.5f, 1.0f }, all }, rock, mountain },
			{ "Ocean", glm::vec3(0.2f, 0.4f, 0.85f),		{ Range{ -1.0f, -0.2f }, all, all, all, all }, none, seabed },
		};
	}
}
