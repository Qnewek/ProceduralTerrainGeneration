#version 450 core

// Biome of every texel from its five noise values, evaluated in place, by the rules of common/biomes.glsl (= Biome.cpp):
// temperature and humidity changed by the terrain height (before the biome modifiers, so there is no loop), then the id
// is the nearest biome (first on ties) and the color mixes all biomes closer than blendWidth to the nearest one.
// With writeSplat also the share of every surface material (mixMaterials), 4 materials per layer of splatMap.
// After a change run ProceduralTerrainGenerator.exe --gpu-check.

layout(local_size_x = 32, local_size_y = 32) in;

layout(rgba32f, binding = 0) uniform writeonly image2D colorMap;
layout(r32i, binding = 1) uniform writeonly iimage2D idMap;
// Layer l holds the weights of materials 4l to 4l + 3 (MAX_MATERIALS / 4 layers)
layout(rgba8, binding = 2) uniform writeonly image2DArray splatMap;
uniform bool writeSplat;

#include "../common/noise_params.glsl"
#include "../common/world_input.glsl"

uniform float blendWidth;		// 0 = only the nearest biome
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;		// first texel of this dispatch, large maps are generated in parts
uniform vec2 origin;			// map position of texel (0, 0)
uniform float sampleStep;		// map distance between neighbouring texels (MapArea::step)
uniform ivec2 islandSize;		// see noise.glsl
uniform int mode;				// terrain method (MODE_* of terrain.glsl), never MODE_NOISE
uniform vec3 layered;			// MODE_LAYERED: seaLevel, coastWidth, mountainShare (TerrainConfig)

#include "../common/noise.glsl"
#include "../common/terrain.glsl"
#include "../common/biomes.glsl"

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
	applyClimate(values, terrainHeight(values[0], values[1], values[2]));

	float nearest;
	int id = nearestBiome(values, nearest);

	// All biomes take part, so a biome passing another in distance changes the color continuously
	vec3 color = vec3(biomes[id].r, biomes[id].g, biomes[id].b);
	if (blendWidth > 0.0) {
		vec3 sum = vec3(0.0);
		float total = 0.0;
		for (int b = 0; b < biomeCount; b++) {
			float weight = blendWeight(biomeDistance(b, values), nearest, blendWidth);
			sum += weight * vec3(biomes[b].r, biomes[b].g, biomes[b].b);
			total += weight;
		}
		// total >= 1: the nearest biome has weight 1
		color = sum / total;
	}
	imageStore(colorMap, texel, vec4(color, 1.0));
	imageStore(idMap, texel, ivec4(id));

	if (writeSplat) {
		float weights[MAX_MATERIALS];
		mixMaterials(values, position, weights);
		for (int l = 0; l < MAX_MATERIALS / 4; l++) {
			imageStore(splatMap, ivec3(texel, l), vec4(weights[l * 4], weights[l * 4 + 1], weights[l * 4 + 2], weights[l * 4 + 3]));
		}
	}
}
