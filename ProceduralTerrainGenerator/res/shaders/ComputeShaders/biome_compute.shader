#version 450 core

// Biome of every texel from its five noise values, evaluated in place; the same rules as biome::Nearest and biome::BlendWeight (Biome.h):
// the id is the nearest biome (first on ties), the color mixes all biomes closer than blendWidth to the nearest one.
// After a change run ProceduralTerrainGenerator.exe --gpu-check.

layout(local_size_x = 32, local_size_y = 32) in;

layout(rgba32f, binding = 0) uniform writeonly image2D colorMap;
layout(r32i, binding = 1) uniform writeonly iimage2D idMap;

#include "../common/noise_params.glsl"

// Order of noises and ranges (biome::Parameter): continentalness, mountainousness, weirdness, temperature, humidity
const int PARAMETER_COUNT = 5;

// Matches GpuBiomeInput in GpuHeightGenerator.cpp
layout(std430, binding = 0) readonly buffer BiomeInput {
	NoiseParams noises[5];
	uint permutations[5 * 256];
};

// Matches GpuBiomeEntry in GpuHeightGenerator.cpp
struct BiomeEntry {
	float ranges[2 * PARAMETER_COUNT];	// min and max of every parameter, order as above
	float r, g, b;
};

layout(std430, binding = 1) readonly buffer BiomeTable {
	BiomeEntry biomes[];
};

uniform int biomeCount;
uniform float blendWidth;		// 0 = only the nearest biome
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;		// first texel of this dispatch, large maps are generated in parts
uniform vec2 origin;			// map position of texel (0, 0)
uniform float sampleStep;		// map distance between neighbouring texels (MapArea::step)
uniform ivec2 islandSize;		// see noise.glsl

#include "../common/noise.glsl"

// biome::Biome::Distance
float biomeDistance(int b, float values[PARAMETER_COUNT])
{
	float sum = 0.0;
	for (int i = 0; i < PARAMETER_COUNT; i++) {
		float outside = max(max(biomes[b].ranges[i * 2] - values[i], values[i] - biomes[b].ranges[i * 2 + 1]), 0.0);
		sum += outside * outside;
	}
	return sqrt(sum);
}

void main()
{
	ivec2 texel = tileOrigin + ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(texel, mapSize))) {
		return;
	}

	vec2 position = vec2(texel) * sampleStep + origin;
	float values[PARAMETER_COUNT];
	for (int i = 0; i < PARAMETER_COUNT; i++) {
		values[i] = evaluateNoise(i, position.x, position.y);
	}

	int id = 0;
	float nearest = biomeDistance(0, values);
	for (int b = 1; b < biomeCount; b++) {
		float distance = biomeDistance(b, values);
		if (distance < nearest) {
			nearest = distance;
			id = b;
		}
	}

	// All biomes take part, so a biome passing another in distance changes the color continuously
	vec3 color = vec3(biomes[id].r, biomes[id].g, biomes[id].b);
	if (blendWidth > 0.0) {
		vec3 sum = vec3(0.0);
		float total = 0.0;
		for (int b = 0; b < biomeCount; b++) {
			// biome::BlendWeight
			float weight = 1.0 - smoothstep(0.0, 1.0, (biomeDistance(b, values) - nearest) / blendWidth);
			sum += weight * vec3(biomes[b].r, biomes[b].g, biomes[b].b);
			total += weight;
		}
		// total >= 1: the nearest biome has weight 1
		color = sum / total;
	}
	imageStore(colorMap, texel, vec4(color, 1.0));
	imageStore(idMap, texel, ivec4(id));
}
