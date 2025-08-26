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
	int seed, width, height;

	//std::vector<std::vector<std::pair<int,int>>> vegetationPoints;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousnessNoise;
	noise::SimplexNoiseClass PVNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousnessSpline;
	tk::spline PVSpline;

public:
	TerrainGenerator();
	~TerrainGenerator();

	bool Initialize(int _width, int _height);
	bool Resize(int _width, int _height);
	bool GenerateTerrain();
	bool GenerateNoises();

	void SetContinentalnessNoiseConfig(noise::NoiseConfigParameters config) { continentalnessNoise.SetConfig(config); };
	void SetMountainousnessNoiseConfig(noise::NoiseConfigParameters config) { mountainousnessNoise.SetConfig(config); };
	void SetPVNoiseConfig(noise::NoiseConfigParameters config) { PVNoise.SetConfig(config); };
	bool SetSplines(std::vector<std::vector<double>> splines);
	bool SetSpline(WorldParameter p, std::vector<std::vector<double>>  spline);

	int GetWidth(){ return width; };
	int GetHeight(){ return height; };
	float* GetHeightMap() const { return heightMap; }
	float GetHeightAt(int x, int y);
	noise::NoiseConfigParameters& GetSelectedNoiseConfig(WorldParameter p);
	noise::SimplexNoiseClass& GetSelectedNoise(WorldParameter p);
};