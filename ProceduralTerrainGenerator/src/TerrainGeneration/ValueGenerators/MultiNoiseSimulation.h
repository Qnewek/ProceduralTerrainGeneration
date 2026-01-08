#include <vector>
#include <utility>
#include <iostream>

#include "Noise.h"
#include "Splines/spline.h"


class MultiNoiseSimulation : public HeightGenerator
{
public:
	enum class WorldGenParameter {
		CONTINENTALNESS,
		MOUNTAINOUSNESS,
		WEIRDNESS
	};
	enum class EvaluationMethod {
		LINEAR_COMBINE,
		SPLINE_COMBINE,
		C
	};
private:
	int resolution;
	int seed;

	noise::SimplexNoiseClass continentalnessNoise;
	noise::SimplexNoiseClass mountainousnessNoise;
	noise::SimplexNoiseClass weirdnessNoise;

	tk::spline continentalnessSpline;
	tk::spline mountainousnessSpline;
	tk::spline weirdnessSpline;

	EvaluationMethod evalMethod = EvaluationMethod::LINEAR_COMBINE;
public:
	MultiNoiseSimulation();
	~MultiNoiseSimulation() = default;

	bool Initialize() override;
	float GetVal(float x, float y) override;

	void SetResolution();
	void SetContinentalnessNoiseConfig(noise::NoiseConfigParameters config) { continentalnessNoise.SetConfig(config); };
	void SetMountainousnessNoiseConfig(noise::NoiseConfigParameters config) { mountainousnessNoise.SetConfig(config); };
	void SetPVNoiseConfig(noise::NoiseConfigParameters config) { weirdnessNoise.SetConfig(config); };
	bool SetSplines(std::vector<std::vector<double>> splines);
	bool SetSpline(WorldGenParameter p, std::vector<std::vector<double>>  spline);

	int& GetResolitionRef() { return resolution; };
	noise::NoiseConfigParameters& GetSelectedNoiseConfig(WorldGenParameter p);
	noise::SimplexNoiseClass& GetSelectedNoise(WorldGenParameter p);
	EvaluationMethod& GetEvaluationMethod() { return evalMethod; };
	std::vector<std::vector<double>> GetSplinePoints(WorldGenParameter p);
};