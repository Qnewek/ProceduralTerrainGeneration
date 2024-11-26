#include "Biome.h"

Biome::Biome() : m_id(-1), m_TemperatureLevel({ -1, -1 }), m_HumidityLevel({ -1, -1 }), m_ContinentalnessLevel({ -1, -1 }), m_MountainousnessLevel({ -1, -1 })
{
}

Biome::Biome(int id, std::string name) : m_id(id), m_Name(name), m_TemperatureLevel({ -1, -1 }), m_HumidityLevel({ -1, -1 }), m_ContinentalnessLevel({ -1, -1 }), m_MountainousnessLevel({ -1, -1 })
{
}

Biome::Biome(int id, std::string name, vec2 temperatureLevel, vec2 humidityLevel, vec2 continentalnessLevel, vec2 mountainousnessLevel) : m_id(id), m_Name(name),
m_TemperatureLevel(temperatureLevel), m_HumidityLevel(humidityLevel), m_ContinentalnessLevel(continentalnessLevel), m_MountainousnessLevel(mountainousnessLevel)
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

const int& Biome::getId() const
{
	return m_id;
}

const std::string& Biome::getName() const
{
	return m_Name;
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
