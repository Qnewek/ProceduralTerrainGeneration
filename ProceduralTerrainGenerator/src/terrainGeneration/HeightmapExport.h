#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Grid.h"

//Saving height maps as 16-bit files for other tools. A fixed value range is mapped to the 16-bit range,
//so exports of different maps stay comparable. Rows are written from y = 0, which is the top row of the 2D map view.
namespace heightmap {
	enum class Format {
		PNG16,	//16-bit greyscale PNG
		R16		//headerless little-endian 16-bit values, row by row
	};

	struct ValueRange {
		float min, max;
	};

	inline constexpr ValueRange terrainRange{ 0.0f, 1.0f };
	inline constexpr ValueRange noiseRange{ -1.0f, 1.0f };

	//File name of an export. R16 files do not store their size, so it is added to the name
	//@param baseName - name without an extension
	std::string FileName(const std::string& baseName, Format format, int width, int height);

	//Maps values to 16 bits. Values outside the range and NaN are clipped to the nearest end (NaN to min)
	//@param clippedCells - receives the number of clipped cells
	std::vector<uint16_t> Quantize(const HeightField& map, ValueRange range, size_t& clippedCells);

	//Writes a map, creating missing folders. An existing file is replaced only after a successful write
	//@param clippedCells - receives the number of cells outside the range
	//@param error - description of the problem on failure
	//@return true on success
	bool Export(const std::filesystem::path& path, const HeightField& map, Format format, ValueRange range, size_t& clippedCells, std::string& error);
}
