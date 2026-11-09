#pragma once

#include <vector>

#include "Biome.h"
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
	//Base land shape from continentalness with mountains on top: amplitude from mountainousness, shape from weirdness
	//read as peaks and valleys (see TerrainConfig::seaLevel)
	LAYERED
};

//Control points of a spline mapping a noise value [-1, 1] to a factor [0, 1]
struct SplinePoints {
	std::vector<double> x, y;
	bool cubic = false;		//cubic spline if true, linear interpolation otherwise
};

struct TerrainConfig {
	noise::NoiseConfigParameters continentalness, mountainousness, weirdness;
	SplinePoints continentalnessSpline, mountainousnessSpline, weirdnessSpline;
	EvaluationMethod method = EvaluationMethod::LAYERED;

	//LAYERED: h = base * (1 - mountainShare) + land * amplitude * peaks * mountainShare, in [0, 1], where
	//base = continentalnessSpline(C), amplitude = mountainousnessSpline(M), peaks = peaksSpline(W),
	//land = smoothstep(seaLevel, seaLevel + coastWidth, base): mountains only grow inland from the coast.
	//seaLevel is a value of the base spline; the water surface lies at height seaLevel * (1 - mountainShare).
	//Weirdness with Ridge (gain 3, offset 1) is the fold 1 - |3|W| - 2| = peaks and valleys of Minecraft 1.18.
	float seaLevel = 0.3f;
	float coastWidth = 0.15f;
	float mountainShare = 0.6f;
	SplinePoints peaksSpline{ {-1.0, -0.5, 0.0, 0.5, 1.0}, {0.0, 0.05, 0.25, 0.7, 1.0} };

	//Height of the water surface the climate of biomes is measured from: seaLevel * (1 - mountainShare) for LAYERED;
	//the other methods have no sea, so heights are measured from 0
	float WaterHeight() const { return method == EvaluationMethod::LAYERED ? seaLevel * (1.0f - mountainShare) : 0.0f; }

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

	//Spline the current method uses for a component: LAYERED reads weirdness through peaksSpline
	SplinePoints& MethodSpline(WorldGenParameter p) {
		return method == EvaluationMethod::LAYERED && p == WorldGenParameter::WEIRDNESS ? peaksSpline : Spline(p);
	}
	const SplinePoints& MethodSpline(WorldGenParameter p) const { return const_cast<TerrainConfig*>(this)->MethodSpline(p); }

	//Default terrain used at startup and by --gpu-check
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

namespace erosion {
	//Configuration parameters for the erosion
	//@param erosionRate: The rate at which the droplet erodes the terrain
	//@param depositionRate: The rate at which the droplet deposits sediment
	//@param evaporationRate: The rate at which the droplet Evaporates
	//@param gravity: The force of gravity on the droplet
	//@param inertia: The inertia of the droplet
	//@param minSlope: The minimum slope for the droplet to move
	//@param erosionRadius: The radius of the droplet
	//@param blur: The blur of the droplet
	//@param dropletLifetime: The lifetime of the droplet
	//@param dropletCount: The number of simulated droplets
	//@param initialWater: The initial amount of water in the droplet
	//@param initialVelocity: The initial velocity of the droplet
	//@param initialCapacity: The initial capacity of the droplet
	struct ErosionConfig {
		//Erosion parameters
		float erosionRate = 0.6f;
		float depositionRate = 0.5f;
		float evaporationRate = 0.01f;

		float gravity = 1.0f;
		float inertia = 0.1f;

		float minSlope = 0.0f;
		int erosionRadius = 3;
		float blur = 0.0f;

		int dropletLifetime = 64;
		int dropletCount = 20000;

		float initialWater = 1.0f;
		float initialVelocity = 1.0f;
		float initialCapacity = 1.0f;
	};
}

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

	//Biomes as ranges of the five noise values (see Biome.h); never empty. A biome id is its index here
	std::vector<biome::Biome> table = biome::DefaultBiomes();
	//Distance in the parameter space over which a biome fades into the nearest one; 0 = sharp borders
	float blendWidth = 0.08f;
	//Temperature and humidity changed by the terrain height before choosing biomes (see biome::ApplyClimate)
	biome::Climate climate;

	//Noise of a biome-only parameter (TEMPERATURE or HUMIDITY; anything else returns temperature)
	noise::NoiseConfigParameters& Noise(BiomeParameter p) { return p == BiomeParameter::HUMIDITY ? humidity : temperature; }
	const noise::NoiseConfigParameters& Noise(BiomeParameter p) const { return p == BiomeParameter::HUMIDITY ? humidity : temperature; }

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
		return c;
	}
};

//Everything the "TerrainGen" mode generates from
struct WorldConfig {
	int width = 0, height = 0;
	TerrainConfig terrain = TerrainConfig::Default();
	BiomeConfig biomes = BiomeConfig::Default();
};

//Everything the "Noise heightmap" mode generates from
struct NoiseMapConfig {
	int width = 0, height = 0;
	noise::NoiseConfigParameters noise;
	erosion::ErosionConfig erosion;
};
