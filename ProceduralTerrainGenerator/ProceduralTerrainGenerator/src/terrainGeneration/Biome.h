#pragma once

#include <vector>
#include <string>

#include "glm/glm.hpp"

#include "Noise.h"

namespace biome {
	struct vec2 {
		int x, y;

		vec2() : x(-1), y(-1) {}
		vec2(int x, int y) : x(x), y(y) {}
		vec2(const vec2& v) : x(v.x), y(v.y) {}
	};

	class Biome
	{
	public:
		Biome();
		Biome(int id, std::string name);
		Biome(int id, std::string name, vec2 temperatureLevel, vec2 humidityLevel, vec2 continentalnessLevel, vec2 mountainousnessLevel, int texOffset, int vegetationLevel);
		Biome(const Biome& b) = default;
		~Biome();

		vec2 getTemperatureLevel() const;
		vec2 getHumidityLevel() const;
		vec2 getContinentalnessLevel() const;
		vec2 getMountainousnessLevel() const;
		int getId() const;
		int getVegetationLevel() const { return vegetationLevel; };
		std::string getName() const;
		int getTexOffset() const;

		void setTemperatureLevel(vec2 temperatureLevel);
		void setHumidityLevel(vec2 humidityLevel);
		void setContinentalnessLevel(vec2 continentalnessLevel);
		void setMountainousnessLevel(vec2 mountainousnessLevel);

		bool isSpecified() const;
		bool verifyBiome(const int& T, const int& H, const int& C, const int& M) const;
	private:
		int m_id;
		int texOffset;
		std::string m_Name;
		int vegetationLevel;
		vec2 m_TemperatureLevel;
		vec2 m_HumidityLevel;
		vec2 m_ContinentalnessLevel;
		vec2 m_MountainousnessLevel;
	};
}