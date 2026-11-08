#pragma once

#include <array>
#include <string>
#include <vector>

#include "glm/glm.hpp"

//Biomes as regions in the space of the five parameter noises, as the multi-noise biomes of Minecraft 1.18:
//every point belongs to the nearest biome, and biomes closer than the blend width to the nearest one mix with it.
//biome_compute.shader does the same on the GPU; the functions here describe it and are used by tests.
namespace biome {
	//Parameters a biome is described by, in the order of noises and ranges in biome_compute.shader
	enum Parameter { CONTINENTALNESS, MOUNTAINOUSNESS, WEIRDNESS, TEMPERATURE, HUMIDITY, PARAMETER_COUNT };

	//Range of a noise value (noises are in [-1, 1]); min <= max
	struct Range {
		float min = -1.0f, max = 1.0f;
	};

	using ParameterValues = std::array<float, PARAMETER_COUNT>;

	struct Biome {
		std::string name;
		glm::vec3 color = glm::vec3(0.0f);
		std::array<Range, PARAMETER_COUNT> ranges;

		//Distance in the parameter space from the ranges of the biome: 0 inside all of them, otherwise
		//the Euclidean length of how far each value lies outside its range
		float Distance(const ParameterValues& values) const;
	};

	//Index of the nearest biome, the first one on ties; the table must not be empty. Biome ids are indices in the table
	int Nearest(const std::vector<Biome>& table, const ParameterValues& values);

	//Weight of a biome distance more than the nearest one: 1 at the nearest, falling smoothly to 0 at blendWidth;
	//blendWidth <= 0 = only the nearest biome
	float BlendWeight(float distance, float nearestDistance, float blendWidth);

	//Biomes used by default
	std::vector<Biome> DefaultBiomes();
}
