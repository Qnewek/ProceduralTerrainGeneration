#version 450 core

// Biome of every texel from its five noise values, evaluated in place, by the rules of common/biomes.glsl (= Biome.cpp):
// temperature and humidity changed by the terrain height (before the biome modifiers, so there is no loop), then the id
// is the nearest biome (first on ties) and the color mixes all biomes closer than blendWidth to the nearest one.
// With writeSplat also the share of every surface material (biome::MixMaterials: sums below + finishMaterials), 4 materials per layer of splatMap.
// With writeHeight also the terrain height, changed by the biome modifiers if modifyTerrain (as height_compute.shader computes it),
// so a world with biomes needs one pass: the noises and the biome distances are evaluated once for all outputs.
// After a change run ProceduralTerrainGenerator.exe --gpu-check.

layout(local_size_x = 32, local_size_y = 32) in;

layout(rgba32f, binding = 0) uniform writeonly image2D colorMap;
layout(r32i, binding = 1) uniform writeonly iimage2D idMap;
// Layer l holds the weights of materials 4l to 4l + 3 (MAX_MATERIALS / 4 layers)
layout(rgba8, binding = 2) uniform writeonly image2DArray splatMap;
layout(r32f, binding = 3) uniform writeonly image2D heightMap;
uniform bool writeSplat;
uniform bool writeHeight;
uniform bool modifyTerrain;		// with writeHeight: the height changed by the biome modifiers

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
	for (int i = 0; i < NOISE_COUNT; i++) {
		values[i] = evaluateNoise(i, position.x, position.y);
	}
	float height = terrainHeight(values[0], values[1], values[2]);
	applyClimate(values, height);

	float nearest;
	int id = nearestBiome(values, nearest);
	bool blendColor = blendWidth > 0.0;
	bool modify = writeHeight && modifyTerrain;

	// One pass over the biomes for every blend, each distance computed once. The sums go in the same order as in
	// modifyHeight (height_compute.shader), so the height equals that pass bit for bit (--gpu-check, section world)
	vec3 colorSum = vec3(0.0);
	float colorTotal = 0.0;
	float terrainTotal = 0.0, amplitude = 0.0, offset = 0.0, details = 0.0;
	float weights[MAX_MATERIALS];
	for (int m = 0; m < MAX_MATERIALS; m++) {
		weights[m] = 0.0;
	}
	if (blendColor || modify || writeSplat) {
		for (int b = 0; b < biomeCount; b++) {
			float distance = biomeDistance(b, values);
			// All biomes take part, so a biome passing another in distance changes the color continuously
			if (blendColor) {
				float weight = blendWeight(distance, nearest, blendWidth);
				colorSum += weight * vec3(biomes[b].r, biomes[b].g, biomes[b].b);
				colorTotal += weight;
			}
			if (modify) {
				float weight = blendWeight(distance, nearest, terrainBlendWidth);
				if (weight > 0.0) {
					terrainTotal += weight;
					amplitude += weight * biomes[b].amplitude;
					offset += weight * biomes[b].offset;
					// The noise only where it is seen
					if (biomes[b].detailAmplitude != 0.0) {
						details += weight * biomes[b].detailAmplitude * detailNoise(b, position);
					}
				}
			}
			if (writeSplat) {
				float weight = blendWeight(distance, nearest, materialBlendWidth);
				if (weight > 0.0) {
					for (int l = 0; l < MATERIAL_LAYERS; l++) {
						weights[biomes[b].materials[l]] += weight * biomes[b].materialWeights[l];
					}
				}
			}
		}
	}

	// total >= 1: the nearest biome has weight 1
	vec3 color = blendColor ? colorSum / colorTotal : vec3(biomes[id].r, biomes[id].g, biomes[id].b);
	imageStore(colorMap, texel, vec4(color, 1.0));
	imageStore(idMap, texel, ivec4(id));

	if (writeHeight) {
		if (modify) {
			height = finishModifiedHeight(height, terrainTotal, amplitude, offset, details);
		}
		imageStore(heightMap, texel, vec4(height));
	}

	if (writeSplat) {
		finishMaterials(weights, position);
		for (int l = 0; l < MAX_MATERIALS / 4; l++) {
			imageStore(splatMap, ivec3(texel, l), vec4(weights[l * 4], weights[l * 4 + 1], weights[l * 4 + 2], weights[l * 4 + 3]));
		}
	}
}
