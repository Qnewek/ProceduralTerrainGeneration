#include "TerrainGenerator.h"


TerrainGenerator::TerrainGenerator() : width(0), height(0), seed(0), resolution(500.0f), heightMap(nullptr),
continentalnessNoise(), mountainousnessNoise(), weirdnessNoise(), continentalnessSpline(), mountainousnessSpline(), weirdnessSpline()
{
}

TerrainGenerator::~TerrainGenerator()
{
	if(heightMap)
		delete[] heightMap;
}

bool TerrainGenerator::Initialize(int _width, int _height)
{
	//Basic values for noises
	continentalnessNoise.GetConfigRef().seed = 623;
	continentalnessNoise.GetConfigRef().constrast = 1.5f;
	continentalnessNoise.GetConfigRef().octaves = 7;
	continentalnessNoise.GetConfigRef().scale = 0.5f;
	continentalnessNoise.GetConfigRef().option = noise::Options::NOTHING;

	mountainousnessNoise.GetConfigRef().seed = 262;
	mountainousnessNoise.GetConfigRef().constrast = 1.5f;
	mountainousnessNoise.GetConfigRef().scale = 0.3f;
	mountainousnessNoise.GetConfigRef().option = noise::Options::NOTHING;

	weirdnessNoise.GetConfigRef().seed = 192;
	weirdnessNoise.GetConfigRef().constrast = 1.5f;
	weirdnessNoise.GetConfigRef().RidgeGain = 3.0f;
	weirdnessNoise.GetConfigRef().scale = 0.2f;
	weirdnessNoise.GetConfigRef().option = noise::Options::NOTHING;
	weirdnessNoise.GetConfigRef().Ridge = true;

	std::vector<std::vector<double>> s = { {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 0.1 ,0.3, 0.70, 0.95, 1.0},	//Continentalness {X,Y}
				{-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 0.1, 0.2, 0.3, 0.3, 0.6, 0.6, 1.0},	//Mountainousness {X,Y}
				{-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} };
	
	if(!Resize(_width, _height))
		return false;

	SetSplines(s);

	return true;
}

bool TerrainGenerator::Resize(int _width, int _height)
{
	if (_width <= 0 || _height <= 0) {
		std::cout << "[ERROR] Width and height must be greater than 0\n";
		return false;
	}
	if(width == _width && height == _height)
		return true;

	this->width = _width;
	this->height = _height;

	if (heightMap) {
		delete[] heightMap;
	}

	heightMap = new float[width * height];

	if(!this->mountainousnessNoise.Resize(width, height) || !this->continentalnessNoise.Resize(width, height) || !this->weirdnessNoise.Resize(width, height)) {
		std::cout << "[ERROR] Could not resize noises\n";
		return false;
	}

	GenerateNoises();

	return true;
}

//Scales the resolution of terrain generation by 
void TerrainGenerator::SetResolution()
{
	continentalnessNoise.GetConfigRef().resolution = resolution;
	mountainousnessNoise.GetConfigRef().resolution = resolution;
	weirdnessNoise.GetConfigRef().resolution = resolution;

	GenerateNoises();
}

bool TerrainGenerator::GenerateTerrain(float originx, float originy)
{
	if (!heightMap) {
		std::cout << "[ERROR] HeightMap not initialized, please set a size of the map!\n";
		return false;
	}

	float continentalness = 0.0f;
	float mountainousness = 0.0f;
	float weirdness = 0.0f;
	float elevation = 0.0f;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			continentalness = continentalnessNoise.PointNoise(x + originx, y + originy);
			mountainousness = mountainousnessNoise.PointNoise(x + originx, y + originy);
			weirdness = weirdnessNoise.PointNoise(x + originx, y + originy);
			if (continentalness < -1.0f || mountainousness < -1.0f || weirdness < -1.0f) {
				std::cout << "[ERROR] Couldnt get value for component noise!\n";
				return false;
			}

			switch (evalMethod)
			{
			case TerrainGenerator::EvaluationMethod::LINEAR_COMBINE: 
			{
				mountainousness = (mountainousness + 1.0f) / 2.0f;
				continentalness = (continentalness + 1.0f) / 2.0f;
				weirdness = (weirdness + 1.0f) / 2.0f;
				elevation = continentalness * mountainousness * (1.0f - weirdness);
				break;
			}
			//TODO: Different algorithms for height evaluation
			case TerrainGenerator::EvaluationMethod::SPLINE_COMBINE:
			{
				continentalness = continentalnessSpline(continentalness);
				mountainousness = mountainousnessSpline(mountainousness);
				weirdness = weirdnessSpline(weirdness);
				elevation = continentalness * mountainousness * weirdness;
				break;
			}
			case TerrainGenerator::EvaluationMethod::C:
				break;
			default:
				break;
			}
			heightMap[y * width + x] = elevation;
		}
	}
	std::cout << "[LOG] HeightMap of size: " << height << "x" << width << " succesfully evaluated\n";
	return true;
}

bool TerrainGenerator::GenerateNoises()
{
	if (!continentalnessNoise.GenerateFractalNoise(0.0f,0.0f) || !mountainousnessNoise.GenerateFractalNoise(0.0f, 0.0f) || !weirdnessNoise.GenerateFractalNoise(0.0f, 0.0f)) {
		std::cout << "[ERROR] Could not generate noises\n";
		return false;
	}
	return true;
}

bool TerrainGenerator::SetSplines(std::vector<std::vector<double>> splines)
{
	if (splines.size() <= 5)
		return false;
	continentalnessSpline.set_points(splines[0], splines[1], tk::spline::linear);
	mountainousnessSpline.set_points(splines[2], splines[3], tk::spline::linear);
	weirdnessSpline.set_points(splines[4], splines[5], tk::spline::linear);

	return true;
}

bool TerrainGenerator::SetSpline(WorldGenParameter p, std::vector<std::vector<double>> spline)
{
	switch (p)
	{
	case WorldGenParameter::CONTINENTALNESS:
		continentalnessSpline.set_points(spline[0], spline[1], tk::spline::cspline);
		break;
	case WorldGenParameter::MOUNTAINOUSNESS :
		mountainousnessSpline.set_points(spline[0], spline[1], tk::spline::cspline);
		break;
	case WorldGenParameter::WEIRDNESS:
		weirdnessSpline.set_points(spline[0], spline[1], tk::spline::cspline);
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

noise::NoiseConfigParameters& TerrainGenerator::GetSelectedNoiseConfig(WorldGenParameter p){
	switch (p)
	{
	case WorldGenParameter::CONTINENTALNESS:
		return continentalnessNoise.GetConfigRef();
		break;
	case WorldGenParameter::MOUNTAINOUSNESS:
		return mountainousnessNoise.GetConfigRef();
		break;
	case WorldGenParameter::WEIRDNESS:
		return weirdnessNoise.GetConfigRef();
		break;
	default:
		break;
	}
}

noise::SimplexNoiseClass& TerrainGenerator::GetSelectedNoise(WorldGenParameter p)
{
	switch (p)
	{
	case WorldGenParameter::CONTINENTALNESS:
		return continentalnessNoise;
		break;
	case WorldGenParameter::MOUNTAINOUSNESS:
		return mountainousnessNoise;
		break;
	case WorldGenParameter::WEIRDNESS:
		return weirdnessNoise;
		break;
	default:
		break;
	}
}

std::vector<std::vector<double>> TerrainGenerator::GetSplinePoints(WorldGenParameter p)
{
	switch (p)
	{
	case TerrainGenerator::WorldGenParameter::CONTINENTALNESS: {
		return { continentalnessSpline.get_x(), continentalnessSpline.get_y()};
		break;
	}
	case TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS: {
		return { mountainousnessSpline.get_x(), mountainousnessSpline.get_y() };
		break;
	}
	case TerrainGenerator::WorldGenParameter::WEIRDNESS: {
		return { weirdnessSpline.get_x(), weirdnessSpline.get_y() };
		break;
	}
	default:
		break;
	}
	return {};
}
