#include "MultiNoiseSimulation.h"


MultiNoiseSimulation::MultiNoiseSimulation() : seed(0), resolution(500.0f),
continentalnessNoise(), mountainousnessNoise(), weirdnessNoise(), continentalnessSpline(), mountainousnessSpline(), weirdnessSpline()
{
}

bool MultiNoiseSimulation::Initialize()
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
	SetSplines(s);

	return true;
}

//Scales the resolution of terrain generation by 
void MultiNoiseSimulation::SetResolution()
{
	continentalnessNoise.GetConfigRef().resolution = resolution;
	mountainousnessNoise.GetConfigRef().resolution = resolution;
	weirdnessNoise.GetConfigRef().resolution = resolution;
}

float MultiNoiseSimulation::GetVal(float x, float y)
{
	float continentalness = 0.0f;
	float mountainousness = 0.0f;
	float weirdness = 0.0f;
	float elevation = 0.0f;

	continentalness = continentalnessNoise.GetVal(x, y);
	mountainousness = mountainousnessNoise.GetVal(x, y);
	weirdness = weirdnessNoise.GetVal(x, y);
	if (continentalness < -1.0f || mountainousness < -1.0f || weirdness < -1.0f) {
		std::cout << "[ERROR] Couldnt get value for component noise!\n";
		return false;
	}

	switch (evalMethod)
	{
	case MultiNoiseSimulation::EvaluationMethod::LINEAR_COMBINE:
	{
		mountainousness = (mountainousness + 1.0f) / 2.0f;
		continentalness = (continentalness + 1.0f) / 2.0f;
		weirdness = (weirdness + 1.0f) / 2.0f;
		elevation = continentalness * mountainousness * (1.0f - weirdness);
		break;
	}
	case MultiNoiseSimulation::EvaluationMethod::SPLINE_COMBINE:
	{
		continentalness = continentalnessSpline(continentalness);
		mountainousness = mountainousnessSpline(mountainousness);
		weirdness = weirdnessSpline(weirdness);
		elevation = continentalness * mountainousness * weirdness;
		break;
	}
	case MultiNoiseSimulation::EvaluationMethod::C:
		break;
	default:
		break;
	}
	return elevation;
}


bool MultiNoiseSimulation::SetSplines(std::vector<std::vector<double>> splines)
{
	if (splines.size() <= 5)
		return false;
	continentalnessSpline.set_points(splines[0], splines[1], tk::spline::linear);
	mountainousnessSpline.set_points(splines[2], splines[3], tk::spline::linear);
	weirdnessSpline.set_points(splines[4], splines[5], tk::spline::linear);

	return true;
}

bool MultiNoiseSimulation::SetSpline(WorldGenParameter p, std::vector<std::vector<double>> spline)
{
	switch (p)
	{
	case WorldGenParameter::CONTINENTALNESS:
		continentalnessSpline.set_points(spline[0], spline[1], tk::spline::cspline);
		break;
	case WorldGenParameter::MOUNTAINOUSNESS:
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

noise::NoiseConfigParameters& MultiNoiseSimulation::GetSelectedNoiseConfig(WorldGenParameter p) {
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

noise::SimplexNoiseClass& MultiNoiseSimulation::GetSelectedNoise(WorldGenParameter p)
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

std::vector<std::vector<double>> MultiNoiseSimulation::GetSplinePoints(WorldGenParameter p)
{
	switch (p)
	{
	case MultiNoiseSimulation::WorldGenParameter::CONTINENTALNESS: {
		return { continentalnessSpline.get_x(), continentalnessSpline.get_y() };
		break;
	}
	case MultiNoiseSimulation::WorldGenParameter::MOUNTAINOUSNESS: {
		return { mountainousnessSpline.get_x(), mountainousnessSpline.get_y() };
		break;
	}
	case MultiNoiseSimulation::WorldGenParameter::WEIRDNESS: {
		return { weirdnessSpline.get_x(), weirdnessSpline.get_y() };
		break;
	}
	default:
		break;
	}
	return {};
}
