#pragma once

#include "glm/glm.hpp"
#include "Grid.h"

#include <array>
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
			float persistance = 0.5f, float /*scaleDown*/ = 1.0f, Options option = Options::REVERT_NEGATIVES, float revertGain = 0.5f, bool Ridge = false,
			float RidgeGain = 1.0f, float RidgeOffset = 1.0f, bool island = false, float mixPower = 0.5f,
			IslandType islandType = IslandType::CONE, bool symmetrical = false):
			seed(seed), resolution(res), xoffset(xoffset), yoffset(yoffset), scale(scale), octaves(octaves), constrast(constrast),
			redistribution(redistribution), lacunarity(lacunarity), persistance(persistance), option(option), revertGain(revertGain),
			Ridge(Ridge), RidgeGain(RidgeGain), RidgeOffset(RidgeOffset), island(island), islandType(islandType), mixPower(mixPower), 
			symmetrical(symmetrical){}
	};

	//Evaluates fractal simplex noise for a given configuration and stores the last generated map.
	//The configuration is not owned - it is passed to every call.
	class SimplexNoiseClass
	{
	public:
		SimplexNoiseClass();
		~SimplexNoiseClass();

		bool Initialize(int _width, int _height);
		bool Resize(int _width, int _height);

		//Fills the map in parallel (rows on the thread pool)
		bool GenerateFractalNoise(const NoiseConfigParameters& config, float originx, float originy);
		//Single sample; rebuilds the permutation table first if config.seed changed. Not thread-safe
		float PointNoise(const NoiseConfigParameters& config, float x, float y);
		//Single sample without touching the permutation table - safe to call from many threads at once.
		//UpdatePermutation(config.seed) must be called before
		float Evaluate(const NoiseConfigParameters& config, float x, float y) const;
		bool MakeMapRidged(const NoiseConfigParameters& config);
		float MakeIsland(const NoiseConfigParameters& config, float e, int x, int y) const;

		//Rebuilds the permutation table if the seed changed since the last call
		void UpdatePermutation(int seed);

		float* GetMap() { return heightMap.Data(); }
		const HeightField& GetField() const { return heightMap; }
		float GetVal(int x, int y);
		int GetWidth()  const { return width; }
		int GetHeight() const { return height; }

	private:
		HeightField heightMap;
		int width, height;

		//Permutation table owned by this noise, rebuilt only when the seed changes
		std::array<uint8_t, 256> perm{};
		int permSeed = 0;
		bool permReady = false;

		float Ridge(float h, float offset, float gain) const;
	};
}
