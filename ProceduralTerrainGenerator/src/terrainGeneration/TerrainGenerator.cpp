#include "TerrainGenerator.h"

#include "Splines/spline.h"

//Builds a tk::spline from config control points
static tk::spline MakeSpline(const SplinePoints& points)
{
	tk::spline s;
	s.set_points(points.x, points.y, points.cubic ? tk::spline::cspline : tk::spline::linear);
	return s;
}

TerrainGenerator::TerrainGenerator() : width(0), height(0),
continentalnessNoise(), mountainousnessNoise(), weirdnessNoise()
{
}

TerrainGenerator::~TerrainGenerator()
{
}

bool TerrainGenerator::Initialize(int _width, int _height)
{
	return Resize(_width, _height);
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
	heightMap.Resize(width, height);

	if(!this->mountainousnessNoise.Resize(width, height) || !this->continentalnessNoise.Resize(width, height) || !this->weirdnessNoise.Resize(width, height)) {
		std::cout << "[ERROR] Could not resize noises\n";
		return false;
	}

	return true;
}

bool TerrainGenerator::GenerateTerrain(const TerrainConfig& config, float originx, float originy)
{
	if (!heightMap) {
		std::cout << "[ERROR] HeightMap not initialized, please set a size of the map!\n";
		return false;
	}

	tk::spline continentalnessSpline, mountainousnessSpline, weirdnessSpline;
	if (config.method == EvaluationMethod::SPLINE_COMBINE) {
		continentalnessSpline = MakeSpline(config.continentalnessSpline);
		mountainousnessSpline = MakeSpline(config.mountainousnessSpline);
		weirdnessSpline = MakeSpline(config.weirdnessSpline);
	}

	float continentalness = 0.0f;
	float mountainousness = 0.0f;
	float weirdness = 0.0f;
	float elevation = 0.0f;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			continentalness = continentalnessNoise.PointNoise(config.continentalness, x + originx, y + originy);
			mountainousness = mountainousnessNoise.PointNoise(config.mountainousness, x + originx, y + originy);
			weirdness = weirdnessNoise.PointNoise(config.weirdness, x + originx, y + originy);
			if (continentalness < -1.0f || mountainousness < -1.0f || weirdness < -1.0f) {
				std::cout << "[ERROR] Couldnt get value for component noise!\n";
				return false;
			}

			switch (config.method)
			{
			case EvaluationMethod::LINEAR_COMBINE:
			{
				mountainousness = (mountainousness + 1.0f) / 2.0f;
				continentalness = (continentalness + 1.0f) / 2.0f;
				weirdness = (weirdness + 1.0f) / 2.0f;
				elevation = continentalness * mountainousness * (1.0f - weirdness);
				break;
			}
			//TODO: Different algorithms for height evaluation
			case EvaluationMethod::SPLINE_COMBINE:
			{
				continentalness = static_cast<float>(continentalnessSpline(continentalness));
				mountainousness = static_cast<float>(mountainousnessSpline(mountainousness));
				weirdness = static_cast<float>(weirdnessSpline(weirdness));
				elevation = continentalness * mountainousness * weirdness;
				break;
			}
			case EvaluationMethod::C:
			default:
				elevation = 0.0f;
				break;
			}
			heightMap[y * width + x] = elevation;
		}
	}
	std::cout << "[LOG] HeightMap of size: " << width << "x" << height << " succesfully evaluated\n";
	return true;
}

bool TerrainGenerator::GenerateNoises(const TerrainConfig& config)
{
	if (!continentalnessNoise.GenerateFractalNoise(config.continentalness, 0.0f, 0.0f) ||
		!mountainousnessNoise.GenerateFractalNoise(config.mountainousness, 0.0f, 0.0f) ||
		!weirdnessNoise.GenerateFractalNoise(config.weirdness, 0.0f, 0.0f)) {
		std::cout << "[ERROR] Could not generate noises\n";
		return false;
	}
	return true;
}

float TerrainGenerator::GetHeightAt(int x, int y)
{
	if (!heightMap)
		return -1.0f;
	return heightMap[y * width + x];
}

noise::SimplexNoiseClass& TerrainGenerator::GetSelectedNoise(WorldGenParameter p)
{
	switch (p)
	{
	case WorldGenParameter::MOUNTAINOUSNESS:
		return mountainousnessNoise;
	case WorldGenParameter::WEIRDNESS:
		return weirdnessNoise;
	case WorldGenParameter::CONTINENTALNESS:
	default:
		return continentalnessNoise;
	}
}
