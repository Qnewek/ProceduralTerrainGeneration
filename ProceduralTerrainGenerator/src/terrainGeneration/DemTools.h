#pragma once

#include <cstdint>
#include <vector>

#include "glm/glm.hpp"

#include "Grid.h"

//Preparing height maps of the real world (DEM) for the Imported map mode: holes, parts of a large file, pixel size in metres
namespace dem {
	//Largest side of a map the app shows (TerrainView, generators)
	constexpr int maxMapSize = 8192;

	//Fills the texels marked missing (missing[i] != 0) with the mean of their known 4-neighbours, in waves from the edges of the holes inwards
	//@return texels filled; -1 if no texel is known (the map is left as it was)
	int FillNoData(HeightField& map, const std::vector<uint8_t>& missing);
	//The rectangle size at first; the caller makes sure it lies within the map
	HeightField Crop(const HeightField& map, glm::ivec2 first, glm::ivec2 size);
	//Mean of every factor x factor block; ceil(width / factor) x ceil(height / factor), a partial block at the edge is the mean of what it has
	HeightField Downsample(const HeightField& map, int factor);
	//Smallest power of 2 the map has to be downsampled by to fit maxMapSize on both sides
	int FitDownsample(int width, int height);
	//Size of a pixel in metres; geographic pixels are in degrees and depend on the latitude (degrees) of the map
	glm::dvec2 PixelSizeMetres(glm::dvec2 pixelSize, bool geographic, double latitude);
}
