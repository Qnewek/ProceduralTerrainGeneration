// Input of the generating shaders, matches GpuWorldInput in GpuHeightGenerator.cpp.
// Noises in the order of biome::Parameter: continentalness, mountainousness, weirdness, temperature, humidity
// (a single noise map uses only the first). Permutation table 5 belongs to the detail noise of the biome modifiers,
// table 6 to the river noise (rivers.glsl).
// Splines: the three terrain splines sampled to 1024 values each (SPLINE_SAMPLES of terrain.glsl).
// Nested curves (MODE_NESTED of terrain.glsl): the tree of nested::Flatten (NestedSpline.h), curve 0 = the root over continentalness,
// at most 81 curves of at most 16 knots.
// Needs noise_params.glsl included first.

struct NestedCurve {
	int firstKnot;
	int count;
	int cubic;
	int pad;
};

struct NestedKnot {
	float x;
	float value;
	int child;		// curve of this knot, -1 = the constant value
	int pad;
};

layout(std430, binding = 0) readonly buffer WorldInput {
	NoiseParams noises[5];
	uint permutations[7 * 256];
	float splines[3 * 1024];
	NestedCurve nestedCurves[81];
	NestedKnot nestedKnots[81 * 16];
};
