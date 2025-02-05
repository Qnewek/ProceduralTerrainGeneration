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

		int GetId() const;
		int GetVegetationLevel() const { return vegetationLevel; };
		int GetTexOffset() const;
		vec2 GetTemperatureLevel() const;
		vec2 GetHumidityLevel() const;
		vec2 GetContinentalnessLevel() const;
		vec2 GetMountainousnessLevel() const;
		std::string GetName() const;

		int& GetIdRef() { return id; }
		int& GetVegetationLevelRef() { return vegetationLevel; }
		int& GetTexOffsetRef() { return texOffset; }
		vec2& GetTemperatureLevelRef() { return temperatureLevel; }
		vec2& GetHumidityLevelRef() { return humidityLevel; }
		vec2& GetContinentalnessLevelRef() { return continentalnessLevel; }
		vec2& GetMountainousnessLevelRef() { return mountainousnessLevel; }
		std::string& GetNameRef() { return name; }
		std::vector<vec2>& GetTreeTypesRef() { return treeTypes; }

		void SetTemperatureLevel(vec2 temperatureLevel);
		void SetHumidityLevel(vec2 humidityLevel);
		void SetContinentalnessLevel(vec2 continentalnessLevel);
		void SetMountainousnessLevel(vec2 mountainousnessLevel);

		bool IsSpecified() const;
		bool VerifyBiome(const int& T, const int& H, const int& C, const int& M) const;
	private:
		int id;
		int texOffset;
		std::string name;
		
		vec2 temperatureLevel;
		vec2 humidityLevel;
		vec2 continentalnessLevel;
		vec2 mountainousnessLevel;

		int vegetationLevel;
		std::vector<vec2> treeTypes = {{0,100}};
	};
}