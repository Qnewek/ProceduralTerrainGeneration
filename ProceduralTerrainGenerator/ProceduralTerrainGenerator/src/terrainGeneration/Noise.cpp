#include "Noise.h"
#include "glm/glm.hpp"
#include <cmath>
#include <iostream>

#include <random>

#include "Simplex/SimplexNoise.h"

#define PI 3.14159265

namespace noise
{
	SimplexNoiseClass::SimplexNoiseClass()
	{
	}
	SimplexNoiseClass::~SimplexNoiseClass()
	{
	}
	void SimplexNoiseClass::generateFractalNoise(float* noiseMap, unsigned int mapWidth, unsigned int mapHeigth,
		float scale, int octaves, float constrast, float redistribution,
		float lacunarity, float persistance, float ridgeGain, float ridgeOffset,
		Options option)
	{
		float max = -1.0f;
		float amplitude;
		float frequency;
		float noiseHeight;
		float divider;
		for (int y = 0; y < mapHeigth; y++)
		{
			for (int x = 0; x < mapWidth; x++)
			{
				divider = 0.0f;
				amplitude = 1.0f;
				frequency = 1.0f;
				noiseHeight = 0.0f;

				for (int i = 0; i < octaves; i++)
				{
					noiseHeight += SimplexNoise::noise(x / (float)mapWidth * scale * frequency, y / (float)mapHeigth * scale * frequency) * amplitude;
					//Own implementation, generates rather small range of heights
					//noiseHeight += perlin(x / scale * frequency, y / scale * frequency) * amplitude;
					divider += amplitude;
					amplitude *= persistance;
					frequency *= lacunarity;
				}

				noiseHeight *= constrast;
				noiseHeight /= divider;

				if (noiseHeight < -1.0f) {
					noiseHeight = -1.0f;
				}
				else if (noiseHeight > 1.0f) {
					noiseHeight = 1.0f;
				}

				if (option == Options::REFIT_BASIC) {
					noiseHeight = (noiseHeight + 1.0f) / 2.0f;
				}
				else if (noiseHeight < 0.0f)
				{
					if (option == Options::FLATTEN_NEGATIVES)
					{
						noiseHeight = 0.0f;
					}
					else if (option == Options::REVERT_NEGATIVES)
					{
						noiseHeight = -noiseHeight;
					}
				}
				if (option == Options::RIDGE)
					noiseHeight = ridge(noiseHeight, ridgeOffset, ridgeGain);
				noiseHeight = std::pow(noiseHeight, redistribution);

				if (noiseHeight > max)
					max = noiseHeight;

				noiseMap[y * mapWidth + x] = noiseHeight;
			}
		}
		std::cout << "Noise successfully generated max: " << max << std::endl;
	}

	float SimplexNoiseClass::ridge(float h, float offset, float gain)
	{
		h = offset - fabs(h);
		h = h * h;
		return h * gain;
	}

	void SimplexNoiseClass::makeIsland(float* noiseMap, unsigned int mapWidth, unsigned int mapHeigth, IslandType islandType) {
		float max = -1.0f;
		float distance = 0;
		float halfWidth = mapWidth / 2.0f;
		float halfHeight = mapHeigth / 2.0f;
		float radius = halfWidth < halfHeight ? halfWidth : halfHeight;
		float nx, ny;
		radius *= 0.8f;
		for (int y = 0; y < mapHeigth; y++)
		{
			for (int x = 0; x < mapWidth; x++)
			{
				nx = 2 * x / mapWidth - 1;
				ny = 2 * y / mapHeigth - 1;
				distance = 1 - (1 - (nx * nx)) * (1 - (ny * ny);
				noiseMap[y * mapWidth + x] = std::lerp(noiseMap[y * mapWidth + x], 0.0f, distance / radius);
			}
		}
	}

	glm::vec2 SimplexNoiseClass::randomGradient(int ix, int iy) {
		const unsigned w = 8 * sizeof(unsigned);
		const unsigned s = w >> 1;
		unsigned a = ix, b = iy;

		a *= 3284157443;
		b ^= a << s | a >> w - s;
		b *= 1911520717;

		a ^= b << s | b >> w - s;
		a *= 2048419325;
		float random = a * (PI / ~(~0u >> 1));

		glm::vec2 v;
		v.x = sin(random);
		v.y = cos(random);

		return v;
	}

	float SimplexNoiseClass::dotGridGradient(int ix, int iy, float x, float y) {
		glm::vec2 gradient = randomGradient(ix, iy);

		float dx = x - (float)ix;
		float dy = y - (float)iy;

		return (dx * gradient.x + dy * gradient.y);
	}
	float SimplexNoiseClass::interpolate(float a0, float a1, float w)
	{
		return (a1 - a0) * (3.0 - w * 2.0) * w * w + a0;
	}

	float SimplexNoiseClass::perlin(glm::vec2 v) {
		int x0 = (int)v.x;
		int y0 = (int)v.y;
		int x1 = x0 + 1;
		int y1 = y0 + 1;

		float sx = v.x - (float)x0;
		float sy = v.y - (float)y0;

		float n0 = dotGridGradient(x0, y0, v.x, v.y);
		float n1 = dotGridGradient(x1, y0, v.x, v.y);
		float ix0 = interpolate(n0, n1, sx);

		n0 = dotGridGradient(x0, y1, v.x, v.y);
		n1 = dotGridGradient(x1, y1, v.x, v.y);
		float ix1 = interpolate(n0, n1, sx);

		float value = interpolate(ix0, ix1, sy);

		return value;
	}

}