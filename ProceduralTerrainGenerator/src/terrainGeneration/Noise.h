#pragma once

//Parameters of one fractal simplex noise. The noise itself is evaluated only in GLSL (res/shaders/common/noise.glsl)
namespace noise
{
	enum class Options {
		REFIT_ALL,
		FLATTEN_NEGATIVES,
		REVERT_NEGATIVES,
		NOTHING
	};

	enum class IslandType {
		CONE,
		DIAGONAL,
		EUCLIDEAN_SQUARED,
		SQUARE_BUMP,
		HYPERBOLOID,
		SQUIRCLE,
		TRIG
	};

	struct NoiseConfigParameters {
		//Point offset
		float xoffset, yoffset;
		int resolution;
		int seed;

		//Fractal noise generation
		int octaves;
		float scale, constrast,	redistribution,	lacunarity,	persistance,revertGain;
		Options option;

		//Ridge
		bool Ridge;
		float RidgeGain, RidgeOffset;

		//Island
		bool island;
		float mixPower;
		IslandType islandType;

		//Symmetrical or sth
		bool symmetrical;

		//Domain warping: noise coordinates are moved by a second fractal noise before sampling. Strength in features of the noise
		//(1 = by about one hill), 0 = off; warpScale = frequency of the moving noise relative to this one. Not with symmetrical
		float warpStrength = 0.0f;
		float warpScale = 1.0f;
		//Octaves are weakened where the octaves below are already steep, so slopes stay smooth and details gather
		//in valleys and on ridges, like eroded terrain; 0 = off
		float slopeErosion = 0.0f;
		//Terraces: the value is split into this many steps per unit, 0 = off; sharpness 0 = smooth steps, 1 = flat ledges
		int terraceSteps = 0;
		float terraceSharpness = 0.5f;

		NoiseConfigParameters(int seed = 345, int res = 500, float xoffset = 0.0f, float yoffset = 0.0f, float scale = 1.0f, int octaves = 8,
			float constrast = 1.0f, float redistribution = 1.0f, float lacunarity = 2.0f,
			float persistance = 0.5f, float /*scaleDown*/ = 1.0f, Options option = Options::REVERT_NEGATIVES, float revertGain = 0.5f, bool Ridge = false,
			float RidgeGain = 1.0f, float RidgeOffset = 1.0f, bool island = false, float mixPower = 0.5f,
			IslandType islandType = IslandType::CONE, bool symmetrical = false):
			seed(seed), resolution(res), xoffset(xoffset), yoffset(yoffset), scale(scale), octaves(octaves), constrast(constrast),
			redistribution(redistribution), lacunarity(lacunarity), persistance(persistance), option(option), revertGain(revertGain),
			Ridge(Ridge), RidgeGain(RidgeGain), RidgeOffset(RidgeOffset), island(island), islandType(islandType), mixPower(mixPower), 
			symmetrical(symmetrical){}
	};
}
