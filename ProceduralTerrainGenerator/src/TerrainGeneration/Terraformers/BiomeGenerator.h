#pragma once

#include <unordered_map>
#include <vector>

#include "TerrainObjects/Biome.h"

enum class BiomeParameter {
	CONTINENTALNESS,
	MOUNTAINOUSNESS,
	WEIRDNESS,
	HUMIDITY,
	TEMPERATURE
};

class BiomeGenerator
{
private:
	std::unordered_map<int, biome::Biome> biomes;
	std::vector<std::vector<float>> biomesLevels;

	noise::SimplexNoiseClass temperatureNoise;
	noise::SimplexNoiseClass humidityNoise;
public:
	BiomeGenerator();
	~BiomeGenerator();

	bool Initialize(int _height, int _width);
	int GetVal(float x, float y, float continentalness, float mountainousness, float weirdness);
	int DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness, const int& weirdness);
	int DetermineLevel(BiomeParameter p, float value);
	bool GenerateComponentNoises();

	bool SetRanges(std::vector<std::vector<float>>& ranges);
	bool SetRange(BiomeParameter p, std::vector<float> range);
	bool SetBiomes(std::vector<biome::Biome>& b);

	biome::Biome& GetBiome(int id) { return biomes[id]; };
	int* GetBiomeMap() const { return biomeMap; };
	int GetBiomeAt(int x, int y);
	noise::NoiseConfigParameters& GetTemperatureNoiseConfig() { return temperatureNoise.GetConfigRef(); };
	noise::NoiseConfigParameters& GetHumidityNoiseConfig() { return humidityNoise.GetConfigRef(); };
	noise::SimplexNoiseClass& GetNoiseByParameter(BiomeParameter p);
	std::vector<float>& GetLevelsByParameter(BiomeParameter p);
};