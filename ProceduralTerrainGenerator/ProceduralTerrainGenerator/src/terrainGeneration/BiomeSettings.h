#pragma once

#include <unordered_map>
#include <vector>

#include "Biome.h"

struct RangedLevel {
	float min;
	float max;
	int level;
};

enum class WorldParameter{
	Humidity,
	Temperature,
	Continentalness,
	Mountainousness,
	Variant
};

class BiomeSettings
{
public:
	BiomeSettings();
	~BiomeSettings();

	int determineLevel(WorldParameter p, float value);
	int determineBiome(int humidity, int temperature, int continentalness, int mountainousness, int variant);
	Biome& getBiome(int id);

	bool setRanges();
	bool makeLookUpTable();
private:
	std::vector<std::vector<std::vector<std::vector<std::vector<int>>>>> m_LookUpTable;

	std::unordered_map<int, Biome> m_Biomes;
	std::vector<RangedLevel> m_ContinentalnessLevels;
	std::vector<RangedLevel> m_HumidityLevels;
	std::vector<RangedLevel> m_TemperatureLevels;
	std::vector<RangedLevel> m_MountainousnessLevels;
	std::vector<RangedLevel> m_VariantLevels;
};