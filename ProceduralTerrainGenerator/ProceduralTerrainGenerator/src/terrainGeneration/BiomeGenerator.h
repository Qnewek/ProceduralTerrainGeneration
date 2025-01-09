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

	biome::Biome& getBiome(int id);
	noise::NoiseConfigParameters& getTemperatureNoiseConfig();
	noise::NoiseConfigParameters& getHumidityNoiseConfig();

	bool setRanges(std::vector<std::vector<RangedLevel>>& ranges);
	bool setBiomes(std::vector<biome::Biome>& biomes);

	int determineLevel(WorldParameter p, float value);
	int determineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness);
	bool biomify(float* map, int* biomeMap, const int& width, const int& height, const int& chunkRes, const int& seed, const noise::SimplexNoiseClass& continenatlnes, const noise::SimplexNoiseClass& mountainouss);

private:
	std::unordered_map<int, biome::Biome> m_Biomes;
	std::vector<RangedLevel> m_ContinentalnessLevels;
	std::vector<RangedLevel> m_HumidityLevels;
	std::vector<RangedLevel> m_TemperatureLevels;
	std::vector<RangedLevel> m_MountainousnessLevels;

	noise::SimplexNoiseClass temperatureNoise;
	noise::SimplexNoiseClass humidityNoise;
};