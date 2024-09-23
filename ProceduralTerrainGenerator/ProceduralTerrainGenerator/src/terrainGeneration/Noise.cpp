#include "Noise.h"
#include "glm/glm.hpp"
#include <cmath>
#include <iostream>

#include <algorithm>
#include <random>

#include "Simplex/SimplexNoise.h"

#define PI 3.14159265

namespace noise
{
	SimplexNoiseClass::SimplexNoiseClass(unsigned int width, unsigned int height)
		: config(NoiseConfigParameters()), width(width), height(height)
	{
		heightMap = new float[width * height];
	}
	SimplexNoiseClass::~SimplexNoiseClass()
	{
		delete[] heightMap;
	}
	void SimplexNoiseClass::setSeed(int seed) {
		if (seed != 0 && seed != this->config.seed) {
			this->config.seed = seed;
			SimplexNoise::reseed(seed);
		}
	}
	void SimplexNoiseClass::generateFractalNoise()
	{
		float amplitude;
		float frequency;
		float elevation;
		float divider;
		glm::vec2 vec = glm::vec2(0.0f, 0.0f);

		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				divider = 0.0f;
				amplitude = 1.0f;
				frequency = 1.0f;
				elevation = 0.0f;

				for (int i = 0; i < config.octaves; i++)
				{
					if (this->config.symmetrical) {
						float TAU = 2 * std::_Pi_val;
						float anglex = TAU * (x / (float)width);
						float angley = TAU * (y / (float)height);

						elevation += SimplexNoise::noise(std::cosf(anglex) / TAU * config.scale * frequency + config.xoffset, 
														 std::sinf(anglex) / TAU * config.scale * frequency + config.xoffset,
														 std::cosf(angley) / TAU * config.scale * frequency + config.yoffset,
														 std::sinf(angley) / TAU * config.scale * frequency + config.yoffset)  * amplitude;
					}
					else {
						vec.x = (x / (float)width  * config.scale * frequency) + config.xoffset;
						vec.y = (y / (float)height * config.scale * frequency) + config.yoffset;

						elevation += SimplexNoise::noise(vec.x, vec.y) * amplitude;
						//elevation += perlin(vec) * amplitude;
					}
					divider += amplitude;
					amplitude *= config.persistance;
					frequency *= config.lacunarity;
				}

				elevation *= config.constrast;
				elevation /= divider;

				//Clipping values to be in range -1.0f and 1.0f
				if (elevation < -1.0f) {
					elevation = -1.0f;
				}
				else if (elevation > 1.0f) {
					elevation = 1.0f;
				}

				//Dealing with negatives
				if (config.option == Options::REFIT_ALL) {
					elevation = (elevation + 1.0f) / 2.0f;
				}
				else if (elevation < 0.0f)
				{
					if (config.option == Options::FLATTEN_NEGATIVES)
					{
						elevation = 0.0f;
					}
					else if (config.option == Options::REVERT_NEGATIVES)
					{
						elevation = -(elevation * config.revertGain);
					}
				}
				//Make ridge noise
				if (config.ridge)
					elevation = ridge(elevation, config.ridgeOffset, config.ridgeGain);
				
				//Make island
				if (config.island) {
					elevation = std::fabsf(makeIsland(elevation, x, y));
				}

				//Redistribute the noise
				elevation = std::pow(elevation, config.redistribution);

				heightMap[y * width + x] = elevation;
			}
		}
		std::cout << "Noise successfully generated" << std::endl;
	}

	float SimplexNoiseClass::ridge(float h, float offset, float gain)
	{
		h = offset - fabs(h);
		h = h * h;
		return h * gain;
	}

	float SimplexNoiseClass::makeIsland(float e, int x, int y) {
		float nx = x * 2 / (float)width  -1;
		float ny = y * 2 / (float)height -1;
		float distance = 0;
		if (config.islandType == IslandType::CONE)
			distance = sqrt((nx * nx) + (ny * ny));
		else if (config.islandType == IslandType::DIAGONAL)
			distance = std::max(fabs(nx), fabs(ny));
		else if (config.islandType == IslandType::EUCLIDEAN_SQUARED){
			distance = std::min(1.0f, ((nx * nx) + (ny * ny)) / std::sqrtf(2.0f));
		}
		else if (config.islandType == IslandType::SQUARE_BUMP) {
			distance = 1 - ((1-(nx * nx))*(1-(ny * ny)));
		}
		else if (config.islandType == IslandType::HYPERBOLOID) {
			distance = sqrt((nx * nx) + (ny * ny) + (0.5 * 0.5));
		}
		else if (config.islandType == IslandType::SQUIRCLE) {
			distance = sqrt(std::powf(nx,4) + std::powf(ny,4));
		}
		else if (config.islandType == IslandType::TRIG) {
			distance = 1 - (cos(nx * (std::_Pi_val / 2)) * cos(ny * (std::_Pi_val / 2)));
		}
		return std::lerp(e, 1 - distance, config.mixPower);
	}


	//Implementation based on pseudocode and definition from https://en.wikipedia.org/wiki/Perlin_noise
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