#include "BiomeGenerator.h"
#include <iostream>

BiomeGenerator::BiomeGenerator() : temperatureNoise(), humidityNoise()
{
	temperatureNoise.GetConfigRef().seed = 123;
	temperatureNoise.GetConfigRef().option = noise::Options::NOTHING;
	temperatureNoise.GetConfigRef().scale = 0.01f;
	temperatureNoise.GetConfigRef().constrast = 1.5f;

	humidityNoise.GetConfigRef().seed = 321;
	humidityNoise.GetConfigRef().option = noise::Options::NOTHING;
	humidityNoise.GetConfigRef().scale = 0.01f;
	humidityNoise.GetConfigRef().constrast = 1.5f;
}

BiomeGenerator::~BiomeGenerator()
{
}

bool BiomeGenerator::Biomify(float* map, int* biomeMap, const int& width, const int& height, const int& seed, const noise::SimplexNoiseClass& continenatlnes, const noise::SimplexNoiseClass& mountainousness)
{
	if (!map) {
		std::cout << "[ERROR] HeightMap not initialized" << std::endl;
		return false;
	}
	if (!biomeMap) {
		std::cout << "[ERROR] BiomeMap not initialized" << std::endl;
		return false;
	}

	GenerateTemperatureNoise(width, height, seed);
	GenerateHumidityNoise(width, height, seed);

	int T, H, C, M;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (map[y * width + x] <= 64.0f) {
				biomeMap[y * width + x] = 5;
				continue;
			}
			H = DetermineLevel(WorldParameter::HUMIDITY, humidityNoise.GetVal(x, y));
			T = DetermineLevel(WorldParameter::TEMPERATURE, temperatureNoise.GetVal(x, y));
			C = DetermineLevel(WorldParameter::CONTINENTALNESS, continenatlnes.GetVal(x, y));
			M = DetermineLevel(WorldParameter::MOUNTAINOUSNESS, mountainousness.GetVal(x, y));

			biomeMap[y * width + x] = DetermineBiome(H, T, C, M);
		}
	}
	std::cout << " [LOG] BiomeMap succesfully evaluated" << std::endl;
	return true;
}


int BiomeGenerator::DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness)
{
	for (auto& it : biomes) {
		if (it.second.VerifyBiome(temperature, humidity, continentalness, mountainousness))
			return it.first;
	}

	return 0;
}

int BiomeGenerator::DetermineLevel(WorldParameter p, float value)
{
	switch (p)
	{
	case WorldParameter::HUMIDITY:
		for (const auto& it : humidityLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::TEMPERATURE:
		for (const auto& it : temperatureLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::CONTINENTALNESS:
		for (const auto& it : continentalnessLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::MOUNTAINOUSNESS:
		for (const auto& it : mountainousnessLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	default:
		std::cout << "[ERROR] Wrong biome option!" << std::endl;
		return -1;
		break;
	}
	std::cout << "[ERROR] Level not found for value: " << value << std::endl;
	return -2;
}

void BiomeGenerator::GenerateTemperatureNoise(int width, int height, int seed)
{
	temperatureNoise.SetSeed(seed);
	temperatureNoise.SetMapSize(width, height);
	temperatureNoise.InitMap();
	if (!temperatureNoise.GenerateFractalNoise()) {
		std::cout << "[ERROR] Failed to generate temperature noise" << std::endl;
	}
}

void BiomeGenerator::GenerateHumidityNoise(int width, int height, int seed)
{
	humidityNoise.Reseed();
	humidityNoise.SetMapSize(width, height);
	humidityNoise.InitMap();
	if (!humidityNoise.GenerateFractalNoise()) {
		std::cout << "[ERROR] Failed to generate humidity noise" << std::endl;
	}
}

bool BiomeGenerator::SetRanges(std::vector<std::vector<RangedLevel>>& ranges)
{
	if (ranges.size() != 4) {
		std::cout << "[ERROR] Ranges initialization array empty!" << std::endl;
		return false;
	}

	temperatureLevels = ranges[0];
	humidityLevels = ranges[1];
	continentalnessLevels = ranges[2];
	mountainousnessLevels = ranges[3];

	return true;
}

bool BiomeGenerator::SetRange(char c, std::vector<RangedLevel> range)
{
	switch (c) {
	case 'c':
		continentalnessLevels = range;
		break;
	case 'm':
		mountainousnessLevels = range;
		break;
	case 't':
		temperatureLevels = range;
		break;
	case 'h':
		humidityLevels = range;
		break;
	default:
		std::cout << "[ERROR] Wrong biome option!" << std::endl;
		return false;
		break;
	}
	return true;
}

bool BiomeGenerator::SetBiomes(std::vector<biome::Biome>& b)
{
	if (b.empty()) {
		std::cout << "[ERROR] Biomes initialization array empty!" << std::endl;
		return false;
	}

	for (auto& it : b) {
		biomes[it.GetId()] = biome::Biome(it);
	}

	return true;
}

