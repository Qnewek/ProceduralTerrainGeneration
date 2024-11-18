#include "BiomeSettings.h"

BiomeSettings::BiomeSettings()
{
}

BiomeSettings::~BiomeSettings()
{
}

int BiomeSettings::determineLevel(WorldParameter p, float value)
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
	case WorldParameter::Variant:
		for (const auto& it : m_VariantLevels) {
			if (value >= it.min && value < it.max) {
				return it.level;
			}
		}
		break;
	default:
		return -1;
		break;
	}
}

int BiomeSettings::determineBiome(int humidity, int temperature, int continentalness, int mountainousness, int variant)
{

	return 0;
}

Biome& BiomeSettings::getBiome(int id)
{
	return m_Biomes[id];
}

bool BiomeSettings::setRanges()
{
	//Temporary static settings
	m_HumidityLevels.push_back({ -1.0f, -0.6f, 0 });
	m_HumidityLevels.push_back({ -0.6f, -0.2f, 1 });
	m_HumidityLevels.push_back({ -0.2f, 0.2f, 2 });
	m_HumidityLevels.push_back({ 0.2f, 0.6f, 3 });
	m_HumidityLevels.push_back({ 0.6f, 1.0f, 4 });

	m_TemperatureLevels.push_back({ -1.0f, -0.6f, 0 });
	m_TemperatureLevels.push_back({ -0.6f, -0.2f, 1 });
	m_TemperatureLevels.push_back({ -0.2f, 0.2f, 2 });
	m_TemperatureLevels.push_back({ 0.2f, 0.6f, 3 });
	m_TemperatureLevels.push_back({ 0.6f, 1.0f, 4 });

	m_ContinentalnessLevels.push_back({ -1.0f, -0.6f, 0 });
	m_ContinentalnessLevels.push_back({ -0.6f, -0.2f, 1 });
	m_ContinentalnessLevels.push_back({ -0.2f, 0.2f, 2 });
	m_ContinentalnessLevels.push_back({ 0.2f, 0.6f, 3 });
	m_ContinentalnessLevels.push_back({ 0.6f, 1.0f, 4 });

	m_MountainousnessLevels.push_back({ -1.0f, -0.6f, 0 });
	m_MountainousnessLevels.push_back({ -0.6f, -0.2f, 1 });
	m_MountainousnessLevels.push_back({ -0.2f, 0.2f, 2 });
	m_MountainousnessLevels.push_back({ 0.2f, 0.6f, 3 });
	m_MountainousnessLevels.push_back({ 0.6f, 1.0f, 4 });
		
	m_VariantLevels.push_back({ -1.0f, -0.6f, 0 });
	m_VariantLevels.push_back({ -0.6f, -0.2f, 1 });
	m_VariantLevels.push_back({ -0.2f, 0.2f, 2 });
	m_VariantLevels.push_back({ 0.2f, 0.6f, 3 });
	m_VariantLevels.push_back({ 0.6f, 1.0f, 4 });

	makeLookUpTable();

	return true;
}

bool BiomeSettings::makeLookUpTable()
{
	m_LookUpTable.resize(5);
	for (int i = 0; i < 5; i++) {
		m_LookUpTable[i].resize(5);
		for (int j = 0; j < 5; j++) {
			m_LookUpTable[i][j].resize(5);
			for (int k = 0; k < 5; k++) {
				m_LookUpTable[i][j][k].resize(5);
				for (int l = 0; l < 5; l++) {
					m_LookUpTable[i][j][k][l].resize(5);
					for (int m = 0; m < 5; m++) {
						m_LookUpTable[i][j][k][l][m] = 0;
					}
				}
			}
		}
	}
	
	return true;
}
