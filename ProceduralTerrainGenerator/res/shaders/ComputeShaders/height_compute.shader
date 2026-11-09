#version 450 core

// Generates a noise or terrain height map; the terrain optionally changed by the biome modifiers (common/biomes.glsl).
// After a change run ProceduralTerrainGenerator.exe --gpu-check.

layout(local_size_x = 32, local_size_y = 32) in;

layout(r32f, binding = 0) uniform writeonly image2D outputMap;

#include "../common/noise_params.glsl"
#include "../common/world_input.glsl"

uniform int mode;
uniform vec3 layered;		// MODE_LAYERED: seaLevel, coastWidth, mountainShare (TerrainConfig)
uniform int modifyTerrain;	// 1 = the biome table is bound and its modifiers change the terrain
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;	// first texel of this dispatch, large maps are generated in parts
uniform vec2 origin;		// map position of texel (0, 0)
uniform float sampleStep;	// map distance between neighbouring texels (MapArea::step)
uniform ivec2 islandSize;	// see noise.glsl

#include "../common/noise.glsl"
#include "../common/terrain.glsl"
#include "../common/biomes.glsl"

float evaluateHeight(vec2 position)
{
	if (mode == MODE_NOISE) {
		return evaluateNoise(0, position.x, position.y);
	}
	if (modifyTerrain == 0) {
		return terrainHeight(evaluateNoise(0, position.x, position.y), evaluateNoise(1, position.x, position.y), evaluateNoise(2, position.x, position.y));
	}
	// As in biome_compute.shader: biomes chosen from the height before the modifiers
	float values[PARAMETER_COUNT];
	for (int i = 0; i < PARAMETER_COUNT; i++) {
		values[i] = evaluateNoise(i, position.x, position.y);
	}
	float height = terrainHeight(values[0], values[1], values[2]);
	applyClimate(values, height);
	return modifyHeight(values, height, position);
}

void main()
{
	ivec2 texel = tileOrigin + ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(texel, mapSize))) {
		return;
	}
	vec2 position = vec2(texel) * sampleStep + origin;
	imageStore(outputMap, texel, vec4(evaluateHeight(position)));
}
