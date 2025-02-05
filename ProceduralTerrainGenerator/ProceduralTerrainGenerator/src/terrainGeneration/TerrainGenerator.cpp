#include "TerrainGenerator.h"

#include <iostream>
#include <math.h>

#include "PoissonSampling/PoissonGenerator.h"

TerrainGenerator::TerrainGenerator() : width(0), height(0), seed(0), chunkResolution(0),
heightMap(nullptr), biomeMap(nullptr), biomeMapPerChunk(nullptr),
continentalnessNoise(), mountainousNoise(), PVNoise(), continentalnessSpline(), mountainousSpline(), PVSpline(),
seeLevel(64.0f), biomeGen()
{
	mountainousNoise.GetConfigRef().option = noise::Options::NOTHING;
	continentalnessNoise.GetConfigRef().option = noise::Options::NOTHING;
	PVNoise.GetConfigRef().option = noise::Options::NOTHING;
	PVNoise.GetConfigRef().Ridge = true;
}

TerrainGenerator::~TerrainGenerator()
{
	if(heightMap)
		delete[] heightMap;
	if (biomeMap)
		delete[] biomeMap;
	if(biomeMapPerChunk)
		delete[] biomeMapPerChunk;
}

bool TerrainGenerator::InitializeMap()
{
	if (width <= 0 || height <= 0 || chunkResolution <= 0)
		return false;

	if (heightMap)
		delete[] heightMap;

	heightMap = new float[width * chunkResolution * height * chunkResolution];

	return true;
}

bool TerrainGenerator::InitializeBiomeMap()
{
	if (width <= 0 || height <= 0 || chunkResolution <= 0)
		return false;

	if (biomeMap)
		delete[] biomeMap;

	biomeMap = new int[width * chunkResolution * height * chunkResolution];

	return true;
}

bool TerrainGenerator::SetSize(int width, int height)
{
	if (width <= 0 || height <= 0) {
		std::cout << "[ERROR] width and height must be greater than 0" << std::endl;
	}

	this->width = width;
	this->height = height;

	this->mountainousNoise.SetMapSize(width, height);
	this->continentalnessNoise.SetMapSize(width, height);
	this->PVNoise.SetMapSize(width, height);

	return true;
}

void TerrainGenerator::SetSeed(int seed)
{
	this->seed = seed;
}

bool TerrainGenerator::SetSeeLevel(float seeLevel)
{
	if (seeLevel < 0)
		return false;

	this->seeLevel = seeLevel;

	return true;
}

bool TerrainGenerator::SetChunkResolution(int resolution)
{
	if (resolution <= 0)
		return false;

	this->chunkResolution = resolution;

	this->mountainousNoise.SetChunkSize(chunkResolution, chunkResolution);
	this->continentalnessNoise.SetChunkSize(chunkResolution, chunkResolution);
	this->PVNoise.SetChunkSize(chunkResolution, chunkResolution);

	return true;
}

void TerrainGenerator::SetContinentalnessNoiseConfig(noise::NoiseConfigParameters config)
{
	continentalnessNoise.SetConfig(config);
}

void TerrainGenerator::SetMountainousNoiseConfig(noise::NoiseConfigParameters config)
{
	mountainousNoise.SetConfig(config);
}

void TerrainGenerator::SetPVNoiseConfig(noise::NoiseConfigParameters config)
{
	PVNoise.SetConfig(config);
}

bool TerrainGenerator::SetSplines(std::vector<std::vector<double>> splines)
{
	if (splines.size() <= 5)
		return false;
	continentalnessSpline.set_points(splines[0], splines[1]);
	mountainousSpline.set_points(splines[2], splines[3]);
	PVSpline.set_points(splines[4], splines[5]);

	return true;
}

bool TerrainGenerator::SetSpline(char c, std::vector<std::vector<double>> spline)
{
	switch (c)
	{
	case 'c':
		continentalnessSpline.set_points(spline[0], spline[1]);
		break;
	case 'm':
		mountainousSpline.set_points(spline[0], spline[1]);
		break;
	case 'p':
		PVSpline.set_points(spline[0], spline[1]);
		break;
	default:
		return false;
		break;
	}
	return true;
}

bool TerrainGenerator::SetBiomes(std::vector<biome::Biome>& biomes)
{
	if (!biomeGen.SetBiomes(biomes))
	{
		std::cout << "[ERROR] Biomes couldnt be set" << std::endl;
		return false;
	}
	return true;
}

bool TerrainGenerator::SetRanges(std::vector<std::vector<RangedLevel>>& ranges)
{
	if (ranges.size() != 4)
		return false;

	biomeGen.SetRanges(ranges);

	return true;
}

bool TerrainGenerator::SetRange(char c, std::vector<RangedLevel> range)
{
	return biomeGen.SetRange(c, range);
}

float* TerrainGenerator::GetHeightMap()
{
	return heightMap;
}

int* TerrainGenerator::GetBiomeMap()
{
	return biomeMap;
}

float TerrainGenerator::GetHeightAt(int x, int y)
{
	if (!heightMap)
		return -1.0f;
	return heightMap[y * width * chunkResolution + x];
}

biome::Biome& TerrainGenerator::GetBiome(int id)
{
	return biomeGen.GetBiome(id);
}

int TerrainGenerator::GetBiomeAt(int x, int y)
{
	if (!biomeMap)
		return -1;	
	return biomeMap[y * width * chunkResolution + x];
}

noise::NoiseConfigParameters& TerrainGenerator::GetContinentalnessNoiseConfig()
{
	return continentalnessNoise.GetConfigRef();
}

noise::NoiseConfigParameters& TerrainGenerator::GetMountainousNoiseConfig()
{
	return mountainousNoise.GetConfigRef();
}

noise::NoiseConfigParameters& TerrainGenerator::GetPVNoiseConfig()
{
	return PVNoise.GetConfigRef();
}

noise::NoiseConfigParameters& TerrainGenerator::GetTemperatureNoiseConfig()
{
	return biomeGen.GetTemperatureNoiseConfig();
}

noise::NoiseConfigParameters& TerrainGenerator::GetHumidityNoiseConfig()
{
	return biomeGen.GetHumidityNoiseConfig();
}

bool TerrainGenerator::GenerateHeightMap()
{
	if (!heightMap || width <= 0 || height <= 0 || chunkResolution <= 0) {
		std::cout << "[ERROR] HeightMap not initialized" << std::endl;
		return false;
	}

	continentalnessNoise.InitMap();
	continentalnessNoise.Reseed();
	continentalnessNoise.GenerateFractalNoiseByChunks();

	mountainousNoise.InitMap();
	mountainousNoise.Reseed();
	mountainousNoise.GenerateFractalNoiseByChunks();

	PVNoise.InitMap();
	PVNoise.Reseed();
	PVNoise.GenerateFractalNoiseByChunks();

	std::cout << "[LOG] Evaluating heightMap..." << std::endl;

	float continentalness = 0.0f;
	float mountainous = 0.0f;
	float PV = 0.0f;
	float elevation = 0.0f;

	for (int y = 0; y < height * chunkResolution; y++) {
		for (int x = 0; x < width * chunkResolution; x++) {
			continentalness = continentalnessNoise.GetVal(x, y);
			mountainous = mountainousSpline(mountainousNoise.GetVal(x, y));
			PV = PVSpline(PVNoise.GetVal(x, y));

			if (continentalness >= -0.2 && continentalness <= 0.0)
				mountainous *= 0.0;
			else if (continentalness > 0.0)
				mountainous *= continentalness;
			else
				mountainous *= -(continentalness + 0.2) / 25;

			mountainous -= mountainous * PV;
			elevation = continentalnessSpline(continentalness) + mountainous - (PV * 20.0f);
			heightMap[y * width * chunkResolution + x] = elevation;
		}
	}

	std::cout << "[LOG] HeightMap succesfully evaluated " << std::endl;
	return true;
}

bool TerrainGenerator::GenerateBiomes()
{
	if (!InitializeBiomeMap()) {
		return false;
	}

	if (!biomeGen.Biomify(heightMap ,biomeMap, width, height, chunkResolution, seed, continentalnessNoise, mountainousNoise)) {
		return false;
	}

	return true;
}

bool TerrainGenerator::PerformTerrainGeneration()
{
	if (!GenerateHeightMap())
	{
		std::cout << "[ERROR] HeightMap couldnt be generated" << std::endl;
		return false;
	}

	if (!GenerateBiomes())
	{
		std::cout << "[ERROR] Biomes couldnt be generated" << std::endl;
		return false;
	}
	if (!GenerateBiomeMapPerChunk())
	{
		std::cout << "[ERROR] BiomeMapPerChunk couldnt be generated" << std::endl;
		return false;
	}
	return true;
}

bool TerrainGenerator::VegetationGeneration()
{
	if (!biomeMap || !biomeMapPerChunk) {
		std::cout << "[ERROR] BiomeMap or BiomeMapPerChunk not initialized" << std::endl;
		return false;
	}

	PoissonGenerator::DefaultPRNG PRNG;
	vegetationPoints.resize(width * height);

	treeCount = 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const auto Points = PoissonGenerator::generatePoissonPoints(GetBiome(biomeMapPerChunk[y * width + x]).GetVegetationLevel(), PRNG);

			for (int i = 0; i < Points.size(); i++)
			{
				if (GetHeightAt(Points[i].x * chunkResolution + (x * chunkResolution), Points[i].y * chunkResolution + (y * chunkResolution)) < seeLevel)
					continue;	
				vegetationPoints[y * width + x].push_back(std::make_pair(Points[i].x * chunkResolution + (x * chunkResolution), Points[i].y * chunkResolution + (y * chunkResolution)));
				treeCount++;
			}
		}
	}

	return true;
}

bool TerrainGenerator::GenerateBiomeMapPerChunk()
{
	if (!biomeMap) {
		std::cout << "[ERROR] BiomeMap not initialized" << std::endl;
		return false;
	}
	delete[] biomeMapPerChunk;
	biomeMapPerChunk = new int[width * height];
	int biomeSum = 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int biomeSum = 0;
			for (int j = 0; j < chunkResolution; j++) {
				for (int i = 0; i < chunkResolution; i++) {
					biomeSum += biomeMap[(y * chunkResolution + j) * width * chunkResolution + (x * chunkResolution + i)];
				}
			}
			biomeMapPerChunk[y * width + x] = biomeSum / (chunkResolution * chunkResolution);
		}
	}
	return true;
}
