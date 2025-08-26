#include "TerrainGenerator.h"

#include <iostream>
#include <math.h>

#include "PoissonSampling/PoissonGenerator.h"

TerrainGenerator::TerrainGenerator() : width(0), height(0), seed(0),
heightMap(nullptr), biomeMap(nullptr),
continentalnessNoise(), mountainousnessNoise(), PVNoise(), continentalnessSpline(), mountainousnessSpline(), PVSpline(),
seeLevel(64.0f), biomeGen()
{
	//Initial noise settings
	continentalnessNoise.GetConfigRef().seed = 3;
	continentalnessNoise.GetConfigRef().constrast = 1.5f;
	continentalnessNoise.GetConfigRef().octaves = 7;
	continentalnessNoise.GetConfigRef().scale = 0.05f;
	continentalnessNoise.GetConfigRef().option = noise::Options::NOTHING;

	mountainousnessNoise.GetConfigRef().seed = 9;
	mountainousnessNoise.GetConfigRef().constrast = 1.5f;
	mountainousnessNoise.GetConfigRef().scale = 0.1f;
	mountainousnessNoise.GetConfigRef().option = noise::Options::NOTHING;

	PVNoise.GetConfigRef().seed = 456;   
	PVNoise.GetConfigRef().constrast = 1.5f;
	PVNoise.GetConfigRef().RidgeGain = 3.0f;
	PVNoise.GetConfigRef().scale = 0.05f;
	PVNoise.GetConfigRef().option = noise::Options::NOTHING;
	PVNoise.GetConfigRef().Ridge = true;
}

TerrainGenerator::~TerrainGenerator()
{
	if(heightMap)
		delete[] heightMap;
	if (biomeMap)
		delete[] biomeMap;
}

bool TerrainGenerator::InitMap()
{
	if (width <= 0 || height <= 0)
		return false;

	if (heightMap)
		delete[] heightMap;

	heightMap = new float[width * height];

	return true;
}

bool TerrainGenerator::InitBiomeMap()
{
	if (width <= 0 || height <= 0)
		return false;

	if (biomeMap)
		delete[] biomeMap;

	biomeMap = new int[width * height];

	return true;
}

bool TerrainGenerator::GenerateTerrain()
{
	if (!GenerateHeightMap())
	{
		std::cout << "[ERROR] HeightMap couldnt be generated\n";
		return false;
	}

	if (!GenerateBiomes())
	{
		std::cout << "[ERROR] Biomes couldnt be generated\n";
		return false;
	}
	return true;
}

bool TerrainGenerator::GenerateHeightMap()
{
	if (!heightMap) {
		std::cout << "[ERROR] HeightMap not initialized, please set a size of the map!\n";
		return false;
	}

	continentalnessNoise.InitMap();
	continentalnessNoise.Reseed();
	continentalnessNoise.GenerateFractalNoise();

	mountainousnessNoise.InitMap();
	mountainousnessNoise.Reseed();
	mountainousnessNoise.GenerateFractalNoise();

	PVNoise.InitMap();
	PVNoise.Reseed();
	PVNoise.GenerateFractalNoise();

	float continentalness = 0.0f;
	float mountainousness = 0.0f;
	float PV = 0.0f;
	float elevation = 0.0f;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			continentalness = continentalnessNoise.GetVal(x, y);
			//TODO: Build a proper logic for terrain generation
			/*mountainousness = mountainousnessSpline(mountainousnessNoise.GetVal(x, y));
			PV = PVSpline(PVNoise.GetVal(x, y));

			if (continentalness >= -0.2 && continentalness <= 0.0)
				mountainousness *= 0.0;
			else if (continentalness > 0.0)
				mountainousness *= continentalness;
			else
				mountainousness *= -(continentalness + 0.2) / 25;

			mountainousness -= mountainousness * PV;
			elevation = continentalnessSpline(continentalness) + mountainousness - (PV * 20.0f);*/
			heightMap[y * width + x] = continentalness;
		}
	}
	std::cout << "[LOG] HeightMap of size: " << height << "x" << width << " succesfully evaluated\n";
	return true;
}

bool TerrainGenerator::GenerateBiomes()
{
	if (!InitBiomeMap()) {
		return false;
	}

	if (!biomeGen.Biomify(heightMap, biomeMap, width, height, seed, continentalnessNoise, mountainousnessNoise)) {
		return false;
	}

	return true;
}

bool TerrainGenerator::GenerateVegetation()
{
	if (!biomeMap) {
		std::cout << "[ERROR] BiomeMap or BiomeMapPerChunk not initialized\n";
		return false;
	}

	PoissonGenerator::DefaultPRNG PRNG;
	vegetationPoints.resize(width * height);

	treeCount = 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const auto Points = PoissonGenerator::generatePoissonPoints(1.0f, PRNG);

			for (int i = 0; i < Points.size(); i++)
			{
				if (GetHeightAt(Points[i].x + x, Points[i].y + y) < seeLevel)
					continue;
				vegetationPoints[y * width + x].push_back(std::make_pair(Points[i].x + x, Points[i].y + y));
				treeCount++;
			}
		}
	}

	return true;
}

bool TerrainGenerator::SetSize(int _width, int _height)
{
	if(width == _width && height == _height)
		return true;
	if (_width <= 0 || _height <= 0) {
		std::cout << "[ERROR] Width and height must be greater than 0\n";
		return false;
	}

	this->width = _width;
	this->height = _height;

	if(heightMap){
		delete[] heightMap;
	}

	heightMap = new float[width * height];

	this->mountainousnessNoise.SetMapSize(width, height);
	this->continentalnessNoise.SetMapSize(width, height);
	this->PVNoise.SetMapSize(width, height);

	return true;
}

bool TerrainGenerator::SetSeeLevel(float seeLevel)
{
	if (seeLevel < 0)
		return false;

	this->seeLevel = seeLevel;

	return true;
}

bool TerrainGenerator::SetSplines(std::vector<std::vector<double>> splines)
{
	if (splines.size() <= 5)
		return false;
	continentalnessSpline.set_points(splines[0], splines[1]);
	mountainousnessSpline.set_points(splines[2], splines[3]);
	PVSpline.set_points(splines[4], splines[5]);

	return true;
}

bool TerrainGenerator::SetSpline(WorldParameter p, std::vector<std::vector<double>> spline)
{
	switch (p)
	{
	case WorldParameter::CONTINENTALNESS:
		continentalnessSpline.set_points(spline[0], spline[1]);
		break;
	case WorldParameter::MOUNTAINOUSNESS :
		mountainousnessSpline.set_points(spline[0], spline[1]);
		break;
	case WorldParameter::PV:
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
		std::cout << "[ERROR] Biomes couldnt be set\n";
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

float TerrainGenerator::GetHeightAt(int x, int y)
{
	if (!heightMap)
		return -1.0f;
	return heightMap[y * width + x];
}

int TerrainGenerator::GetBiomeAt(int x, int y)
{
	if (!biomeMap)
		return -1;	
	return biomeMap[y * width + x];
}

noise::NoiseConfigParameters& TerrainGenerator::GetSelectedNoiseConfig(WorldParameter p){
	switch (p)
	{
	case WorldParameter::CONTINENTALNESS:
		return continentalnessNoise.GetConfigRef();
		break;
	case WorldParameter::MOUNTAINOUSNESS:
		return mountainousnessNoise.GetConfigRef();
		break;
	case WorldParameter::PV:
		return PVNoise.GetConfigRef();
		break;
	case WorldParameter::HUMIDITY:
		biomeGen.GetHumidityNoiseConfig();
		break;
	case WorldParameter::TEMPERATURE:
		biomeGen.GetTemperatureNoiseConfig();
		break;
	default:
		break;
	}
}

noise::SimplexNoiseClass& TerrainGenerator::GetSelectedNoise(WorldParameter p)
{
	switch (p)
	{
	case WorldParameter::CONTINENTALNESS:
		return continentalnessNoise;
		break;
	case WorldParameter::MOUNTAINOUSNESS:
		return mountainousnessNoise;
		break;
	case WorldParameter::PV:
		return PVNoise;
		break;
	case WorldParameter::HUMIDITY:
		return biomeGen.GetHumidityNoise();
		break;
	case WorldParameter::TEMPERATURE:
		return biomeGen.GetTemperatureNoise();
		break;
	default:
		break;
	}
}
