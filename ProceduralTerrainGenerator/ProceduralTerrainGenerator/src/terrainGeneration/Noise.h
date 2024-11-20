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
		bool ridge;
		float ridgeGain;
		float ridgeOffset;

		//Island
		bool island;
		float mixPower;
		IslandType islandType;

		//Symmetrical or sth
		bool symmetrical;

		NoiseConfigParameters(int seed = 0, float xoffset = 0.0f, float yoffset = 0.0f, float scale = 1.0f, int octaves = 8,
			float constrast = 1.0f, float redistribution = 1.0f, float lacunarity = 2.0f,
			float persistance = 0.5f, float scaleDown = 1.0f, Options option = Options::REVERT_NEGATIVES, float revertGain = 0.5f, bool ridge = false,
			float ridgeGain = 1.0f, float ridgeOffset = 1.0f, bool island = false, float mixPower = 0.5f,
			IslandType islandType = IslandType::CONE, bool symmetrical = false):
			seed(seed), xoffset(xoffset), yoffset(yoffset), scale(scale), octaves(octaves), constrast(constrast),
			redistribution(redistribution), lacunarity(lacunarity), persistance(persistance), option(option), revertGain(revertGain),
			ridge(ridge), ridgeGain(ridgeGain), ridgeOffset(ridgeOffset), island(island), islandType(islandType), mixPower(mixPower), 
			symmetrical(symmetrical){}

		float getCheckSum() const {
			return xoffset + yoffset + scale + octaves + constrast + redistribution + lacunarity +
				persistance + ridgeGain + ridgeOffset + revertGain + mixPower + seed;
		}
	};

	class SimplexNoiseClass
	{
	public:
		SimplexNoiseClass();
		~SimplexNoiseClass();

		bool generateFractalNoise();
		bool generateFractalNoiseByChunks();
		float makeIsland(float e, int x, int y);
		bool makeMapRidged();

		void initMap();
		void setSeed(int seed);
		void setScale(float scale);
		void setMapSize(unsigned int width, unsigned int height);
		void setChunkSize(unsigned int chunkWidth, unsigned int chunkHeight);
		void setConfig(NoiseConfigParameters config);

		float* getMap() const { return heightMap; }
		float getVal(int x, int y) const { return heightMap[y * width * chunkWidth + x]; }
		unsigned int getWidth()  const { return width; }
		unsigned int getHeight() const { return height; }
		unsigned int getChunkWidth() const { return chunkWidth; }
		unsigned int getChunkHeight() const { return chunkHeight; }
		NoiseConfigParameters& getConfigRef() { return config; }

	private:
		NoiseConfigParameters config;
		float* heightMap;
		unsigned int width, height;
		unsigned int chunkWidth, chunkHeight;

		static float ridge(float h, float offset, float gain);
	};
}
