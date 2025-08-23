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
		float xoffset;
		float yoffset;
		int seed;

		//Fractal noise generation
		float scale;
		int octaves;
		float constrast;
		float redistribution;
		float lacunarity;
		float persistance;
		Options option;
		float revertGain;

		//Ridge
		bool Ridge;
		float RidgeGain;
		float RidgeOffset;

		//Island
		bool island;
		float mixPower;
		IslandType islandType;

		//Symmetrical or sth
		bool symmetrical;

		NoiseConfigParameters(int seed = 345, float xoffset = 0.0f, float yoffset = 0.0f, float scale = 1.0f, int octaves = 8,
			float constrast = 1.0f, float redistribution = 1.0f, float lacunarity = 2.0f,
			float persistance = 0.5f, float scaleDown = 1.0f, Options option = Options::REVERT_NEGATIVES, float revertGain = 0.5f, bool Ridge = false,
			float RidgeGain = 1.0f, float RidgeOffset = 1.0f, bool island = false, float mixPower = 0.5f,
			IslandType islandType = IslandType::CONE, bool symmetrical = false):
			seed(seed), xoffset(xoffset), yoffset(yoffset), scale(scale), octaves(octaves), constrast(constrast),
			redistribution(redistribution), lacunarity(lacunarity), persistance(persistance), option(option), revertGain(revertGain),
			Ridge(Ridge), RidgeGain(RidgeGain), RidgeOffset(RidgeOffset), island(island), islandType(islandType), mixPower(mixPower), 
			symmetrical(symmetrical){}
	};

	class SimplexNoiseClass
	{
	public:
		SimplexNoiseClass();
		~SimplexNoiseClass();

		bool GenerateFractalNoise();
		bool GenerateFractalNoiseByChunks();
		float MakeIsland(float e, int x, int y);
		bool MakeMapRidged();

		void InitMap();
		void SetSeed(int seed);
		void Reseed();
		void SetScale(float scale);
		void SetMapSize(unsigned int width, unsigned int height);
		void SetChunkSize(unsigned int chunkWidth, unsigned int chunkHeight);
		void SetConfig(NoiseConfigParameters config);

		float* GetMap() const { return heightMap; }
		float GetVal(int x, int y) const { return heightMap[y * width * chunkWidth + x]; }
		unsigned int GetWidth()  const { return width; }
		unsigned int GetHeight() const { return height; }
		unsigned int GetChunkWidth() const { return chunkWidth; }
		unsigned int GetChunkHeight() const { return chunkHeight; }
		NoiseConfigParameters& GetConfigRef() { return config; }

	private:
		NoiseConfigParameters config;
		float* heightMap;
		unsigned int width, height;
		unsigned int chunkWidth, chunkHeight;

		float Ridge(float h, float offset, float gain);
	};
}
