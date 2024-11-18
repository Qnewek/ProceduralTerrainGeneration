#include "Biome.h"

Biome::Biome() : m_id(-1), m_TemperatureLevel({ -1, -1 }), m_HumidityLevel({ -1, -1 }), m_ContinentalnessLevel({ -1, -1 }), m_MountainousnessLevel({ -1, -1 }), m_VariantLevel({ -1, -1 })
{
}

Biome::Biome(int id) : m_id(id), m_TemperatureLevel({ -1, -1 }), m_HumidityLevel({ -1, -1 }), m_ContinentalnessLevel({ -1, -1 }), m_MountainousnessLevel({ -1, -1 }), m_VariantLevel({ -1, -1 })
{
	this->m_MapGenerationConfig.option = noise::Options::NOTHING;
	this->m_TextureGenerationConfig.option = noise::Options::NOTHING;
}

Biome::~Biome()
{
	m_TextureLevels.clear();
}

bool Biome::isSpecified() const{
	return m_TemperatureLevel.x != -1 && m_HumidityLevel.x != -1 && 
		   m_ContinentalnessLevel.x != -1 && m_MountainousnessLevel.x != -1 && m_VariantLevel.x != -1;
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

vec2 Biome::getVariantLevel() const
{
	return m_VariantLevel;
}

int Biome::getTextureLevel(float value) const
{
	for (const auto& range : m_TextureLevels) {
		if (value >= range.min && value <= range.max) {
			return range.rangedLevel;
		}
	}
	return -1;
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

void Biome::setVariantLevel(vec2 variantLevel)
{
	m_VariantLevel = variantLevel;
}

void Biome::addTextureLevel(float min, float max, int textureLevel)
{
	m_TextureLevels.push_back({ min, max, textureLevel });
}

void Biome::setGenerationConfigManually(int seed, float scale, int octaves, float constrast, float redistribution, float lacunarity, float persistance, float revertGain, noise::Options option, bool island, float mixPower, noise::IslandType islandType)
{
	m_MapGenerationConfig.seed = seed;
	m_MapGenerationConfig.scale = scale;
	m_MapGenerationConfig.octaves = octaves;
	m_MapGenerationConfig.constrast = constrast;
	m_MapGenerationConfig.redistribution = redistribution;
	m_MapGenerationConfig.lacunarity = lacunarity;
	m_MapGenerationConfig.persistance = persistance;
	m_MapGenerationConfig.revertGain = revertGain;
	m_MapGenerationConfig.option = option;
	m_MapGenerationConfig.island = island;
	m_MapGenerationConfig.mixPower = mixPower;
	m_MapGenerationConfig.islandType = islandType;
}

void Biome::setTextureGenerationConfigManually(int seed, float scale, int octaves, float constrast, float redistribution, float lacunarity, float persistance, float revertGain, noise::Options option, bool island, float mixPower, noise::IslandType islandType)
{
	m_TextureGenerationConfig.seed = seed;
	m_TextureGenerationConfig.scale = scale;
	m_TextureGenerationConfig.octaves = octaves;
	m_TextureGenerationConfig.constrast = constrast;
	m_TextureGenerationConfig.redistribution = redistribution;
	m_TextureGenerationConfig.lacunarity = lacunarity;
	m_TextureGenerationConfig.persistance = persistance;
	m_TextureGenerationConfig.revertGain = revertGain;
	m_TextureGenerationConfig.option = option;
	m_TextureGenerationConfig.island = island;
	m_TextureGenerationConfig.mixPower = mixPower;
	m_TextureGenerationConfig.islandType = islandType;
}

bool Biome::prepareNoise(noise::SimplexNoiseClass& noise, int i) {
	if (i >= 2)
		return false;
	if (i == 0) {
		noise.setConfig(m_MapGenerationConfig);
	}
	else if (i == 1) {
		noise.setConfig(m_TextureGenerationConfig);
	}
	return true;
}
