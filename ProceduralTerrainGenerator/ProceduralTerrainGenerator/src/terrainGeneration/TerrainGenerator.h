#pragma once

#include <vector>
#include <utility>

#include "Noise.h"
#include "BiomeGenerator.h"

#include "Splines/spline.h"

class TerrainGenerator
{
private:
	float* heightMap;
	float seeLevel;
	int* biomeMap;
	int seed, width, height, treeCount = 0;

	std::vector<std::vector<std::pair<int,int>>> vegetationPoints;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousnessNoise;
	noise::SimplexNoiseClass PVNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousnessSpline;
	tk::spline PVSpline;

	BiomeGenerator biomeGen;
public:
	TerrainGenerator();
	~TerrainGenerator();

	bool InitMap();
	bool InitBiomeMap();
	bool GenerateTerrain();
	bool GenerateHeightMap();
	bool GenerateBiomes();
	bool GenerateVegetation();

	bool SetSize(int _width, int _height);
	void SetSeed(int _seed) { this->seed = _seed; };
	bool SetSeeLevel(float seeLevel);
	void SetContinentalnessNoiseConfig(noise::NoiseConfigParameters config) { continentalnessNoise.SetConfig(config); };
	void SetMountainousnessNoiseConfig(noise::NoiseConfigParameters config) { mountainousnessNoise.SetConfig(config); };
	void SetPVNoiseConfig(noise::NoiseConfigParameters config) { PVNoise.SetConfig(config); };
	bool SetSplines(std::vector<std::vector<double>> splines);
	bool SetSpline(WorldParameter p, std::vector<std::vector<double>>  spline);
	bool SetBiomes(std::vector<biome::Biome>& biomes);
	bool SetRanges(std::vector<std::vector<RangedLevel>>& ranges);
	bool SetRange(char c, std::vector<RangedLevel> range) { return biomeGen.SetRange(c, range); };

	int* GetBiomeMap() { return biomeMap; };
	int GetWidth(){ return width; };
	int GetHeight(){ return height; };
	int GetBiomeAt(int x, int y);
	int GetTreeCount() { return treeCount; };
	float* GetHeightMap() { return heightMap; };
	float GetHeightAt(int x, int y);
	biome::Biome& GetBiome(int id);
	noise::NoiseConfigParameters& GetSelectedNoiseConfig(WorldParameter p);
	noise::SimplexNoiseClass& GetSelectedNoise(WorldParameter p);
	std::vector<std::vector<std::pair<int, int>>> GetVegetationPoints() { return vegetationPoints; };

};