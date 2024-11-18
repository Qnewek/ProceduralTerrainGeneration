#pragma once

#include <vector>

#include "glm/glm.hpp"

#include "Noise.h"

struct vec2 {
	int x, y;
};

class Biome 
{
public:
	Biome();
	Biome(int id);
	~Biome();
	
	bool isSpecified() const;
	bool prepareNoise(noise::SimplexNoiseClass& noise, int i);

	vec2 getTemperatureLevel() const;
	vec2 getHumidityLevel() const;
	vec2 getContinentalnessLevel() const;
	vec2 getMountainousnessLevel() const;
	vec2 getVariantLevel() const;
	int getTextureLevel(float value) const;
	
	void setTemperatureLevel(vec2 temperatureLevel);
	void setHumidityLevel(vec2 humidityLevel);
	void setContinentalnessLevel(vec2 continentalnessLevel);
	void setMountainousnessLevel(vec2 mountainousnessLevel);
	void setVariantLevel(vec2 variantLevel);

	void addTextureLevel(float min, float max, int textureLevel);
	void setGenerationConfigManually(int seed, float scale, int octaves, float constrast, float redistribution, float lacunarity, float persistance, float revertGain, noise::Options option, bool island, float mixPower, noise::IslandType islandType);
	void setTextureGenerationConfigManually(int seed, float scale, int octaves, float constrast, float redistribution, float lacunarity, float persistance, float revertGain, noise::Options option, bool island, float mixPower, noise::IslandType islandType);

private:
	int m_id;
	vec2 m_TemperatureLevel;
	vec2 m_HumidityLevel;
	vec2 m_ContinentalnessLevel;
	vec2 m_MountainousnessLevel;
	vec2 m_VariantLevel;

	struct RangedLevel {
		float min;
		float max;
		int rangedLevel;
	};

	std::vector<RangedLevel> m_TextureLevels;

	noise::NoiseConfigParameters m_MapGenerationConfig, m_TextureGenerationConfig;
};