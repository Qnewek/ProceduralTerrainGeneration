#pragma once

#include <unordered_map>
#include <vector>

#include "Biome.h"
#include "Grid.h"

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
	BiomeMap biomeMap;
	int height, width;
	bool isGenerated = false;

	std::unordered_map<int, biome::Biome> biomes;
	std::vector<std::vector<float>> biomesLevels;

	noise::SimplexNoiseClass temperatureNoise;
	noise::SimplexNoiseClass humidityNoise;
	noise::NoiseConfigParameters temperatureConfig;
	noise::NoiseConfigParameters humidityConfig;
public:
	BiomeGenerator();
	~BiomeGenerator();

	bool Initialize(int _width, int _height);
	bool Resize(int _width, int _height);
	bool IsGenerated() const { return isGenerated; };
	void Regenerate() { isGenerated = false; };
	bool Biomify(noise::SimplexNoiseClass& continenatlness, noise::SimplexNoiseClass& mountainousness, noise::SimplexNoiseClass& weirdness);
	int DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness, const int& weirdness);
	int DetermineLevel(BiomeParameter p, float value);
	bool GenerateComponentNoises();

	bool SetRanges(std::vector<std::vector<float>>& ranges);
	bool SetRange(BiomeParameter p, std::vector<float> range);
	bool SetBiomes(std::vector<biome::Biome>& b);

	biome::Biome& GetBiome(int id) { return biomes[id]; };
	const BiomeMap& GetBiomeMap() const { return biomeMap; };
	int GetBiomeAt(int x, int y);
	noise::SimplexNoiseClass& GetNoiseByParameter(BiomeParameter p);
	noise::NoiseConfigParameters& GetNoiseConfigByParameter(BiomeParameter p);
	std::vector<float>& GetLevelsByParameter(BiomeParameter p);
};