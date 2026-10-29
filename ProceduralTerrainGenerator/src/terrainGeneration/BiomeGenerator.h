#pragma once

#include <unordered_map>
#include <vector>

#include "Biome.h"
#include "Grid.h"
#include "WorldConfig.h"

//Assigns a biome to every map cell from the levels of temperature, humidity and the three terrain noises.
//Holds results (biome map, temperature/humidity maps) and the biome definitions; parameters come from BiomeConfig.
class BiomeGenerator
{
private:
	BiomeMap biomeMap;
	int height, width;
	bool isGenerated = false;

	std::unordered_map<int, biome::Biome> biomes;

	noise::SimplexNoiseClass temperatureNoise;
	noise::SimplexNoiseClass humidityNoise;
public:
	BiomeGenerator();
	~BiomeGenerator();

	bool Initialize(int _width, int _height);
	bool Resize(int _width, int _height);
	bool IsGenerated() const { return isGenerated; };
	void Regenerate() { isGenerated = false; };
	bool Biomify(const BiomeConfig& config, noise::SimplexNoiseClass& continenatlness, noise::SimplexNoiseClass& mountainousness, noise::SimplexNoiseClass& weirdness);
	int DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness, const int& weirdness);
	int DetermineLevel(const std::vector<float>& levels, float value);
	//Generates the temperature and humidity maps (origin 0, 0)
	bool GenerateComponentNoises(const BiomeConfig& config);

	bool SetBiomes(std::vector<biome::Biome>& b);

	biome::Biome& GetBiome(int id) { return biomes[id]; };
	const BiomeMap& GetBiomeMap() const { return biomeMap; };
	int GetBiomeAt(int x, int y);
	noise::SimplexNoiseClass& GetNoiseByParameter(BiomeParameter p);
};