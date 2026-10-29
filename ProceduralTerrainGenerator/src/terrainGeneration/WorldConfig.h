#pragma once

#include <vector>

#include "Noise.h"

//Plain data describing the generated world. The UI edits it, generators only read it (passed by const reference),
//so a copy of it is everything a generation job needs.

//Component noises of the terrain
enum class WorldGenParameter {
	CONTINENTALNESS,
	MOUNTAINOUSNESS,
	WEIRDNESS
};

//How the component noises are combined into elevation
enum class EvaluationMethod {
	LINEAR_COMBINE,
	SPLINE_COMBINE,
	C
};

//Control points of a spline mapping a noise value [-1, 1] to a factor [0, 1]
struct SplinePoints {
	std::vector<double> x, y;
	bool cubic = false;		//cubic spline if true, linear interpolation otherwise
};

struct TerrainConfig {
	noise::NoiseConfigParameters continentalness, mountainousness, weirdness;
	SplinePoints continentalnessSpline, mountainousnessSpline, weirdnessSpline;
	EvaluationMethod method = EvaluationMethod::LINEAR_COMBINE;

	noise::NoiseConfigParameters& Noise(WorldGenParameter p) {
		switch (p) {
		case WorldGenParameter::MOUNTAINOUSNESS: return mountainousness;
		case WorldGenParameter::WEIRDNESS: return weirdness;
		case WorldGenParameter::CONTINENTALNESS:
		default: return continentalness;
		}
	}
	const noise::NoiseConfigParameters& Noise(WorldGenParameter p) const { return const_cast<TerrainConfig*>(this)->Noise(p); }

	SplinePoints& Spline(WorldGenParameter p) {
		switch (p) {
		case WorldGenParameter::MOUNTAINOUSNESS: return mountainousnessSpline;
		case WorldGenParameter::WEIRDNESS: return weirdnessSpline;
		case WorldGenParameter::CONTINENTALNESS:
		default: return continentalnessSpline;
		}
	}
	const SplinePoints& Spline(WorldGenParameter p) const { return const_cast<TerrainConfig*>(this)->Spline(p); }

	//Default terrain used at startup and by the benchmark
	static TerrainConfig Default() {
		TerrainConfig c;
		c.continentalness.seed = 623;
		c.continentalness.constrast = 1.5f;
		c.continentalness.octaves = 7;
		c.continentalness.scale = 0.5f;
		c.continentalness.option = noise::Options::NOTHING;

		c.mountainousness.seed = 262;
		c.mountainousness.constrast = 1.5f;
		c.mountainousness.scale = 0.3f;
		c.mountainousness.option = noise::Options::NOTHING;

		c.weirdness.seed = 192;
		c.weirdness.constrast = 1.5f;
		c.weirdness.RidgeGain = 3.0f;
		c.weirdness.scale = 0.2f;
		c.weirdness.option = noise::Options::NOTHING;
		c.weirdness.Ridge = true;

		c.continentalnessSpline = { {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 0.1, 0.3, 0.70, 0.95, 1.0} };
		c.mountainousnessSpline = { {-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 0.1, 0.2, 0.3, 0.3, 0.6, 0.6, 1.0} };
		c.weirdnessSpline = { {-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} };
		return c;
	}
};
