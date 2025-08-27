#include "BiomeGenerator.h"
#include <iostream>

BiomeGenerator::BiomeGenerator() : temperatureNoise(), humidityNoise(), biomeMap(nullptr), height(0), width(0)
{
}

BiomeGenerator::~BiomeGenerator()
{
}

bool BiomeGenerator::Initialize(int _height, int _width)
{
	if (biomeMap) {
		std::cout << "[ERROR] BiomeMap already initialized" << std::endl;
		return false;
	}

	if(!Resize(_height, _width)) {
		return false;
	}
	if(!temperatureNoise.Initialize(height, width)) {
		return false;
	}
	if(!humidityNoise.Initialize(height, width)) {
		return false;
	}

	temperatureNoise.GetConfigRef().seed = 123;
	temperatureNoise.GetConfigRef().option = noise::Options::NOTHING;
	temperatureNoise.GetConfigRef().scale = 0.01f;
	temperatureNoise.GetConfigRef().constrast = 1.5f;

	humidityNoise.GetConfigRef().seed = 321;
	humidityNoise.GetConfigRef().option = noise::Options::NOTHING;
	humidityNoise.GetConfigRef().scale = 0.01f;
	humidityNoise.GetConfigRef().constrast = 1.5f;

	std::vector<biome::Biome>b = {
		biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, 1.f),
		biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, 2, 1.f),
		biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, 7, 1.f),
		biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, 8, 1.f),
		biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, 0, 1.f),
		biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, 5, 1.f)
	};

	std::vector<std::vector<RangedLevel>> r = {
		{{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
		{{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
		{{-1.0f, -0.7f, 0},{-0.7f, -0.2f, 1},{ -0.2f, 0.03f, 2},{0.03f, 0.3f, 3},{0.3f, 1.1f, 4}},
		{{-1.0f, -0.78f, 0},{-0.78f, -0.37f, 1},{-0.37f, -0.2f, 2},{-0.2f, 0.05f, 3},{0.05f, 0.45f, 4},{0.45f, 0.55f, 5},{0.55f, 1.1f, 6}}
	};

	if (!SetBiomes(b)) {
		return false;
	}
	if (!SetRanges(r)) {
		return false;
	}
	//Calculating component noises with basic configuration
	if (GenerateComponentNoises()) {
		return false;
	}

	return true;
}

bool BiomeGenerator::Resize(int _height, int _width)
{
	if (_height <= 0 || _width <= 0) {
		std::cout << "[ERROR] Biome map couldnt be resized, width and height must be greater than 0\n";
		return false;
	}
	if (width == this->width && height == this->height) {
		std::cout << "[LOG] Biome map is already initialized with the same size\n";
		return false;
	}

	width = _width;
	height = _height;
	if (biomeMap) {
		delete[] biomeMap;
	}
	biomeMap = new int[width * height];
	temperatureNoise.Resize(height, width);
	humidityNoise.Resize(height, width);

	std::cout << "[LOG] BiomeGenerator has been succesfully initialized with size: " << height << "x" << width << "\n";
	return true;
}



bool BiomeGenerator::Biomify(float* heightMap, noise::SimplexNoiseClass& continenatlness, noise::SimplexNoiseClass& mountainousness)
{
	if (!heightMap || !biomeMap) {
		std::cout << "[ERROR] HeightMap or biomeMap not initialized" << std::endl;
		return false;
	}

	int T, H, C, M;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (heightMap[y * width + x] <= 64.0f) {
				biomeMap[y * width + x] = 5;
				continue;
			}
			H = DetermineLevel(BiomeParameter::HUMIDITY, humidityNoise.GetVal(x, y));
			T = DetermineLevel(BiomeParameter::TEMPERATURE, temperatureNoise.GetVal(x, y));
			C = DetermineLevel(BiomeParameter::CONTINENTALNESS, continenatlness.GetVal(x, y));
			M = DetermineLevel(BiomeParameter::MOUNTAINOUSNESS, mountainousness.GetVal(x, y));

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

int BiomeGenerator::DetermineLevel(BiomeParameter p, float value)
{
	switch (p)
	{
	case BiomeParameter::HUMIDITY:
		for (const auto& it : humidityLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case BiomeParameter::TEMPERATURE:
		for (const auto& it : temperatureLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case BiomeParameter::CONTINENTALNESS:
		for (const auto& it : continentalnessLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	case BiomeParameter::MOUNTAINOUSNESS:
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

bool BiomeGenerator::GenerateComponentNoises()
{
	if (!temperatureNoise.GenerateFractalNoise()) {
		return false;
	}
	if (!humidityNoise.GenerateFractalNoise()) {
		return false;
	}

	return true;
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

