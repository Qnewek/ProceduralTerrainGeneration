#include "BiomeGenerator.h"
#include <iostream>

BiomeGenerator::BiomeGenerator() : temperatureNoise(), humidityNoise(), biomesLevels(5)
{
}

BiomeGenerator::~BiomeGenerator()
{
}

bool BiomeGenerator::Initialize(int _height, int _width)
{

	humidityNoise.GetConfigRef().octaves = 4;
	temperatureNoise.GetConfigRef().seed = 123;
	temperatureNoise.GetConfigRef().option = noise::Options::NOTHING;
	temperatureNoise.GetConfigRef().scale = 0.5f;
	temperatureNoise.GetConfigRef().constrast = 1.5f;

	humidityNoise.GetConfigRef().octaves = 4;
	humidityNoise.GetConfigRef().seed = 321;
	humidityNoise.GetConfigRef().option = noise::Options::NOTHING;
	humidityNoise.GetConfigRef().scale = 0.3f;
	humidityNoise.GetConfigRef().constrast = 1.5f;

	std::vector<biome::Biome>b = {
		biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, {0, 1}, glm::vec3(0.2f, 0.8f, 0.2f), 1.0f),
		biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, {0, 1}, glm::vec3(0.95f, 0.1f, 0.1f), 1.0f),
		biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, {0, 1}, glm::vec3(0.95f, 0.95f, 0.95f), 1.0f),
		biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, {0, 1}, glm::vec3(0.93f, 0.82f, 0.55f), 1.0f),
		biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, {0, 1}, glm::vec3(0.5f, 0.5f, 0.5f), 1.0f),
		biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, {0, 1}, glm::vec3(0.2f, 0.4f, 0.85f), 1.0f)
	};

	//Value -1 as a lower boundry
	std::vector<std::vector<float>> r = {
		{-1.0f, -0.5f, 0.0f, 0.5f, 1.01f},
		{-1.0f, -0.5f, 0.0f, 0.5f, 1.01f},
		{-1.0f, -0.7f, -0.2f, 0.03f, 0.3f, 1.01f},
		{-1.0f, -0.78f, -0.37f, -0.2f, 0.05f, 0.45f, 0.55f, 1.01f},
		{-1.0f, 1.1f}
	};

	if (!SetBiomes(b)) {
		return false;
	}
	if (!SetRanges(r)) {
		return false;
	}

	return true;
}

int BiomeGenerator::GetVal(float x, float y, float continentalness, float mountainousness, float weirdness)
{	
	int T, H, C, M, W;
	H = DetermineLevel(BiomeParameter::HUMIDITY, humidityNoise.GetVal(x, y));
	T = DetermineLevel(BiomeParameter::TEMPERATURE, temperatureNoise.GetVal(x, y));
	C = DetermineLevel(BiomeParameter::CONTINENTALNESS, continentalness);
	M = DetermineLevel(BiomeParameter::MOUNTAINOUSNESS, mountainousness);
	W = DetermineLevel(BiomeParameter::WEIRDNESS, weirdness); 

	return DetermineBiome(H, T, C, M, W);;
}


int BiomeGenerator::DetermineBiome(const int& temperature, const int& humidity, const int& continentalness, const int& mountainousness, const int& weirdness)
{
	for (auto& it : biomes) {
		if (it.second.VerifyBiome(temperature, humidity, continentalness, mountainousness, weirdness))
			return it.first;
	}

	return 0;
}

int BiomeGenerator::DetermineLevel(BiomeParameter p, float value)
{
	std::vector<float> *levels;
	switch (p)
	{
	case BiomeParameter::TEMPERATURE:
	{
		levels = &biomesLevels[0];
		break;
	}
	case BiomeParameter::HUMIDITY:
	{
		levels = &biomesLevels[1];
		break;
	}
	case BiomeParameter::CONTINENTALNESS:
	{
		levels = &biomesLevels[2];
		break;
	}
	case BiomeParameter::MOUNTAINOUSNESS:
	{
		levels = &biomesLevels[3];
		break;
	}
	case BiomeParameter::WEIRDNESS:
	{
		levels = &biomesLevels[4];
		break;
	}
	default:
		std::cout << "[ERROR] Wrong biome option!" << std::endl;
		return -1;
		break;
	}
	for (int i = 0; i < levels->size(); i++) {
		if (value < (*levels)[i]) {
			return i - 1;
		}
	}
	std::cout << "[ERROR] Level not found for value: " << value << std::endl;
	return -2;
}

bool BiomeGenerator::SetRanges(std::vector<std::vector<float>>& ranges)
{
	if (ranges.size() != 5) {
		std::cout << "[ERROR] Ranges initialization array empty!" << std::endl;
		return false;
	}

	biomesLevels = ranges;

	return true;
}

bool BiomeGenerator::SetRange(BiomeParameter c, std::vector<float> range)
{
	switch (c) {
	case BiomeParameter::TEMPERATURE:
	{
		biomesLevels[0] = range;
		break;
	}
	case BiomeParameter::HUMIDITY:
	{
		biomesLevels[1] = range;
		break;
	}
	case BiomeParameter::CONTINENTALNESS:
	{
		biomesLevels[2] = range;
		break;
	}
	case BiomeParameter::MOUNTAINOUSNESS:
	{
		biomesLevels[3] = range;
		break;
	}
	case BiomeParameter::WEIRDNESS:
	{
		biomesLevels[4] = range;
		break;
	}
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

std::vector<float>& BiomeGenerator::GetLevelsByParameter(BiomeParameter p)
{
	switch (p) {
	case BiomeParameter::TEMPERATURE:
	{
		return biomesLevels[0];
		break;
	}
	case BiomeParameter::HUMIDITY:
	{
		return biomesLevels[1];
		break;
	}
	case BiomeParameter::CONTINENTALNESS:
	{
		return biomesLevels[2];
		break;
	}
	case BiomeParameter::MOUNTAINOUSNESS:
	{
		return biomesLevels[3];
		break;
	}
	case BiomeParameter::WEIRDNESS:
	{
		return biomesLevels[4];
		break;
	}
	default:
	{
		std::cout << "[ERROR] Wrong biome option!" << std::endl;
		break;
	}
	}
	return biomesLevels[0];
}

