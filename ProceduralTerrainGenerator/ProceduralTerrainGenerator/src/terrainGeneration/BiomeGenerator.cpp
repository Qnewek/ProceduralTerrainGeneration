#include "BiomeGenerator.h"
#include <iostream>

BiomeGenerator::BiomeGenerator()
{
}

BiomeGenerator::~BiomeGenerator()
{
}

int BiomeGenerator::determineLevel(WorldParameter p, float value)
{
	switch (p)
	{
	case WorldParameter::Humidity:
		for (const auto& it : m_HumidityLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::Temperature:
		for (const auto& it : m_TemperatureLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::Continentalness:
		for (const auto& it : m_ContinentalnessLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case WorldParameter::Mountainousness:
		for (const auto& it : m_MountainousnessLevels) {
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

int BiomeGenerator::determineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness)
{
	for (auto& it : m_Biomes) {
		if (it.second.verifyBiome(temperature, humidity, continentalness, mountainousness))
			return it.first;
	}

	return 0;
}

bool BiomeGenerator::biomify(float* map, int* biomeMap, const int& width, const int& height, const int& chunkRes, const int& seed, const noise::SimplexNoiseClass& continenatlnes, const noise::SimplexNoiseClass& mountainouss)
{
	if (!biomeMap) {
		std::cout << "[ERROR] BiomeMap not initialized" << std::endl;
		return false;
	}

	temperatureNoise.setSeed(seed);
	temperatureNoise.setMapSize(width, height);
	temperatureNoise.setChunkSize(chunkRes, chunkRes);
	temperatureNoise.getConfigRef().option = noise::Options::NOTHING;
	temperatureNoise.getConfigRef().scale = 0.01f;
	temperatureNoise.getConfigRef().constrast = 1.5f;
	temperatureNoise.initMap();
	if (!temperatureNoise.generateFractalNoiseByChunks()) {
		std::cout << "[ERROR] Failed to generate temperature noise" << std::endl;
		return false;
	}

	humidityNoise.setSeed(seed/2);
	humidityNoise.setMapSize(width, height);
	humidityNoise.setChunkSize(chunkRes, chunkRes);
	humidityNoise.getConfigRef().option = noise::Options::NOTHING;
	humidityNoise.getConfigRef().scale = 0.01f;
	humidityNoise.getConfigRef().constrast = 1.5f;
	humidityNoise.initMap();
	if (!humidityNoise.generateFractalNoiseByChunks()) {
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
			H = determineLevel(WorldParameter::Humidity, humidityNoise.getVal(x, y));
			T = determineLevel(WorldParameter::Temperature, temperatureNoise.getVal(x, y));
			C = determineLevel(WorldParameter::Continentalness, continenatlnes.getVal(x, y));
			M = determineLevel(WorldParameter::Mountainousness, mountainouss.getVal(x, y));

			biomeMap[y * width * chunkRes + x] = determineBiome(H, T, C, M);
		}
	}
	return true;
}

biome::Biome& BiomeGenerator::getBiome(int id)
{
	return m_Biomes[id];
}

noise::NoiseConfigParameters& BiomeGenerator::getTemperatureNoiseConfig()
{
	return temperatureNoise.getConfigRef();
}

noise::NoiseConfigParameters& BiomeGenerator::getHumidityNoiseConfig()
{
	return humidityNoise.getConfigRef();
}

bool BiomeGenerator::setRanges(std::vector<std::vector<RangedLevel>>& ranges)
{
	if (ranges.size() != 4) {
		std::cout << "[ERROR] Ranges initialization array empty!" << std::endl;
		return false;
	}

	m_TemperatureLevels = ranges[0];
	m_HumidityLevels = ranges[1];
	m_ContinentalnessLevels = ranges[2];
	m_MountainousnessLevels = ranges[3];

	return true;
}

bool BiomeGenerator::setBiomes(std::vector<biome::Biome>& biomes)
{
	if (biomes.empty()) {
		std::cout << "[ERROR] Biomes initialization array empty!" << std::endl;
		return false;
	}

	for (auto& it : biomes) {
		m_Biomes[it.getId()] = biome::Biome(it);
	}

	return true;
}

