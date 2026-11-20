#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Grid.h"

//Saving height maps for other tools. 16-bit files map a fixed value range to the 16-bit range, so exports of different maps
//stay comparable; EXR stores 32-bit floats scaled to metres, without a range or rounding. Rows are written from y = 0,
//which is the top row of the 2D map view.
namespace heightmap {
	enum class Format {
		PNG16,	//16-bit greyscale PNG
		R16,	//headerless little-endian 16-bit values, row by row
		EXR32	//OpenEXR, one 32-bit float channel "Y", uncompressed scanlines
	};

	struct ValueRange {
		float min, max;
	};

	inline constexpr ValueRange terrainRange{ 0.0f, 1.0f };
	inline constexpr ValueRange noiseRange{ -1.0f, 1.0f };

	//File name of an export. R16 files do not store their size, so it is added to the name. EXR files get ".exr"
	//@param baseName - name without an extension
	std::string FileName(const std::string& baseName, Format format, int width, int height);

	//Maps values to 16 bits. Values outside the range and NaN are clipped to the nearest end (NaN to min)
	//@param clippedCells - receives the number of clipped cells
	std::vector<uint16_t> Quantize(const HeightField& map, ValueRange range, size_t& clippedCells);

	//OpenEXR file holding value * scale per texel. Non-finite values are written as range.min * scale
	//@param invalidCells - receives the number of non-finite values
	std::vector<unsigned char> EncodeExr(const HeightField& map, float scale, ValueRange range, size_t& invalidCells);

	//Smallest and largest finite value of a map; {0, 0} if it has none
	ValueRange MinMax(const HeightField& map);

	//Writes a map, creating missing folders. An existing file is replaced only after a successful write
	//@param range - values mapped to the 16-bit range (PNG16, R16)
	//@param clippedCells - receives the number of cells outside the range (EXR32: only the non-finite ones)
	//@param error - description of the problem on failure
	//@param exrScale - EXR32 stores value * exrScale (metres of height 1)
	//@return true on success
	bool Export(const std::filesystem::path& path, const HeightField& map, Format format, ValueRange range, size_t& clippedCells, std::string& error,
		float exrScale = 1.0f);
}
