#version 450 core

// Assigns a biome to every texel; GLSL version of BiomeGenerator::Biomify, which reads maps generated earlier.
// Here the five noises are evaluated in place; the CPU maps always start at map position (0, 0), which is origin = (0, 0) here.

layout(local_size_x = 32, local_size_y = 32) in;

layout(rgba32f, binding = 0) uniform writeonly image2D colorMap;
layout(r32i, binding = 1) uniform writeonly iimage2D idMap;

#include "../common/noise_params.glsl"

// Order of noises and levels: continentalness, mountainousness, weirdness, temperature, humidity
const int CONTINENTALNESS = 0, MOUNTAINOUSNESS = 1, WEIRDNESS = 2, TEMPERATURE = 3, HUMIDITY = 4;

// Matches GpuBiomeInput in GpuHeightGenerator.cpp
layout(std430, binding = 0) readonly buffer BiomeInput {
	NoiseParams noises[5];
	uint permutations[5 * 256];
};

layout(std430, binding = 1) readonly buffer BiomeLevels {
	float levels[];
};

// Matches GpuBiomeEntry in GpuHeightGenerator.cpp
struct BiomeEntry {
	float levelRanges[10];	// from and to (exclusive) of every parameter, order as above
	int id;
	float r, g, b;
};

layout(std430, binding = 2) readonly buffer BiomeTable {
	BiomeEntry biomes[];
};

uniform int biomeCount;
uniform ivec2 levelRanges[5];	// first entry in levels and number of entries of every parameter
uniform vec3 fallbackColor;		// biome 0, used where no biome matches
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;		// first texel of this dispatch, large maps are generated in parts
uniform vec2 origin;			// map position of texel (0, 0)
uniform float sampleStep;		// map distance between neighbouring texels (MapArea::step)
uniform ivec2 islandSize;		// see noise.glsl

#include "../common/noise.glsl"

int determineLevel(int parameter, float value)
{
	ivec2 range = levelRanges[parameter];
	for (int i = 0; i < range.y; i++) {
		if (value < levels[range.x + i]) {
			return i - 1;
		}
	}
	return -2;
}

void main()
{
	ivec2 texel = tileOrigin + ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(texel, mapSize))) {
		return;
	}

	vec2 position = vec2(texel) * sampleStep + origin;
	float parameterLevels[5];
	for (int i = 0; i < 5; i++) {
		parameterLevels[i] = float(determineLevel(i, evaluateNoise(i, position.x, position.y)));
	}

	int id = 0;
	vec3 color = fallbackColor;
	for (int b = 0; b < biomeCount; b++) {
		bool matches = true;
		for (int i = 0; i < 5 && matches; i++) {
			matches = parameterLevels[i] >= biomes[b].levelRanges[i * 2] && parameterLevels[i] < biomes[b].levelRanges[i * 2 + 1];
		}
		if (matches) {
			id = biomes[b].id;
			color = vec3(biomes[b].r, biomes[b].g, biomes[b].b);
			break;
		}
	}
	imageStore(colorMap, texel, vec4(color, 1.0));
	imageStore(idMap, texel, ivec4(id));
}
