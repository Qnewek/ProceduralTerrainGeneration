#include "Biome.h"

namespace biome{
	Biome::Biome() : id(-1), name(""), temperatureLevel(), humidityLevel(), continentalnessLevel(), mountainousnessLevel(), weirdnessLevel(), vegetationLevel(0)
	{
	}

	Biome::Biome(int _id, std::string _name) : id(_id), name(_name), temperatureLevel(), humidityLevel(), continentalnessLevel(), mountainousnessLevel(), weirdnessLevel(), vegetationLevel(0)
	{
	}

	Biome::Biome(int _id, std::string _name, glm::vec2 _temperatureLevel, glm::vec2 _humidityLevel, glm::vec2 _continentalnessLevel, glm::vec2 _mountainousnessLevel, glm::vec2 _weirdnessLevel, glm::vec3 _color, float _vegetationLevel) : id(_id), name(_name),
		temperatureLevel(_temperatureLevel), humidityLevel(_humidityLevel), continentalnessLevel(_continentalnessLevel), mountainousnessLevel(_mountainousnessLevel), weirdnessLevel(_weirdnessLevel), color(_color), vegetationLevel(_vegetationLevel)
	{
		isSpecified = true;
	}

	Biome::~Biome()
	{
	}

	bool Biome::VerifyBiome(const int& T, const int& H, const int& C, const int& M, const int& W) const
	{
		return T >= temperatureLevel.x && T < temperatureLevel.y &&
			H >= humidityLevel.x && H < humidityLevel.y &&
			C >= continentalnessLevel.x && C < continentalnessLevel.y &&
			M >= mountainousnessLevel.x && M < mountainousnessLevel.y &&
			W >= weirdnessLevel.x && W < weirdnessLevel.y;
	}

	std::vector<Biome> DefaultBiomes()
	{
		return {
			Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, {0, 1}, glm::vec3(0.2f, 0.8f, 0.2f), 1.0f),
			Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, {0, 1}, glm::vec3(0.95f, 0.1f, 0.1f), 1.0f),
			Biome(2, "Snow",		{0, 1}, {0, 4}, {3, 5}, {0, 4}, {0, 1}, glm::vec3(0.95f, 0.95f, 0.95f), 1.0f),
			Biome(3, "Sand",		{0, 4}, {0, 4}, {2, 3}, {0, 7}, {0, 1}, glm::vec3(0.93f, 0.82f, 0.55f), 1.0f),
			Biome(4, "Mountain",	{0, 4}, {0, 4}, {4, 5}, {4, 7}, {0, 1}, glm::vec3(0.5f, 0.5f, 0.5f), 1.0f),
			Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, {0, 1}, glm::vec3(0.2f, 0.4f, 0.85f), 1.0f)
		};
	}
}