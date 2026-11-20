#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Grid.h"

//Material and biome maps of an export pack: shares of the surface materials (splat map) as RGBA textures with 4 materials each
//(Unity alphamaps) and as one greyscale weight map per material (Unreal landscape layers, Godot), biome ids and colors.
//All 8-bit PNGs, rows from y = 0 like the height maps. No OpenGL here: the subsystem reads the textures and hands over the bytes.
namespace splatmap {
	//Shares of every material per texel: data[(y * width + x) * channels + material], 0 - 255
	struct Shares {
		int width = 0, height = 0;
		int channels = 0;
		std::vector<uint8_t> data;

		uint8_t At(int x, int y, int material) const { return data[(static_cast<size_t>(y) * width + x) * channels + material]; }
	};

	//Shares from a texture array read layer after layer, each layer RGBA (4 materials) row by row, as the splat map of the generator is stored
	//@param materials - number of materials kept (channels of the result), at most 4 * layers
	Shares FromLayers(const uint8_t* layers, int width, int height, int layerCount, int materials);

	//Makes the shares of every texel sum to exactly 255 (8-bit rounding of the GPU loses up to a few units), keeping their proportions:
	//largest remainder rounding. Texels with no material stay empty
	void Normalize(Shares& shares);

	//One material as a greyscale map
	std::vector<uint8_t> Channel(const Shares& shares, int material);
	//Four materials from firstMaterial on as RGBA; channels past the last material are 0
	std::vector<uint8_t> Rgba(const Shares& shares, int firstMaterial);
	//Mean share of every material over the map, 0 - 1
	std::vector<float> Coverage(const Shares& shares);

	//Biome ids as 8-bit greyscale (id = value); ids outside 0 - 255 are clipped
	std::vector<uint8_t> BiomeIds(const BiomeMap& ids);
	//Share of the map taken by every biome id from 0 to count - 1
	std::vector<float> BiomeCoverage(const BiomeMap& ids, int count);
	//Colors 0 - 1 (RGBA floats, alpha ignored) as 8-bit RGB
	std::vector<uint8_t> Colors(const float* rgba, size_t texels);

	//File name part from a material name: letters and digits kept, anything else becomes '_', lower case
	std::string SafeName(const std::string& name);

	//Writes an 8-bit PNG with 1 (grey), 3 (RGB) or 4 (RGBA) channels, creating missing folders
	//@param error - description of the problem on failure
	//@return true on success
	bool WritePng8(const std::filesystem::path& path, const std::vector<uint8_t>& pixels, int width, int height, int channels, std::string& error);
}
