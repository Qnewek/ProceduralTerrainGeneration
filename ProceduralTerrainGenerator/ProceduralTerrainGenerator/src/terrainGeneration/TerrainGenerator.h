#pragma once

#include <vector>
#include <utility>
#include <iostream>

#include "Noise.h"
#include "Splines/spline.h"

class TerrainGenerator
{
public:
	enum class WorldGenParameter {
		CONTINENTALNESS,
		MOUNTAINOUSNESS,
		WEIRDNESS
	};
	//TODO: Different mothodes for valvulating height
	enum class EvaluationMethod {
		LINEAR_COMBINE,
		SPLINE_COMBINE,
		C
	};
private:
	float* heightMap;
	int resolution;
	int seed, width, height;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousnessNoise;
	noise::SimplexNoiseClass weirdnessNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousnessSpline;
	tk::spline weirdnessSpline;

	EvaluationMethod evalMethod = EvaluationMethod::LINEAR_COMBINE;
public:
	TerrainGenerator();
	~TerrainGenerator();

	bool Initialize(int _width, int _height);
	bool Resize(int _width, int _height);
	bool GenerateTerrain();
	bool GenerateNoises();

	void SetResolution();
	void SetContinentalnessNoiseConfig(noise::NoiseConfigParameters config) { continentalnessNoise.SetConfig(config); };
	void SetMountainousnessNoiseConfig(noise::NoiseConfigParameters config) { mountainousnessNoise.SetConfig(config); };
	void SetPVNoiseConfig(noise::NoiseConfigParameters config) { weirdnessNoise.SetConfig(config); };
	bool SetSplines(std::vector<std::vector<double>> splines);
	bool SetSpline(WorldGenParameter p, std::vector<std::vector<double>>  spline);

	int GetWidth(){ return width; };
	int GetHeight(){ return height; };
	float* GetHeightMap() const { return heightMap; }
	float GetHeightAt(int x, int y);
	int& GetResolitionRef() { return resolution; };
	noise::NoiseConfigParameters& GetSelectedNoiseConfig(WorldGenParameter p);
	noise::SimplexNoiseClass& GetSelectedNoise(WorldGenParameter p);
	EvaluationMethod& GetEvaluationMethod() { return evalMethod; };
	std::vector<std::vector<double>> GetSplinePoints(WorldGenParameter p);
};