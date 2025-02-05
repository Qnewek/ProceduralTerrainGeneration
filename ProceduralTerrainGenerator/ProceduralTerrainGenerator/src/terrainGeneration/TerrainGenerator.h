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

	bool InitializeMap();
	bool InitializeBiomeMap();

	bool SetSize(int width, int height);
	void SetSeed(int seed);
	bool SetSeeLevel(float seeLevel);
	bool SetChunkResolution(int resolution);
	void SetContinentalnessNoiseConfig(noise::NoiseConfigParameters config);
	void SetMountainousNoiseConfig(noise::NoiseConfigParameters config);
	void SetPVNoiseConfig(noise::NoiseConfigParameters config);
	bool SetSplines(std::vector<std::vector<double>> splines);
	bool SetSpline(char c, std::vector<std::vector<double>>  spline);
	bool SetBiomes(std::vector<biome::Biome>& biomes);
	bool SetRanges(std::vector<std::vector<RangedLevel>>& ranges);
	bool SetRange(char c, std::vector<RangedLevel> range);

	int* GetBiomeMap();
	int GetWidth(){ return width * chunkResolution; };
	int GetHeight(){ return height * chunkResolution; };
	int GetBiomeAt(int x, int y);
	int GetTreeCount() { return treeCount; };
	float* GetHeightMap();
	float GetHeightAt(int x, int y);
	biome::Biome& GetBiome(int id);
	noise::NoiseConfigParameters& GetContinentalnessNoiseConfig();
	noise::NoiseConfigParameters& GetMountainousNoiseConfig();
	noise::NoiseConfigParameters& GetPVNoiseConfig();
	noise::NoiseConfigParameters& GetTemperatureNoiseConfig();
	noise::NoiseConfigParameters& GetHumidityNoiseConfig();
	noise::SimplexNoiseClass& GetContinentalnessNoise() { return continentalnessNoise; };
	noise::SimplexNoiseClass& GetMountainousNoise() { return mountainousNoise; };
	noise::SimplexNoiseClass& GetPVNoise() { return PVNoise; };
	noise::SimplexNoiseClass& GetTemperatureNoise() { return biomeGen.GetTemperatureNoise(); };
	noise::SimplexNoiseClass& GetHumidityNoise() { return biomeGen.GetHumidityNoise(); };
	std::vector<std::vector<std::pair<int, int>>> GetVegetationPoints() { return vegetationPoints; };

	bool GenerateHeightMap();
	bool GenerateBiomes();
	bool PerformTerrainGeneration();
	bool VegetationGeneration();
	bool GenerateBiomeMapPerChunk();

private:
	float* heightMap;
	int* biomeMap;
	int* biomeMapPerChunk;
	int seed, width, height;
	int chunkResolution;
	float seeLevel;
	int treeCount = 0;

	std::vector<std::vector<std::pair<int,int>>> vegetationPoints;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousNoise;
	noise::SimplexNoiseClass PVNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousSpline;
	tk::spline PVSpline;

	BiomeGenerator biomeGen;
};