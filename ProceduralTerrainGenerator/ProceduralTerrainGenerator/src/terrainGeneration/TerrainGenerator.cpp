#include "TerrainGenerator.h"

#include <iostream>
#include <math.h>

#include "PoissonSampling/PoissonGenerator.h"

TerrainGenerator::TerrainGenerator() : width(0), height(0), seed(0), heightMap(nullptr), 
continentalnessNoise(), mountainousnessNoise(), PVNoise(), continentalnessSpline(), mountainousnessSpline(), PVSpline()
{
}

TerrainGenerator::~TerrainGenerator()
{
	if(heightMap)
		delete[] heightMap;
}

bool TerrainGenerator::Initialize(int _width, int _height)
{
	if(!Resize(_width, _height))
		return false;
	this->width = _width;
	this->height = _height;

	//Basic values for noises
	continentalnessNoise.GetConfigRef().seed = 3;
	continentalnessNoise.GetConfigRef().constrast = 1.5f;
	continentalnessNoise.GetConfigRef().octaves = 7;
	continentalnessNoise.GetConfigRef().scale = 0.5f;
	continentalnessNoise.GetConfigRef().option = noise::Options::NOTHING;

	mountainousnessNoise.GetConfigRef().seed = 9;
	mountainousnessNoise.GetConfigRef().constrast = 1.5f;
	mountainousnessNoise.GetConfigRef().scale = 0.3f;
	mountainousnessNoise.GetConfigRef().option = noise::Options::NOTHING;

	PVNoise.GetConfigRef().seed = 456;
	PVNoise.GetConfigRef().constrast = 1.5f;
	PVNoise.GetConfigRef().RidgeGain = 3.0f;
	PVNoise.GetConfigRef().scale = 0.2f;
	PVNoise.GetConfigRef().option = noise::Options::NOTHING;
	PVNoise.GetConfigRef().Ridge = true;

	std::vector<std::vector<double>> s = { {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 40.0 ,64.0, 66.0, 68.0, 70.0},	//Continentalness {X,Y}
				{-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 5.0, 10.0, 20.0, 30.0, 80.0, 100.0, 170.0},	//Mountainousness {X,Y}
				{-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} };

	SetSplines(s);
	GenerateNoises();

	return true;
}

bool TerrainGenerator::Resize(int _width, int _height)
{
	if(width == _width && height == _height)
		return true;
	if (_width <= 0 || _height <= 0) {
		std::cout << "[ERROR] Width and height must be greater than 0\n";
		return false;
	}

	this->width = _width;
	this->height = _height;

	if (heightMap) {
		delete[] heightMap;
	}

	heightMap = new float[width * height];

	if(!this->mountainousnessNoise.Resize(width, height) || !this->continentalnessNoise.Resize(width, height) || !this->PVNoise.Resize(width, height)) {
		std::cout << "[ERROR] Could not resize noises\n";
		return false;
	}

	GenerateNoises();

	return true;
}

bool TerrainGenerator::GenerateTerrain()
{
	if (!heightMap) {
		std::cout << "[ERROR] HeightMap not initialized, please set a size of the map!\n";
		return false;
	}

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

bool TerrainGenerator::GenerateNoises()
{
	if (!continentalnessNoise.GenerateFractalNoise() || !mountainousnessNoise.GenerateFractalNoise() || !PVNoise.GenerateFractalNoise()) {
		return false;
	}
	return true;
}


//bool TerrainGenerator::GenerateVegetation()
//{
//	if (!biomeMap) {
//		std::cout << "[ERROR] BiomeMap or BiomeMapPerChunk not initialized\n";
//		return false;
//	}
//
//	PoissonGenerator::DefaultPRNG PRNG;
//	vegetationPoints.resize(width * height);
//
//	treeCount = 0;
//
//	for (int y = 0; y < height; y++) {
//		for (int x = 0; x < width; x++) {
//			const auto Points = PoissonGenerator::generatePoissonPoints(1.0f, PRNG);
//
//			for (int i = 0; i < Points.size(); i++)
//			{
//				if (GetHeightAt(Points[i].x + x, Points[i].y + y) < seeLevel)
//					continue;
//				vegetationPoints[y * width + x].push_back(std::make_pair(Points[i].x + x, Points[i].y + y));
//				treeCount++;
//			}
//		}
//	}
//
//	return true;
//}

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

float TerrainGenerator::GetHeightAt(int x, int y)
{
	if (!heightMap)
		return -1.0f;
	return heightMap[y * width + x];
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
	default:
		break;
	}
}
