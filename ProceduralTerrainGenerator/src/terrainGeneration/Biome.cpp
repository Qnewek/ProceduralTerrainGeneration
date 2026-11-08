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

	std::vector<Biome> DefaultBiomes()
	{
		//Order of ranges: continentalness, mountainousness, weirdness, temperature, humidity.
		//Converted from the discrete levels used before 2026-11-08, so the default world looks as it did
		const Range all{ -1.0f, 1.0f };
		return {
			{ "Grassplains", glm::vec3(0.2f, 0.8f, 0.2f),	{ Range{ 0.03f, 1.0f }, Range{ -1.0f, -0.2f }, all, Range{ -0.5f, 0.0f }, Range{ -0.5f, 1.0f } } },
			{ "Desert", glm::vec3(0.95f, 0.1f, 0.1f),		{ Range{ 0.03f, 1.0f }, Range{ -1.0f, 0.05f }, all, Range{ 0.0f, 1.0f }, Range{ -1.0f, -0.5f } } },
			{ "Snow", glm::vec3(0.95f, 0.95f, 0.95f),		{ Range{ 0.03f, 1.0f }, Range{ -1.0f, 0.05f }, all, Range{ -1.0f, -0.5f }, all } },
			{ "Sand", glm::vec3(0.93f, 0.82f, 0.55f),		{ Range{ -0.2f, 0.03f }, all, all, all, all } },
			{ "Mountain", glm::vec3(0.5f, 0.5f, 0.5f),		{ Range{ 0.3f, 1.0f }, Range{ 0.05f, 1.0f }, all, all, all } },
			{ "Ocean", glm::vec3(0.2f, 0.4f, 0.85f),		{ Range{ -1.0f, -0.2f }, all, all, all, all } },
		};
	}
}
