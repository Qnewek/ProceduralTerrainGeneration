#pragma once
namespace noise
{
	enum class Options {
		NOTHING,
		FLATTEN_NEGATIVES,
		REVERT_NEGATIVES,
		REFIT_BASIC
	};
	typedef struct {
		float x;
		float y;
	} vec2;

	void getNoiseMap(float* noiseMap, unsigned int mapWidth, unsigned int mapHeigth, float scale, int octaves, float constrast, float redistribution, Options option);
	vec2 randomGradient(int ix, int iy);
	float dotGridGradient(int ix, int iy, float x, float y);
	float perlin(float x, float y);
	float interpolate(float a0, float a1, float w);
}
