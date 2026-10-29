#include "BiomeGenerator.h"
#include <iostream>

BiomeGenerator::BiomeGenerator() : height(0), width(0), temperatureNoise(), humidityNoise()
{
}

BiomeGenerator::~BiomeGenerator()
{
}

bool BiomeGenerator::Initialize(int _width, int _height)
{
	if (biomeMap) {
		std::cout << "[ERROR] BiomeMap already initialized" << std::endl;
		return false;
	}

	if (!temperatureNoise.Initialize(_width, _height)) {
		std::cout << "[ERROR] Temperature noise couldnt be initialized\n";
		return false;
	}
	if (!humidityNoise.Initialize(_width, _height)) {
		std::cout << "[ERROR] Humidity noise couldnt be initialized\n";
		return false;
	}

	if(!Resize(_width, _height)) {
		return false;
	}

	std::vector<biome::Biome>b = {
		biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, {0, 1}, glm::vec3(0.2f, 0.8f, 0.2f), 1.0f),
		biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, {0, 1}, glm::vec3(0.95f, 0.1f, 0.1f), 1.0f),
		biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, {0, 1}, glm::vec3(0.95f, 0.95f, 0.95f), 1.0f),
		biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, {0, 1}, glm::vec3(0.93f, 0.82f, 0.55f), 1.0f),
		biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, {0, 1}, glm::vec3(0.5f, 0.5f, 0.5f), 1.0f),
		biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, {0, 1}, glm::vec3(0.2f, 0.4f, 0.85f), 1.0f)
	};

	if (!SetBiomes(b)) {
		return false;
	}

	return true;
}

bool BiomeGenerator::Resize(int _width, int _height)
{
	if (_height <= 0 || _width <= 0) {
		std::cout << "[ERROR] Biome map couldnt be resized, width and height must be greater than 0\n";
		return false;
	}
	if (_width == this->width && _height == this->height) {
		std::cout << "[ERROR] Biome map is already initialized with the same size\n";
		return false;
	}

	width = _width;
	height = _height;
	biomeMap.Resize(width, height);
	temperatureNoise.Resize(width, height);
	humidityNoise.Resize(width, height);

	std::cout << "[LOG] BiomeGenerator has been succesfully initialized with size: " << width << "x" << height << "\n";
	isGenerated = false;
	return true;
}



bool BiomeGenerator::Biomify(const BiomeConfig& config, noise::SimplexNoiseClass& continenatlness, noise::SimplexNoiseClass& mountainousness, noise::SimplexNoiseClass& weirdness)
{
	if(!biomeMap) {
		std::cout << "[ERROR] BiomeMap not initialized\n";
		return false;
	}
	if( continenatlness.GetHeight() != height || continenatlness.GetWidth() != width ||
		mountainousness.GetHeight() != height || mountainousness.GetWidth() != width ||
		weirdness.GetHeight() != height || weirdness.GetWidth() != width) {
		std::cout << "[ERROR] One of the component noises has invalid size\n";
		return false;
	}

	int T, H, C, M, W;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			H = DetermineLevel(config.humidityLevels, humidityNoise.GetVal(x, y));
			T = DetermineLevel(config.temperatureLevels, temperatureNoise.GetVal(x, y));
			C = DetermineLevel(config.continentalnessLevels, continenatlness.GetVal(x, y));
			M = DetermineLevel(config.mountainousnessLevels, mountainousness.GetVal(x, y));
			W = DetermineLevel(config.weirdnessLevels, weirdness.GetVal(x, y));

			biomeMap[y * width + x] = DetermineBiome(T, H, C, M, W);
		}
	}
	std::cout << "[LOG] BiomeMap succesfully evaluated" << std::endl;
	isGenerated = true;
	return true;
}


int BiomeGenerator::DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness, const int& weirdness)
{
	for (auto& it : biomes) {
		if (it.second.VerifyBiome(temperature, humidity, continentalness, mountainousness, weirdness))
			return it.first;
	}

	return 0;
}

//Returns the level of value: index i such that levels[i] <= value < levels[i + 1]
//@param levels - ascending level boundaries (see BiomeConfig)
int BiomeGenerator::DetermineLevel(const std::vector<float>& levels, float value)
{
	for (int i = 0; i < (int)levels.size(); i++) {
		if (value < levels[i]) {
			return i - 1;
		}
	}
	std::cout << "[ERROR] Level not found for value: " << value << std::endl;
	return -2;
}

bool BiomeGenerator::GenerateComponentNoises(const BiomeConfig& config)
{
	if (!temperatureNoise.GenerateFractalNoise(config.temperature, 0.0f, 0.0f)) {
		return false;
	}
	if (!humidityNoise.GenerateFractalNoise(config.humidity, 0.0f, 0.0f)) {
		return false;
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

int BiomeGenerator::GetBiomeAt(int x, int y)
{
	if (!biomeMap || width == 0 || height == 0) {
		std::cout << "[ERROR] BiomeMap has not been initialized!\n";
		return -1;
	}
	if( x < 0 || x >= width || y < 0 || y >= height) {
		std::cout << "[ERROR] Coordinates out of bounds!" << std::endl;
		return -1;
	}
	
	return biomeMap[y * width + x];
}

noise::SimplexNoiseClass& BiomeGenerator::GetNoiseByParameter(BiomeParameter p)
{
	switch (p) {
	case BiomeParameter::HUMIDITY:
		return humidityNoise;
		break;
	case BiomeParameter::TEMPERATURE:
		return temperatureNoise;
		break;
	default:
		std::cout << "[ERROR] Wrong biome option!" << std::endl;
		return temperatureNoise;
		break;
	}
}


