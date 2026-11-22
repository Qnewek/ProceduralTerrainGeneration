#version 450 core

// Biomes of a map that was not generated (Imported map, GpuHeightGenerator::GenerateMapBiomes). The CPU reference: mapbiomes::ClimateHeight
// (MapBiomes.cpp) for the height the climate sees, then biome::ApplyClimate / Nearest / BlendWeight / MixMaterials (the rules of
// common/biomes.glsl, shared with biome_compute.shader). --gpu-check, section mapbiomes, compares both.
// Continentalness and mountainousness come from the map (climateMap, mapbiomes::ComputeClimate), weirdness is 0, temperature and humidity
// are the noises of the biome config at positionOrigin + texel * texelMetres (metres). No terrain modifiers, no rivers.
// Painted biomes (biomepaint, stage D2) go over the automatic ones by their strength s: color = mix(automatic, painted, s), materials = the
// normalized automatic mix and the normalized layers of the painted biome mixed by s (biomepaint::PaintedMaterials) before the patches,
// id = painted from s >= 128 / 255. A slot whose name is not in the table (paintBiomes = -1) stays automatic.

layout(local_size_x = 32, local_size_y = 32) in;

layout(rgba32f, binding = 0) uniform writeonly image2D colorMap;
layout(r32i, binding = 1) uniform writeonly iimage2D idMap;
// Layer l holds the weights of materials 4l to 4l + 3 (MAX_MATERIALS / 4 layers)
layout(rgba8, binding = 2) uniform writeonly image2DArray splatMap;
layout(r32f, binding = 3) uniform readonly image2D heights;		// map units
layout(rg16f, binding = 4) uniform readonly image2D climateMap;	// continentalness, mountainousness
layout(rg8ui, binding = 5) uniform readonly uimage2D paintMap;	// slot of the palette, strength 0-255
uniform int paintBiomes[256];	// biome of each slot, -1 = automatic
uniform ivec2 regionEnd;		// texels from here on are not generated (a pass over the region of a brush)
uniform bool writeSplat;

#include "../common/noise_params.glsl"
#include "../common/world_input.glsl"

uniform float blendWidth;		// 0 = only the nearest biome
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;		// first texel of this dispatch, large maps are generated in parts
uniform ivec2 islandSize;		// see noise.glsl
uniform float metresPerUnit;	// metres of map height 1
uniform bool hasSea;
uniform float seaLevel;			// metres
uniform float heightUnit;		// mapbiomes::Settings::heightUnitMetres
uniform float baseMetres;
uniform vec2 positionOrigin;	// noise position of texel (0, 0), metres
uniform vec2 texelMetres;
uniform vec2 patchOrigin;		// positionOrigin / texelMetres: material patches are measured in texels

#include "../common/noise.glsl"
#include "../common/biomes.glsl"

// mapbiomes::ClimateHeight: the flat sea of a map of the real world gets deeper away from the coast
float climateHeight(float metres, float continentalness)
{
	if (hasSea && metres <= seaLevel && metres >= seaLevel - 1.0) {
		return waterHeight * (1.0 - min(abs(continentalness), 1.0));
	}
	return (metres - baseMetres) / heightUnit;
}

// biomepaint::PaintedMaterials; a painted biome without materials leaves the automatic ones
void paintMaterials(inout float weights[MAX_MATERIALS], int biome, float strength)
{
	float painted[MAX_MATERIALS];
	float automaticSum = 0.0, layerSum = 0.0;
	for (int m = 0; m < MAX_MATERIALS; m++) {
		painted[m] = 0.0;
		automaticSum += weights[m];
	}
	for (int l = 0; l < MATERIAL_LAYERS; l++) {
		painted[biomes[biome].materials[l]] += biomes[biome].materialWeights[l];
	}
	for (int m = 0; m < MAX_MATERIALS; m++) {
		layerSum += painted[m];
	}
	if (layerSum <= 0.0) {
		strength = 0.0;
	}
	for (int m = 0; m < MAX_MATERIALS; m++) {
		float a = automaticSum > 0.0 ? weights[m] / automaticSum : 0.0;
		float p = layerSum > 0.0 ? painted[m] / layerSum : 0.0;
		weights[m] = a + (p - a) * strength;
	}
}

void main()
{
	ivec2 texel = tileOrigin + ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(texel, mapSize)) || any(greaterThanEqual(texel, regionEnd))) {
		return;
	}

	vec2 position = positionOrigin + vec2(texel) * texelMetres;
	vec2 cm = imageLoad(climateMap, texel).rg;
	float values[PARAMETER_COUNT];
	values[0] = cm.x;
	values[1] = cm.y;
	values[2] = 0.0;
	values[3] = evaluateNoise(3, position.x, position.y);
	values[4] = evaluateNoise(4, position.x, position.y);
	values[ELEVATION] = 0.0;
	float metres = imageLoad(heights, texel).r * metresPerUnit;
	applyClimate(values, climateHeight(metres, cm.x));

	float nearest;
	int id = nearestBiome(values, nearest);
	bool blendColor = blendWidth > 0.0;

	vec3 colorSum = vec3(0.0);
	float colorTotal = 0.0;
	float weights[MAX_MATERIALS];
	for (int m = 0; m < MAX_MATERIALS; m++) {
		weights[m] = 0.0;
	}
	if (blendColor || writeSplat) {
		for (int b = 0; b < biomeCount; b++) {
			float distance = biomeDistance(b, values);
			if (blendColor) {
				float weight = blendWeight(distance, nearest, blendWidth);
				colorSum += weight * vec3(biomes[b].r, biomes[b].g, biomes[b].b);
				colorTotal += weight;
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

	uvec2 paint = imageLoad(paintMap, texel).rg;
	int painted = paint.x > 0u ? paintBiomes[paint.x] : -1;
	if (painted >= 0 && paint.y > 0u) {
		float strength = float(paint.y) / 255.0;
		color = color + (vec3(biomes[painted].r, biomes[painted].g, biomes[painted].b) - color) * strength;
		if (paint.y >= 128u) {
			id = painted;
		}
		if (writeSplat) {
			paintMaterials(weights, painted, strength);
		}
	}

	imageStore(colorMap, texel, vec4(color, 1.0));
	imageStore(idMap, texel, ivec4(id));

	if (writeSplat) {
		// Patches in texels like in TerrainGen (a map sent from it, 1 m texels, gets the same patches); in metres they would shrink
		// below a pixel on a DEM of 30 m pixels
		finishMaterials(weights, patchOrigin + vec2(texel));
		for (int l = 0; l < MAX_MATERIALS / 4; l++) {
			imageStore(splatMap, ivec3(texel, l), vec4(weights[l * 4], weights[l * 4 + 1], weights[l * 4 + 2], weights[l * 4 + 3]));
		}
	}
}
