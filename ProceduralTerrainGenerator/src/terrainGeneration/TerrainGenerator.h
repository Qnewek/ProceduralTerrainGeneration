#pragma once

#include <vector>
#include <utility>
#include <iostream>

#include "Noise.h"
#include "WorldConfig.h"

//Combines the three component noises of a TerrainConfig into a height map.
//Holds only results (the height map and the component noise maps), never the configuration.
class TerrainGenerator
{
private:
	HeightField heightMap;
	int width, height;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousnessNoise;
	noise::SimplexNoiseClass weirdnessNoise;

public:
	TerrainGenerator();
	~TerrainGenerator();

	bool Initialize(int _width, int _height);
	//Reallocates the maps; component noise maps have to be regenerated afterwards (GenerateNoises)
	bool Resize(int _width, int _height);
	bool GenerateTerrain(const TerrainConfig& config, float originx, float originy);
	//Generates the component noise maps (origin 0, 0) used by biomes and the noise preview
	bool GenerateNoises(const TerrainConfig& config);

	int GetWidth(){ return width; };
	int GetHeight(){ return height; };
	float* GetHeightMap() { return heightMap.Data(); }
	const HeightField& GetHeightField() const { return heightMap; }
	float GetHeightAt(int x, int y);
	noise::SimplexNoiseClass& GetSelectedNoise(WorldGenParameter p);
};