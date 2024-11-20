#include "TerrainGenerator.h"

#include <iostream>
#include <math.h>

TerrainGenerator::TerrainGenerator() : width(0), height(0), heightMap(nullptr), seed(0), chunkResolution(0),
continentalnessNoise(), mountainousNoise(), PVNoise(), continentalnessSpline(), mountainousSpline(), PVSpline(), seeLevel(64.0f)
{
	mountainousNoise.getConfigRef().option = noise::Options::NOTHING;
	continentalnessNoise.getConfigRef().option = noise::Options::NOTHING;
	PVNoise.getConfigRef().option = noise::Options::NOTHING;
	PVNoise.getConfigRef().ridge = true;
}

TerrainGenerator::~TerrainGenerator()
{
	delete[] heightMap;
}


bool TerrainGenerator::initializeMap()
{
	if (width <= 0 || height <= 0 || chunkResolution <= 0)
		return false;

	if (heightMap != nullptr)
		delete[] heightMap;
	
	heightMap = new float[width * chunkResolution * height * chunkResolution];
}

bool TerrainGenerator::setSize(int width, int height)
{
	if (width <= 0 || height <= 0) {
		std::cout << "[ERROR] width and height must be greater than 0" << std::endl;
	}

	this->width = width;
	this->height = height;

	this->mountainousNoise.setMapSize(width, height);
	this->continentalnessNoise.setMapSize(width, height);
	this->PVNoise.setMapSize(width, height);

	return true;
}

void TerrainGenerator::setSeed(int seed)
{
	this->seed = seed;
}

bool TerrainGenerator::setSeeLevel(float seeLevel)
{
	if (seeLevel < 0)
		return false;

	this->seeLevel = seeLevel;

	return true;
}

bool TerrainGenerator::setChunkResolution(int resolution)
{
	if (resolution <= 0)
		return false;

	this->chunkResolution = resolution;

	this->mountainousNoise.setChunkSize(chunkResolution, chunkResolution);
	this->continentalnessNoise.setChunkSize(chunkResolution, chunkResolution);
	this->PVNoise.setChunkSize(chunkResolution, chunkResolution);

	return true;
}

void TerrainGenerator::setContinentalnessNoiseConfig(noise::NoiseConfigParameters config)
{
	continentalnessNoise.setConfig(config);
}

void TerrainGenerator::setMountainousNoiseConfig(noise::NoiseConfigParameters config)
{
	mountainousNoise.setConfig(config);
}

void TerrainGenerator::setPVNoiseConfig(noise::NoiseConfigParameters config)
{
	PVNoise.setConfig(config);
}

bool TerrainGenerator::setSplines(std::vector<std::vector<double>> splines)
{
	if (splines.size() <= 5)
		return false;

	continentalnessSpline.set_points(splines[0], splines[1]);
	mountainousSpline.set_points(splines[2], splines[3]);
	PVSpline.set_points(splines[4], splines[5]);

	return true;
}

float* TerrainGenerator::getHeightMap()
{
	return heightMap;
}

noise::NoiseConfigParameters& TerrainGenerator::getContinentalnessNoiseConfig()
{
	return continentalnessNoise.getConfigRef();
}

noise::NoiseConfigParameters& TerrainGenerator::getMountainousNoiseConfig()
{
	return mountainousNoise.getConfigRef();
}

noise::NoiseConfigParameters& TerrainGenerator::getPVNoiseConfig()
{
	return PVNoise.getConfigRef();
}

bool TerrainGenerator::generateHeightMap()
{
	if (!heightMap || width <= 0 || height <= 0 || chunkResolution <= 0) {
		std::cout << "[ERROR] HeightMap not initialized" << std::endl;
		return false;
	}

	continentalnessNoise.setSeed(seed);
	continentalnessNoise.initMap();
	continentalnessNoise.generateFractalNoiseByChunks();

	mountainousNoise.setSeed(seed/2);
	mountainousNoise.initMap();
	mountainousNoise.generateFractalNoiseByChunks();

	PVNoise.setSeed(seed/3);
	PVNoise.initMap();
	PVNoise.generateFractalNoiseByChunks();

	std::cout << "[LOG] Interpolating heightMap" << std::endl;

	float continentalness = 0.0f;
	float mountainous = 0.0f;
	float PV = 0.0f;
	float elevation = 0.0f;

	for (int y = 0; y < height * chunkResolution; y++) {
		for (int x = 0; x < width * chunkResolution; x++) {
			continentalness = continentalnessNoise.getVal(x, y);
			mountainous = mountainousSpline(mountainousNoise.getVal(x, y));
			//PV = PVSpline(PVNoise.getVal(x, y));

			if (continentalness >= -0.2 && continentalness  <= 0.0) {
				mountainous *= 0.0;
			}
			else if (continentalness > 0.0) {
				mountainous *= continentalness;
			}
			else
			{
				mountainous *= -(continentalness + 0.2) / 25;
			}

			elevation = continentalnessSpline(continentalness) + mountainous;

			heightMap[y * width * chunkResolution + x] = elevation;
		}
	}
	
	std::cout << "[LOG] HeightMap interpolated " << std::endl;
	return true;
}