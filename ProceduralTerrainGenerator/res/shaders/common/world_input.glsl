// Input of the generating shaders, matches GpuWorldInput in GpuHeightGenerator.cpp.
// Noises in the order of biome::Parameter: continentalness, mountainousness, weirdness, temperature, humidity
// (a single noise map uses only the first). Permutation table 5 belongs to the detail noise of the biome modifiers.
// Splines: the three terrain splines sampled to 1024 values each (SPLINE_SAMPLES of terrain.glsl).
// Needs noise_params.glsl included first.

layout(std430, binding = 0) readonly buffer WorldInput {
	NoiseParams noises[5];
	uint permutations[6 * 256];
	float splines[3 * 1024];
};
