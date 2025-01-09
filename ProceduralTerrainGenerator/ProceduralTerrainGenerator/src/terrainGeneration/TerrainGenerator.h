#pragma once

#include <vector>
#include <utility>

#include "Noise.h"
#include "BiomeGenerator.h"
#include "Player.h"

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
	bool setBiomes(std::vector<biome::Biome>& biomes);
	bool setRanges(std::vector<std::vector<RangedLevel>>& ranges);

	float* getHeightMap();
	int* getBiomeMap();
	int getWidth(){ return width * chunkResolution; };
	int getHeight(){ return height * chunkResolution; };
	float getHeightAt(int x, int y);
	biome::Biome& getBiome(int id);
	int getBiomeAt(int x, int y);
	int getTreeCount() { return treeCount; };
	noise::NoiseConfigParameters& getContinentalnessNoiseConfig();
	noise::NoiseConfigParameters& getMountainousNoiseConfig();
	noise::NoiseConfigParameters& getPVNoiseConfig();
	noise::NoiseConfigParameters& getTemperatureNoiseConfig();
	noise::NoiseConfigParameters& getHumidityNoiseConfig();
	std::vector<std::vector<std::pair<int, int>>> getVegetationPoints() { return vegetationPoints; };

	bool generateHeightMap();
	bool generateBiomes();
	bool performTerrainGeneration();
	bool vegetationGeneration();
	bool generateBiomeMapPerChunk();

private:
	float* heightMap;
	int* biomeMap;
	int* biomeMapPerChunk;
	int seed, width, height;
	int chunkResolution;
	float seeLevel;
	int treeCount;

	std::vector<std::vector<std::pair<int,int>>> vegetationPoints;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousNoise;
	noise::SimplexNoiseClass PVNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousSpline;
	tk::spline PVSpline;

	BiomeGenerator biomeGen;
};