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
		Biome(int _id, std::string _name);
		Biome(int _id, std::string _name, vec2 _temperatureLevel, vec2 _humidityLevel, vec2 _continentalnessLevel, vec2 _mountainousnessLevel, vec2 _weirdnessLevel, glm::vec3 _color, int _vegetationLevel);
		Biome(const Biome& b) = default;
		~Biome();

		bool IsSpecified() const;
		bool VerifyBiome(const int& T, const int& H, const int& C, const int& M, const int& W) const;
		
		int GetId() const { return id; };
		int GetVegetationLevel() const { return vegetationLevel; };
		std::string GetName() const { return name; };
		vec2 GetTemperatureLevel() const { return temperatureLevel; };
		vec2 GetHumidityLevel() const { return humidityLevel; };
		vec2 GetContinentalnessLevel() const { return continentalnessLevel; };
		vec2 GetMountainousnessLevel() const { return mountainousnessLevel; };
		glm::vec3 GetColor() const { return color; };
		
		void SetTemperatureLevel(vec2 temperatureLevel) { this->temperatureLevel = temperatureLevel; }
		void SetHumidityLevel(vec2 humidityLevel) { this->humidityLevel = humidityLevel; }
		void SetContinentalnessLevel(vec2 continentalnessLevel) { this->continentalnessLevel = continentalnessLevel; }
		void SetMountainousnessLevel(vec2 mountainousnessLevel) { this->mountainousnessLevel = mountainousnessLevel; }
		void SetWeirdnessLevel(vec2 weirdnessLevel) { this->weirdnessLevel = weirdnessLevel; }

	private:
		int id, vegetationLevel;
		std::string name;
		vec2 temperatureLevel,humidityLevel, continentalnessLevel, mountainousnessLevel, weirdnessLevel;
		glm::vec3 color = glm::vec3(0.0f, 0.0f, 0.0f);
	};
}