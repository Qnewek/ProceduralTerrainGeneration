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
	Humidity,
	Temperature,
	Continentalness,
	Mountainousness,
	Variant
};

class BiomeGenerator
{
public:
	BiomeGenerator();
	~BiomeGenerator();

	biome::Biome& GetBiome(int id);
	noise::NoiseConfigParameters& GetTemperatureNoiseConfig();
	noise::NoiseConfigParameters& GetHumidityNoiseConfig();
	noise::SimplexNoiseClass& GetTemperatureNoise() { return temperatureNoise; };
	noise::SimplexNoiseClass& GetHumidityNoise() { return humidityNoise; };

	bool SetRanges(std::vector<std::vector<RangedLevel>>& ranges);
	bool SetRange(char c, std::vector<RangedLevel> range);
	bool SetBiomes(std::vector<biome::Biome>& b);

	int DetermineLevel(WorldParameter p, float value);
	int DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness);
	bool Biomify(float* map, int* biomeMap, const int& width, const int& height, const int& chunkRes, const int& seed, const noise::SimplexNoiseClass& continenatlnes, const noise::SimplexNoiseClass& mountainouss);

private:
	std::unordered_map<int, biome::Biome> biomes;
	std::vector<RangedLevel> continentalnessLevels;
	std::vector<RangedLevel> humidityLevels;
	std::vector<RangedLevel> temperatureLevels;
	std::vector<RangedLevel> mountainousnessLevels;

	noise::SimplexNoiseClass temperatureNoise;
	noise::SimplexNoiseClass humidityNoise;
};