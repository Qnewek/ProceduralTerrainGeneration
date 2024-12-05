#include "Biome.h"

namespace biome{
	Biome::Biome() : m_id(-1), m_Name(""), m_TemperatureLevel(), m_HumidityLevel(), m_ContinentalnessLevel(), m_MountainousnessLevel(), texOffset(0), vegetationLevel(0)
	{
	}

	Biome::Biome(int id, std::string name) : m_id(id), m_Name(name), m_TemperatureLevel(), m_HumidityLevel(), m_ContinentalnessLevel(), m_MountainousnessLevel(), texOffset(0), vegetationLevel(0)
	{
	}

	Biome::Biome(int id, std::string name, vec2 temperatureLevel, vec2 humidityLevel, vec2 continentalnessLevel, vec2 mountainousnessLevel, int texOffset, int vegetationLevel) : m_id(id), m_Name(name),
		m_TemperatureLevel(temperatureLevel), m_HumidityLevel(humidityLevel), m_ContinentalnessLevel(continentalnessLevel), m_MountainousnessLevel(mountainousnessLevel), texOffset(texOffset), vegetationLevel(vegetationLevel)
	{
	}

	Biome::~Biome()
	{
	}

	vec2 Biome::getTemperatureLevel() const
	{
		return m_TemperatureLevel;
	}

	vec2 Biome::getHumidityLevel() const
	{
		return m_HumidityLevel;
	}

	vec2 Biome::getContinentalnessLevel() const
	{
		return m_ContinentalnessLevel;
	}

	vec2 Biome::getMountainousnessLevel() const
	{
		return m_MountainousnessLevel;
	}

	int Biome::getId() const
	{
		return m_id;
	}

	std::string Biome::getName() const
	{
		return m_Name;
	}

	int Biome::getTexOffset() const
	{
		return texOffset;
	}

	void Biome::setTemperatureLevel(vec2 temperatureLevel)
	{
		m_TemperatureLevel = temperatureLevel;
	}

	void Biome::setHumidityLevel(vec2 humidityLevel)
	{
		m_HumidityLevel = humidityLevel;
	}

	void Biome::setContinentalnessLevel(vec2 continentalnessLevel)
	{
		m_ContinentalnessLevel = continentalnessLevel;
	}

	void Biome::setMountainousnessLevel(vec2 mountainousnessLevel)
	{
		m_MountainousnessLevel = mountainousnessLevel;
	}

	bool Biome::isSpecified() const {
		return m_TemperatureLevel.x != -1 && m_HumidityLevel.x != -1 &&
			m_ContinentalnessLevel.x != -1 && m_MountainousnessLevel.x != -1;
	}

	bool Biome::verifyBiome(const int& T, const int& H, const int& C, const int& M) const
	{
		return T >= m_TemperatureLevel.x && T < m_TemperatureLevel.y &&
			H >= m_HumidityLevel.x && H < m_HumidityLevel.y &&
			C >= m_ContinentalnessLevel.x && C < m_ContinentalnessLevel.y &&
			M >= m_MountainousnessLevel.x && M < m_MountainousnessLevel.y;
	}
}