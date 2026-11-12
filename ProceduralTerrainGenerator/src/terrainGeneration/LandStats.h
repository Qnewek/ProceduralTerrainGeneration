#pragma once

#include "Grid.h"

//How a height map splits into water and land, and the land into landmasses (for calibrating the terrain)
struct LandStats {
	float waterShare = 0.0f;		//texels below the water height, of all texels
	int landmasses = 0;				//groups of land texels connected through edges (not corners)
	float largestShare = 0.0f;		//the largest landmass, of all land; 0 without land
	float continentalShare = 0.0f;	//land in landmasses of at least continentMinShare of the map, of all land; 0 without land
};

namespace land {
	//Smallest landmass counted as a continent, as a share of the whole map
	constexpr float continentMinShare = 0.01f;

	//How the world is sampled for the statistics (GpuHeightGenerator::SampleTerrain): sampleSize^2 samples, sampleStep map texels
	//apart, so 32768 texels along each side - many continents at the default continentalness scale, while small islands
	//below the step are missed
	constexpr int sampleSize = 512;
	constexpr float sampleStep = 64.0f;

	//@param waterHeight - texels below it are water (TerrainConfig::WaterHeight)
	LandStats Measure(const HeightField& map, float waterHeight);
}
