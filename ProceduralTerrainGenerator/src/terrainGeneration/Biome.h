#pragma once

#include <vector>
#include <string>

#include "glm/glm.hpp"

#include "Noise.h"

namespace biome {
	class Biome
	{
	private:
		int id;
		float vegetationLevel;
		bool isSpecified = false;
		std::string name;
		glm::vec2 temperatureLevel,humidityLevel, continentalnessLevel, mountainousnessLevel, weirdnessLevel;
		glm::vec3 color = glm::vec3(0.0f, 0.0f, 0.0f);
	public:
		Biome();
		Biome(int _id, std::string _name);
		Biome(int _id, std::string _name, glm::vec2 _temperatureLevel, glm::vec2 _humidityLevel, glm::vec2 _continentalnessLevel, glm::vec2 _mountainousnessLevel, glm::vec2 _weirdnessLevel, glm::vec3 _color, int _vegetationLevel);
		Biome(const Biome& b) = default;
		~Biome();

		bool IsSpecified() const { return isSpecified; };
		bool VerifyBiome(const int& T, const int& H, const int& C, const int& M, const int& W) const;
		
		int GetId() const { return id; };
		float GetVegetationLevel() const { return vegetationLevel; };
		std::string GetName() const { return name; };
		glm::vec2 GetTemperatureLevel() const { return temperatureLevel; };
		glm::vec2 GetHumidityLevel() const { return humidityLevel; };
		glm::vec2 GetContinentalnessLevel() const { return continentalnessLevel; };
		glm::vec2 GetMountainousnessLevel() const { return mountainousnessLevel; };
		glm::vec3 GetColor() const { return color; };
		
		void SetTemperatureLevel(glm::vec2 temperatureLevel) { this->temperatureLevel = temperatureLevel; }
		void SetHumidityLevel(glm::vec2 humidityLevel) { this->humidityLevel = humidityLevel; }
		void SetContinentalnessLevel(glm::vec2 continentalnessLevel) { this->continentalnessLevel = continentalnessLevel; }
		void SetMountainousnessLevel(glm::vec2 mountainousnessLevel) { this->mountainousnessLevel = mountainousnessLevel; }
		void SetWeirdnessLevel(glm::vec2 weirdnessLevel) { this->weirdnessLevel = weirdnessLevel; }

	};
}