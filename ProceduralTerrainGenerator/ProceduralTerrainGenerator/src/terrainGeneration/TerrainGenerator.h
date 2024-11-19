#pragma once

#include <vector>

#include "Noise.h"

#include "Splines/spline.h"

class TerrainGenerator
{
public:
	TerrainGenerator();
	~TerrainGenerator();

	bool initializeMap();

	bool setSize(int width, int height);
	void setSeed(int seed);
	bool setChunkResolution(int resolution);
	bool setChunkScalingFactor(float scalingFactor);
	void setContinentalnessNoiseConfig(noise::NoiseConfigParameters config);
	void setMountainousNoiseConfig(noise::NoiseConfigParameters config);
	void setPVNoiseConfig(noise::NoiseConfigParameters config);
	bool setSplines(std::vector<std::vector<double>> splines);

	float* getHeightMap();
	float getScalingFactor();
	noise::NoiseConfigParameters& getContinentalnessNoiseConfig();
	noise::NoiseConfigParameters& getMountainousNoiseConfig();
	noise::NoiseConfigParameters& getPVNoiseConfig();

	bool generateHeightMap();

private:
	float* heightMap;
	int seed;
	int width, height;
	int chunkResolution;
	float chunkScalingFactor;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousNoise;
	noise::SimplexNoiseClass PVNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousSpline;
	tk::spline PVSpline;
};