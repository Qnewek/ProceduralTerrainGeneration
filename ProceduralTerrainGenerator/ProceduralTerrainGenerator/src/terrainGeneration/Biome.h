#pragma once

#include <vector>
#include <string>

#include "glm/glm.hpp"

#include "Noise.h"

struct vec2 {
	int x, y;
};

class Biome
{
public:
	Biome();
	Biome(int id, std::string name);
	Biome(int id, std::string name, vec2 temperatureLevel, vec2 humidityLevel, vec2 continentalnessLevel, vec2 mountainousnessLevel);
	Biome(const Biome& b) = default;
	~Biome();

	vec2 getTemperatureLevel() const;
	vec2 getHumidityLevel() const;
	vec2 getContinentalnessLevel() const;
	vec2 getMountainousnessLevel() const;
	const int& getId() const;
	const std::string& getName() const;

	void setTemperatureLevel(vec2 temperatureLevel);
	void setHumidityLevel(vec2 humidityLevel);
	void setContinentalnessLevel(vec2 continentalnessLevel);
	void setMountainousnessLevel(vec2 mountainousnessLevel);

	bool isSpecified() const;
	bool verifyBiome(const int& T, const int& H, const int& C, const int& M) const;
private:
	int m_id;
	std::string m_Name;
	vec2 m_TemperatureLevel;
	vec2 m_HumidityLevel;
	vec2 m_ContinentalnessLevel;
	vec2 m_MountainousnessLevel;
};