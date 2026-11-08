#version 450 core

// Generates a noise or terrain height map. After a change run ProceduralTerrainGenerator.exe --gpu-check.

layout(local_size_x = 32, local_size_y = 32) in;

layout(r32f, binding = 0) uniform writeonly image2D outputMap;

#include "../common/noise_params.glsl"

const int SPLINE_SAMPLES = 1024;

// Matches GpuHeightInput in GpuHeightGenerator.cpp
layout(std430, binding = 0) readonly buffer HeightInput {
	NoiseParams noises[3];
	uint permutations[3 * 256];
	float splines[3 * SPLINE_SAMPLES];
};

const int MODE_NOISE = 0, MODE_LINEAR = 1, MODE_SPLINE = 2, MODE_ZERO = 3;

uniform int mode;
uniform ivec2 mapSize;
uniform ivec2 tileOrigin;	// first texel of this dispatch, large maps are generated in parts
uniform vec2 origin;		// map position of texel (0, 0)
uniform float sampleStep;	// map distance between neighbouring texels (MapArea::step)
uniform ivec2 islandSize;	// see noise.glsl

#include "../common/noise.glsl"

// A value outside [-1, 1] takes the end of the table, while tk::spline on the CPU extrapolates
float evaluateSpline(int index, float value)
{
	float position = (clamp(value, -1.0, 1.0) + 1.0) * 0.5 * float(SPLINE_SAMPLES - 1);
	int sampleIndex = min(int(position), SPLINE_SAMPLES - 2);
	int first = index * SPLINE_SAMPLES + sampleIndex;
	return mix(splines[first], splines[first + 1], position - float(sampleIndex));
}

float evaluateHeight(float x, float y)
{
	if (mode == MODE_NOISE) {
		return evaluateNoise(0, x, y);
	}
	if (mode == MODE_ZERO) {
		return 0.0;
	}
	float continentalness = evaluateNoise(0, x, y);
	float mountainousness = evaluateNoise(1, x, y);
	float weirdness = evaluateNoise(2, x, y);
	if (mode == MODE_LINEAR) {
		return (continentalness + 1.0) / 2.0 * ((mountainousness + 1.0) / 2.0) * (1.0 - (weirdness + 1.0) / 2.0);
	}
	return evaluateSpline(0, continentalness) * evaluateSpline(1, mountainousness) * evaluateSpline(2, weirdness);
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
