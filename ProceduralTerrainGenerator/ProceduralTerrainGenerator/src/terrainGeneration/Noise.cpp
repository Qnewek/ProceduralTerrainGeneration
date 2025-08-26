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
	SimplexNoiseClass::SimplexNoiseClass()
		: config(NoiseConfigParameters()), width(1), height(1),
		heightMap(nullptr)
	{
		SimplexNoise::reseed(config.seed);
	}
	SimplexNoiseClass::~SimplexNoiseClass()
	{
		delete[] heightMap;
	}

	//Initializes the height map based on the width and height of the map
	void SimplexNoiseClass::InitMap()
	{
		if (width > 0 && height > 0) {
			if (heightMap)
				delete[] heightMap;
			heightMap = new float[width * height];
		}
		else {
			std::cout << "[ERROR] Map size must be greater than 0" << std::endl;
		}
	}

	//Sets the seed of the noise, if the seed is different than the current seed
	//Seeding in this case is performed by shuffling permutation table of the simplex noise
	//
	//@param seed - seed of the noise
	void SimplexNoiseClass::SetSeed(int seed) {
		if (seed != this->config.seed) {
			this->config.seed = seed;
			SimplexNoise::reseed(seed);
		}
	}

	void SimplexNoiseClass::Reseed()
	{
		SimplexNoise::reseed(this->config.seed);
	}
		
	//Sets the scale of the noise sampling, the higher the scale the more zoomed out the noise will be,
	//
	//@param scale - scale of the noise
	void SimplexNoiseClass::SetScale(float scale)
	{
		if (scale > 0.0f && scale != this->config.scale) {
			this->config.scale = scale;
		}
		else {
			std::cout << "[ERROR] Scale must be greater than 0" << std::endl;
		}
	}

	//Set the size of the map in chunks that the noise will be generated intoif, its not set manually
	//Default value for the map size is 1
	//
	//@param width - width of the map in chunks
	//@param height - height of the map in chunks
	void SimplexNoiseClass::SetMapSize(unsigned int width, unsigned int height)
	{
		if (width > 0 && height > 0 && (width != this->width || height != this->height)) {
			this->width = width;
			this->height = height;
			if (heightMap) {
				delete[] heightMap;
				heightMap = nullptr;
			}
		}
	}

	//Set the configuration parameters of the noise
	//
	//@param config - configuration parameters of the noise
	void SimplexNoiseClass::SetConfig(NoiseConfigParameters config)
	{
		this->config = config;
		if (config.seed != this->config.seed)
			SetSeed(config.seed);
	}

	//Function generating perlin noise based on the configuration parameters
	//Return a 2D height map of the noise in range for one configuration
	//
	//@return float* - 2D height map of the noise
	bool SimplexNoiseClass::GenerateFractalNoise()
	{
		if (heightMap == nullptr) {
			std::cout << "[ERROR] Height map not initialized" << std::endl;
			return false;
		}

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
						vec.x = (x / (float)width  * config.scale + config.xoffset) * frequency;
						vec.y = (y / (float)height * config.scale + config.yoffset) * frequency;

						elevation += SimplexNoise::noise(vec.x, vec.y) * amplitude;
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
				else if (elevation < 0.0f && config.option != Options::NOTHING)
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
				//Make Ridge noise
				if (config.Ridge)
					elevation = Ridge(elevation, config.RidgeOffset, config.RidgeGain);
				
				//Make island
				if (config.island) {
					elevation = std::fabsf(MakeIsland(elevation, x, y));
				}

				//Redistribute the noise
				elevation = std::pow(elevation, config.redistribution);

				heightMap[y * width + x] = elevation;
			}
		}
		std::cout << "[LOG] Noise successfully generated" << std::endl;
		return true;
	}

	//Function generating Ridge noise based on the configuration parameters
	//
	//@param h - height value
	//@param offset - offset value
	//@param gain - gain value
	float SimplexNoiseClass::Ridge(float h, float offset, float gain)
	{
		return offset - abs((gain * abs(h)) - gain + 1.0f);
	}

	//Function generating Ridged noise based on the configuration parameters
	//
	bool SimplexNoiseClass::MakeMapRidged()
	{
		if (!this->heightMap)
			return false;

		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				heightMap[y * width + x] = Ridge(heightMap[y * width + x], config.RidgeOffset, config.RidgeGain);
			}
		}
	}

	//Function generating island noise based on the configuration parameters
	//
	//@param e - elevation value
	//@param x - x coordinate
	//@param y - y coordinate
	float SimplexNoiseClass::MakeIsland(float e, int x, int y) {
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
}