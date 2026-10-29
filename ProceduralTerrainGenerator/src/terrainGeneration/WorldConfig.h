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

//Parameters a biome is chosen by
enum class BiomeParameter {
	CONTINENTALNESS,
	MOUNTAINOUSNESS,
	WEIRDNESS,
	HUMIDITY,
	TEMPERATURE
};

struct BiomeConfig {
	noise::NoiseConfigParameters temperature, humidity;

	//Ascending boundaries splitting a parameter value into integer levels: level i covers [levels[i], levels[i + 1]).
	//The first boundary is the fixed lower bound -1, the last one is just above the maximal value.
	std::vector<float> temperatureLevels, humidityLevels, continentalnessLevels, mountainousnessLevels, weirdnessLevels;

	//Noise of a biome-only parameter (TEMPERATURE or HUMIDITY; anything else returns temperature)
	noise::NoiseConfigParameters& Noise(BiomeParameter p) { return p == BiomeParameter::HUMIDITY ? humidity : temperature; }
	const noise::NoiseConfigParameters& Noise(BiomeParameter p) const { return p == BiomeParameter::HUMIDITY ? humidity : temperature; }

	std::vector<float>& Levels(BiomeParameter p) {
		switch (p) {
		case BiomeParameter::HUMIDITY: return humidityLevels;
		case BiomeParameter::CONTINENTALNESS: return continentalnessLevels;
		case BiomeParameter::MOUNTAINOUSNESS: return mountainousnessLevels;
		case BiomeParameter::WEIRDNESS: return weirdnessLevels;
		case BiomeParameter::TEMPERATURE:
		default: return temperatureLevels;
		}
	}
	const std::vector<float>& Levels(BiomeParameter p) const { return const_cast<BiomeConfig*>(this)->Levels(p); }

	//Default biome parameters used at startup
	static BiomeConfig Default() {
		BiomeConfig c;
		c.temperature.seed = 123;
		c.temperature.option = noise::Options::NOTHING;
		c.temperature.scale = 0.5f;
		c.temperature.constrast = 1.5f;

		c.humidity.octaves = 4;
		c.humidity.seed = 321;
		c.humidity.option = noise::Options::NOTHING;
		c.humidity.scale = 0.3f;
		c.humidity.constrast = 1.5f;

		c.temperatureLevels = { -1.0f, -0.5f, 0.0f, 0.5f, 1.01f };
		c.humidityLevels = { -1.0f, -0.5f, 0.0f, 0.5f, 1.01f };
		c.continentalnessLevels = { -1.0f, -0.7f, -0.2f, 0.03f, 0.3f, 1.01f };
		c.mountainousnessLevels = { -1.0f, -0.78f, -0.37f, -0.2f, 0.05f, 0.45f, 0.55f, 1.01f };
		c.weirdnessLevels = { -1.0f, 1.1f };
		return c;
	}
};
