#pragma once

#include "glm/glm.hpp"

#include <cstdint>
#include <vector>

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

		NoiseConfigParameters(int seed = 345, int res = 500, float xoffset = 0.0f, float yoffset = 0.0f, float scale = 1.0f, int octaves = 8,
			float constrast = 1.0f, float redistribution = 1.0f, float lacunarity = 2.0f,
			float persistance = 0.5f, float scaleDown = 1.0f, Options option = Options::REVERT_NEGATIVES, float revertGain = 0.5f, bool Ridge = false,
			float RidgeGain = 1.0f, float RidgeOffset = 1.0f, bool island = false, float mixPower = 0.5f,
			IslandType islandType = IslandType::CONE, bool symmetrical = false):
			seed(seed), resolution(res), xoffset(xoffset), yoffset(yoffset), scale(scale), octaves(octaves), constrast(constrast),
			redistribution(redistribution), lacunarity(lacunarity), persistance(persistance), option(option), revertGain(revertGain),
			Ridge(Ridge), RidgeGain(RidgeGain), RidgeOffset(RidgeOffset), island(island), islandType(islandType), mixPower(mixPower), 
			symmetrical(symmetrical){}
	};

	class SimplexNoiseClass
	{
	public:
		SimplexNoiseClass();
		~SimplexNoiseClass();

		bool Initialize(int _height, int _width);
		bool Resize(int _height, int _width);

		bool GenerateFractalNoise();
		bool MakeMapRidged();
		float MakeIsland(float e, int x, int y);

		void SetConfig(NoiseConfigParameters config) { this->config = config; }

		float* GetMap() const { return heightMap; }
		float GetVal(int x, int y);
		unsigned int GetWidth()  const { return width; }
		unsigned int GetHeight() const { return height; }
		NoiseConfigParameters& GetConfigRef() { return config; }

	private:
		NoiseConfigParameters config;
		float* heightMap;
		unsigned int width, height;

		float Ridge(float h, float offset, float gain);
	};
}
