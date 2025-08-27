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
		: config(NoiseConfigParameters()), width(0), height(0),
		heightMap(nullptr)
	{
	}
	SimplexNoiseClass::~SimplexNoiseClass()
	{
		delete[] heightMap;
	}

	//Initialize the size of the map that the noise will be generated into
	//@param _width - width of the map
	//@param _height - height of the
	bool SimplexNoiseClass::Initialize(int _height, int _width) {
		if (heightMap) {
			std::cout << "[ERROR] Noise map already initialized\n";
			return false;
		}
		return Resize(_height, _width);
	}

	//Resize the size og the noise map
	//@param _width - width of the map
	//@param _height - height of the map
	bool SimplexNoiseClass::Resize(int _height, int _width)
	{
		if (_height <= 0 || _width <= 0) {
			std::cout << "[ERROR] Noise map couldnt be resized, width and height must be greater than 0\n";
			return false;
		}
		if (_width == this->width && _height == this->height) {
			std::cout << "[LOG] Noise map is already initialized with the same size\n";
			return false;
		}

		width = _width;
		height = _height;
		if (heightMap) {
			delete[] heightMap;
		}
		heightMap = new float[width * height];
		std::cout << "[LOG] Noise object has been succesfully initialized with size: " << height << "x" << width << "\n";
		return true;
	}

	//Function generating perlin noise based on the configuration parameters
	//Return a 2D height map of the noise in range for one configuration
	//
	//@return float* - 2D height map of the noise
	bool SimplexNoiseClass::GenerateFractalNoise()
	{
		if (!heightMap) {
			std::cout << "[ERROR] Noise object not initialized!" << std::endl;
			return false;
		}
		SimplexNoise::reseed(this->config.seed);

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
						float anglex = TAU * (x / (float)config.resolution);
						float angley = TAU * (y / (float)config.resolution);

						elevation += SimplexNoise::noise(std::cosf(anglex) / TAU * config.scale * frequency + config.xoffset, 
														 std::sinf(anglex) / TAU * config.scale * frequency + config.xoffset,
														 std::cosf(angley) / TAU * config.scale * frequency + config.yoffset,
														 std::sinf(angley) / TAU * config.scale * frequency + config.yoffset)  * amplitude;
					}
					else {
						vec.x = (x / (float)config.resolution * config.scale + config.xoffset) * frequency;
						vec.y = (y / (float)config.resolution * config.scale + config.yoffset) * frequency;

						elevation += SimplexNoise::noise(vec.x, vec.y) * amplitude;
					}
					divider += amplitude;
					amplitude *= config.persistance;
					frequency *= config.lacunarity;
				}

				elevation *= config.constrast;
				elevation /= divider;

				elevation = std::clamp(elevation, -1.0f, 1.0f);

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
	float SimplexNoiseClass::GetVal(int x, int y)
	{
		if(!heightMap) {
			std::cout << "[ERROR] Noise object not initialized!" << std::endl;
			return -2.0f;
		}
		if(x < 0 || x >= width || y < 0 || y >= height) {
			std::cout << "[ERROR] Coordinates out of bounds!" << std::endl;
			return -2.0f;
		}
		return heightMap[y * width + x];
	}
}