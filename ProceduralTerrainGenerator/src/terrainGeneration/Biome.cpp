#include "Biome.h"

namespace biome{
	Biome::Biome() : id(-1), name(""), temperatureLevel(), humidityLevel(), continentalnessLevel(), mountainousnessLevel(), vegetationLevel(0)
	{
	}

	Biome::Biome(int _id, std::string _name) : id(_id), name(_name), temperatureLevel(), humidityLevel(), continentalnessLevel(), mountainousnessLevel(), vegetationLevel(0)
	{
	}

	Biome::Biome(int _id, std::string _name, vec2 _temperatureLevel, vec2 _humidityLevel, vec2 _continentalnessLevel, vec2 _mountainousnessLevel, vec2 _weirdnessLevel, glm::vec3 _color, int _vegetationLevel) : id(_id), name(_name),
		temperatureLevel(_temperatureLevel), humidityLevel(_humidityLevel), continentalnessLevel(_continentalnessLevel), mountainousnessLevel(_mountainousnessLevel), weirdnessLevel(_weirdnessLevel), color(_color), vegetationLevel(_vegetationLevel)
	{
	}

	Biome::~Biome()
	{
	}

	bool Biome::IsSpecified() const {
		return temperatureLevel.x != -1 && humidityLevel.x != -1 &&
			continentalnessLevel.x != -1 && mountainousnessLevel.x != -1 && weirdnessLevel.x != -1;
	}

	bool Biome::VerifyBiome(const int& T, const int& H, const int& C, const int& M, const int& W) const
	{
		return T >= temperatureLevel.x && T < temperatureLevel.y &&
			H >= humidityLevel.x && H < humidityLevel.y &&
			C >= continentalnessLevel.x && C < continentalnessLevel.y &&
			M >= mountainousnessLevel.x && M < mountainousnessLevel.y &&
			W >= weirdnessLevel.x && W < weirdnessLevel.y;
	}
}