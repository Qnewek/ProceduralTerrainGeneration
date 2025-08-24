#include "Biome.h"

namespace biome{
	Biome::Biome() : id(-1), name(""), temperatureLevel(), humidityLevel(), continentalnessLevel(), mountainousnessLevel(), texOffset(0), vegetationLevel(0)
	{
	}

	Biome::Biome(int id, std::string name) : id(id), name(name), temperatureLevel(), humidityLevel(), continentalnessLevel(), mountainousnessLevel(), texOffset(0), vegetationLevel(0)
	{
	}

	Biome::Biome(int id, std::string name, vec2 temperatureLevel, vec2 humidityLevel, vec2 continentalnessLevel, vec2 mountainousnessLevel, int texOffset, int vegetationLevel) : id(id), name(name),
		temperatureLevel(temperatureLevel), humidityLevel(humidityLevel), continentalnessLevel(continentalnessLevel), mountainousnessLevel(mountainousnessLevel), texOffset(texOffset), vegetationLevel(vegetationLevel)
	{
	}

	Biome::~Biome()
	{
	}

	bool Biome::IsSpecified() const {
		return temperatureLevel.x != -1 && humidityLevel.x != -1 &&
			continentalnessLevel.x != -1 && mountainousnessLevel.x != -1;
	}

	bool Biome::VerifyBiome(const int& T, const int& H, const int& C, const int& M) const
	{
		return T >= temperatureLevel.x && T < temperatureLevel.y &&
			H >= humidityLevel.x && H < humidityLevel.y &&
			C >= continentalnessLevel.x && C < continentalnessLevel.y &&
			M >= mountainousnessLevel.x && M < mountainousnessLevel.y;
	}
}