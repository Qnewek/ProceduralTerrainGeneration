#version 450 core

// Generates a noise or terrain height map. After a change run ProceduralTerrainGenerator.exe --gpu-check.

layout(local_size_x = 32, local_size_y = 32) in;

layout(r32f, binding = 0) uniform writeonly image2D outputMap;

#include "../common/noise_params.glsl"

// Matches GpuHeightInput in GpuHeightGenerator.cpp; 1024 = SPLINE_SAMPLES of terrain.glsl
layout(std430, binding = 0) readonly buffer HeightInput {
	NoiseParams noises[3];
	uint permutations[3 * 256];
	float splines[3 * 1024];
};

uniform int mode;
uniform vec3 layered;		// MODE_LAYERED: seaLevel, coastWidth, mountainShare (TerrainConfig)
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;	// first texel of this dispatch, large maps are generated in parts
uniform vec2 origin;		// map position of texel (0, 0)
uniform float sampleStep;	// map distance between neighbouring texels (MapArea::step)
uniform ivec2 islandSize;	// see noise.glsl

#include "../common/noise.glsl"
#include "../common/terrain.glsl"

float evaluateHeight(float x, float y)
{
	if (mode == MODE_NOISE) {
		return evaluateNoise(0, x, y);
	}
	return terrainHeight(evaluateNoise(0, x, y), evaluateNoise(1, x, y), evaluateNoise(2, x, y));
}

void main()
{
	ivec2 texel = tileOrigin + ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(texel, mapSize))) {
		return;
	}
	vec2 position = vec2(texel) * sampleStep + origin;
	imageStore(outputMap, texel, vec4(evaluateHeight(position.x, position.y)));
}
