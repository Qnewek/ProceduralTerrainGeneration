#pragma once

#include "glm/glm.hpp"

namespace noise
{
	enum class Options {
		REFIT_BASIC,
		FLATTEN_NEGATIVES,
		REVERT_NEGATIVES,
		RIDGE
	};

	enum class IslandType {
		CIRCLE,
		SQUARE
	};

	class SimplexNoiseClass
	{
	public:
		SimplexNoiseClass();
		~SimplexNoiseClass();

		static float perlin(glm::vec2 v);
		void generateFractalNoise(float* noiseMap, unsigned int mapWidth, unsigned int mapHeigth, 
								  float scale, int octaves, float constrast, float redistribution, 
								  float lacunarity, float persistance, float ridgeGain, float ridgeOffset,
								  Options option);
		void makeIsland(float* noiseMap, unsigned int mapWidth, unsigned int mapHeigth, IslandType islandType);
	private:
		static glm::vec2 randomGradient(int ix, int iy);
		static float dotGridGradient(int ix, int iy, float x, float y);
		static float interpolate(float a0, float a1, float w);
		static float ridge(float h, float offset, float gain);
	};
}
