#include "Biome.h"

#include <algorithm>
#include <cmath>

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
		return {
			{ "Grassplains", glm::vec3(0.2f, 0.8f, 0.2f),	{ Range{ 0.03f, 1.0f }, Range{ -1.0f, -0.2f }, all, Range{ -0.5f, 0.0f }, Range{ -0.5f, 1.0f } }, plains },
			{ "Desert", glm::vec3(0.95f, 0.1f, 0.1f),		{ Range{ 0.03f, 1.0f }, Range{ -1.0f, 0.05f }, all, Range{ 0.0f, 1.0f }, Range{ -1.0f, -0.5f } }, dunes },
			{ "Snow", glm::vec3(0.95f, 0.95f, 0.95f),		{ Range{ 0.03f, 1.0f }, Range{ -1.0f, 0.05f }, all, Range{ -1.0f, -0.5f }, all }, snowfield },
			{ "Sand", glm::vec3(0.93f, 0.82f, 0.55f),		{ Range{ -0.2f, 0.03f }, all, all, all, all }, none },
			{ "Snowy peaks", glm::vec3(0.85f, 0.9f, 1.0f),	{ Range{ 0.3f, 1.0f }, Range{ 0.05f, 1.0f }, all, Range{ -1.0f, -0.5f }, all }, rock },
			{ "Mountain", glm::vec3(0.5f, 0.5f, 0.5f),		{ Range{ 0.3f, 1.0f }, Range{ 0.05f, 1.0f }, all, Range{ -0.5f, 1.0f }, all }, rock },
			{ "Ocean", glm::vec3(0.2f, 0.4f, 0.85f),		{ Range{ -1.0f, -0.2f }, all, all, all, all }, none },
		};
	}
}
