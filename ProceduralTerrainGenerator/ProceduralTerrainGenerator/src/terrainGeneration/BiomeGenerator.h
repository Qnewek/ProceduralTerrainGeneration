#pragma once

#include <unordered_map>
#include <vector>

#include "Biome.h"

struct RangedLevel {
	float min;
	float max;
	int level;
};

enum class WorldParameter {
	CONTINENTALNESS,
	MOUNTAINOUSNESS,
	PV,
	HUMIDITY,
	TEMPERATURE
};

class BiomeGenerator
{
public:
	BiomeGenerator();
	~BiomeGenerator();

	bool Biomify(float* map, int* biomeMap, const int& width, const int& height, const int& seed, const noise::SimplexNoiseClass& continenatlnes, const noise::SimplexNoiseClass& mountainouss);
	int DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness);
	int DetermineLevel(WorldParameter p, float value);
	void GenerateTemperatureNoise(int width, int height, int seed);
	void GenerateHumidityNoise(int width, int height, int seed);

	bool SetRanges(std::vector<std::vector<RangedLevel>>& ranges);
	bool SetRange(char c, std::vector<RangedLevel> range);
	bool SetBiomes(std::vector<biome::Biome>& b);

	biome::Biome& GetBiome(int id) { return biomes[id]; };
	noise::NoiseConfigParameters& GetTemperatureNoiseConfig() { return temperatureNoise.GetConfigRef(); };
	noise::NoiseConfigParameters& GetHumidityNoiseConfig() { return humidityNoise.GetConfigRef(); };
	noise::SimplexNoiseClass& GetTemperatureNoise() { return temperatureNoise; };
	noise::SimplexNoiseClass& GetHumidityNoise() { return humidityNoise; };

private:
	std::unordered_map<int, biome::Biome> biomes;
	std::vector<RangedLevel> continentalnessLevels;
	std::vector<RangedLevel> humidityLevels;
	std::vector<RangedLevel> temperatureLevels;
	std::vector<RangedLevel> mountainousnessLevels;

	noise::SimplexNoiseClass temperatureNoise;
	noise::SimplexNoiseClass humidityNoise;
};