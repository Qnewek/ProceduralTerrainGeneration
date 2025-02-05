#include "BiomeGenerator.h"
#include <iostream>

BiomeGenerator::BiomeGenerator()
{
}

BiomeGenerator::~BiomeGenerator()
{
}

int BiomeGenerator::DetermineLevel(WorldParameter p, float value)
{
	switch (p)
	{
	case WorldParameter::Humidity:
		for (const auto& it : humidityLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::Temperature:
		for (const auto& it : temperatureLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::Continentalness:
		for (const auto& it : continentalnessLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::Mountainousness:
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

int BiomeGenerator::DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness)
{
	for (auto& it : biomes) {
		if (it.second.VerifyBiome(temperature, humidity, continentalness, mountainousness))
			return it.first;
	}

	return 0;
}

bool BiomeGenerator::Biomify(float* map, int* biomeMap, const int& width, const int& height, const int& chunkRes, const int& seed, const noise::SimplexNoiseClass& continenatlnes, const noise::SimplexNoiseClass& mountainouss)
{
	if (!biomeMap) {
		std::cout << "[ERROR] BiomeMap not initialized" << std::endl;
		return false;
	}

	temperatureNoise.Reseed();
	temperatureNoise.SetMapSize(width, height);
	temperatureNoise.SetChunkSize(chunkRes, chunkRes);
	temperatureNoise.GetConfigRef().option = noise::Options::NOTHING;
	temperatureNoise.GetConfigRef().scale = 0.01f;
	temperatureNoise.GetConfigRef().constrast = 1.5f;
	temperatureNoise.InitMap();
	if (!temperatureNoise.GenerateFractalNoiseByChunks()) {
		std::cout << "[ERROR] Failed to generate temperature noise" << std::endl;
		return false;
	}

	humidityNoise.Reseed();
	humidityNoise.SetMapSize(width, height);
	humidityNoise.SetChunkSize(chunkRes, chunkRes);
	humidityNoise.GetConfigRef().option = noise::Options::NOTHING;
	humidityNoise.GetConfigRef().scale = 0.01f;
	humidityNoise.GetConfigRef().constrast = 1.5f;
	humidityNoise.InitMap();
	if (!humidityNoise.GenerateFractalNoiseByChunks()) {
		std::cout << "[ERROR] Failed to generate humidity noise" << std::endl;
		return false;
	}

	int T, H, C, M;
	std::cout << "[LOG] Evaluating biomeMap..." << std::endl;

	for (int y = 0; y < height * chunkRes; y++) {
		for (int x = 0; x < width * chunkRes; x++) {
			if (map[y * width * chunkRes + x] <= 64.0f) {
				biomeMap[y * width * chunkRes + x] = 5;
				continue;
			}
			H = DetermineLevel(WorldParameter::Humidity, humidityNoise.GetVal(x, y));
			T = DetermineLevel(WorldParameter::Temperature, temperatureNoise.GetVal(x, y));
			C = DetermineLevel(WorldParameter::Continentalness, continenatlnes.GetVal(x, y));
			M = DetermineLevel(WorldParameter::Mountainousness, mountainouss.GetVal(x, y));

			biomeMap[y * width * chunkRes + x] = DetermineBiome(H, T, C, M);
		}
	}
	return true;
}

biome::Biome& BiomeGenerator::GetBiome(int id)
{
	return biomes[id];
}

noise::NoiseConfigParameters& BiomeGenerator::GetTemperatureNoiseConfig()
{
	return temperatureNoise.GetConfigRef();
}

noise::NoiseConfigParameters& BiomeGenerator::GetHumidityNoiseConfig()
{
	return humidityNoise.GetConfigRef();
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

