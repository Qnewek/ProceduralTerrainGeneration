#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "GeoReference.h"

//Land cover under a map of the real world (Imported map, stage D3): the class of every map pixel from land cover files (ESA WorldCover
//and the like: GeoTIFF, one unsigned 8-bit class code per pixel, 0 = no data). The class of a map pixel is the most frequent one of
//k x k samples over it (k = map pixel / land cover pixel, 1 to 4; a tie goes to the smaller code, 0 is skipped)
namespace landcover {
	//What happened to one file
	struct FileStatus {
		std::string path;
		std::string system;		//its coordinate system for the UI, empty when it could not be read
		float coverage = 0.0f;	//share of the map pixels that got their class from this file, 0 to 1
		bool stretched = false;	//laid over the whole map (the map or the file has no usable georeference)
		std::string skipped;	//why it was not used; empty = used
		std::string error;		//why it could not be read; empty = read
	};

	struct ClassMap {
		int width = 0, height = 0;
		std::vector<uint8_t> classes;			//map[y * width + x], 0 = no class
		std::array<uint64_t, 256> counts{};		//map pixels of each class
		std::vector<FileStatus> files;			//in the order of the files given
	};

	//The class map of a map width x height. With a georeference in a supported system: every file with a georeference in a supported
	//system, read only under the map (an earlier file wins where both have a class); others are skipped. Without one: the first readable
	//file is stretched over the map, the others are skipped
	//@param maxRasterSide - the part of a file under the map is reduced (most frequent class of s x s blocks) until no side is longer;
	//0 = 2 x the longer side of the map
	//@throws std::bad_alloc only; problems of the files are in FileStatus::error
	ClassMap Compute(const std::vector<std::filesystem::path>& files, int width, int height, const std::optional<geo::GeoReference>& map,
		int maxRasterSide = 0);
}
