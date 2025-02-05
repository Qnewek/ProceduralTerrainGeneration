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

	vec2 Biome::GetTemperatureLevel() const
	{
		return temperatureLevel;
	}

	vec2 Biome::GetHumidityLevel() const
	{
		return humidityLevel;
	}

	vec2 Biome::GetContinentalnessLevel() const
	{
		return continentalnessLevel;
	}

	vec2 Biome::GetMountainousnessLevel() const
	{
		return mountainousnessLevel;
	}

	int Biome::GetId() const
	{
		return id;
	}

	std::string Biome::GetName() const
	{
		return name;
	}

	int Biome::GetTexOffset() const
	{
		return texOffset;
	}

	void Biome::SetTemperatureLevel(vec2 temperatureLevel)
	{
		temperatureLevel = temperatureLevel;
	}

	void Biome::SetHumidityLevel(vec2 humidityLevel)
	{
		humidityLevel = humidityLevel;
	}

	void Biome::SetContinentalnessLevel(vec2 continentalnessLevel)
	{
		continentalnessLevel = continentalnessLevel;
	}

	void Biome::SetMountainousnessLevel(vec2 mountainousnessLevel)
	{
		mountainousnessLevel = mountainousnessLevel;
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