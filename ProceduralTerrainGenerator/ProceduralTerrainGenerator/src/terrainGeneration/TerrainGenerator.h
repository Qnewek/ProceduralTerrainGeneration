#pragma once

#include <vector>

#include "Noise.h"
#include "BiomeGenerator.h"
#include "Biome.h"

#include "Splines/spline.h"

class TerrainGenerator
{
public:
	TerrainGenerator();
	~TerrainGenerator();

	bool initializeMap();
	bool initializeBiomeMap();

	bool setSize(int width, int height);
	void setSeed(int seed);
	bool setSeeLevel(float seeLevel);
	bool setChunkResolution(int resolution);
	void setContinentalnessNoiseConfig(noise::NoiseConfigParameters config);
	void setMountainousNoiseConfig(noise::NoiseConfigParameters config);
	void setPVNoiseConfig(noise::NoiseConfigParameters config);
	bool setSplines(std::vector<std::vector<double>> splines);
	//bool setBiomes(std::vector<Biome>& biomes);
	//bool setRanges(std::vector<std::vector<RangedLevel>>& ranges);

	float* getHeightMap();
	int* getBiomeMap();
	int getWidth(){ return width * chunkResolution; };
	int getHeight(){ return height * chunkResolution; };
	noise::NoiseConfigParameters& getContinentalnessNoiseConfig();
	noise::NoiseConfigParameters& getMountainousNoiseConfig();
	noise::NoiseConfigParameters& getPVNoiseConfig();
	//noise::NoiseConfigParameters& getTemperatureNoiseConfig();
	//noise::NoiseConfigParameters& getHumidityNoiseConfig();

	bool generateHeightMap();
	bool generateBiomes();
	bool performTerrainGeneration();

private:
	float* heightMap;
	int* biomeMap;
	int seed, width, height;
	int chunkResolution;
	float seeLevel;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousNoise;
	noise::SimplexNoiseClass PVNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousSpline;
	tk::spline PVSpline;
};