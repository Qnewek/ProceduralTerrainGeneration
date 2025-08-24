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

		bool IsSpecified() const;
		bool VerifyBiome(const int& T, const int& H, const int& C, const int& M) const;
		
		int GetId() const { return id; };
		int GetVegetationLevel() const { return vegetationLevel; };
		int GetTexOffset() const { return texOffset; };
		int& GetIdRef() { return id; }
		int& GetVegetationLevelRef() { return vegetationLevel; }
		int& GetTexOffsetRef() { return texOffset; }
		vec2 GetTemperatureLevel() const { return temperatureLevel; };
		vec2 GetHumidityLevel() const { return humidityLevel; };
		vec2 GetContinentalnessLevel() const { return continentalnessLevel; };
		vec2 GetMountainousnessLevel() const { return mountainousnessLevel; };
		vec2& GetTemperatureLevelRef() { return temperatureLevel; }
		vec2& GetHumidityLevelRef() { return humidityLevel; }
		vec2& GetContinentalnessLevelRef() { return continentalnessLevel; }
		vec2& GetMountainousnessLevelRef() { return mountainousnessLevel; }
		std::vector<vec2>& GetTreeTypesRef() { return treeTypes; }
		std::string GetName() const { return name; };
		std::string& GetNameRef() { return name; }
		
		void SetTemperatureLevel(vec2 temperatureLevel) { this->temperatureLevel = temperatureLevel; }
		void SetHumidityLevel(vec2 humidityLevel) { this->humidityLevel = humidityLevel; }
		void SetContinentalnessLevel(vec2 continentalnessLevel) { this->continentalnessLevel = continentalnessLevel; }
		void SetMountainousnessLevel(vec2 mountainousnessLevel) { this->mountainousnessLevel = mountainousnessLevel; }

	private:
		int id, texOffset, vegetationLevel;
		std::string name;
		vec2 temperatureLevel,humidityLevel, continentalnessLevel, mountainousnessLevel;
		std::vector<vec2> treeTypes = {{0,100}};
	};
}