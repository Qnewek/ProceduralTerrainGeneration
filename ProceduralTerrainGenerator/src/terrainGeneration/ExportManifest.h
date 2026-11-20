#pragma once

#include <optional>
#include <string>
#include <vector>

#include "HeightmapExport.h"

//Description of an export pack: one folder with the files of a map and <name>.json telling other tools how to read them
//(size and heights in metres, value encoding, sea level, axes) plus the generation parameters, so the map can be made again.
//Later parts of the pack (material maps, meshes, vegetation) add their files to the same list.
namespace exportpack {
	//Metres per map texel; the app draws one texel as TerrainView::worldUnitsPerTexel world units = 1 m
	inline constexpr float metresPerTexel = 1.0f;

	//One file of the pack
	struct File {
		std::string path;		//relative to the manifest
		std::string content;	//what it holds, e.g. "height", "height_eroded_pipes", "splat", "weight"
		std::string format;		//e.g. "png16", "exr32", "png8 rgba"
		std::optional<heightmap::Format> height;	//set for height maps: the manifest tells how their values become metres
	};
	File HeightFile(std::string path, std::string content, heightmap::Format format);
	//An 8-bit PNG with 1 (grey), 3 (RGB) or 4 (RGBA) channels
	File Png8File(std::string path, std::string content, int channels);

	//A surface material in the material maps (splat)
	struct Material {
		int id = 0;					//index in terrain_materials.json
		std::string name;
		std::string splatFile;		//RGBA file holding it
		int splatChannel = 0;		//0 - 3 = R, G, B, A
		std::string weightFile;		//its own greyscale map; empty = it covers nothing, no file
		float coverage = 0.0f;		//mean share over the map
	};

	//An entry of the biome table in the biome id map
	struct Biome {
		int id = 0;					//value in the id map
		std::string name;
		float color[3] = { 0.0f, 0.0f, 0.0f };
		float coverage = 0.0f;		//share of the map
	};

	struct Manifest {
		std::string name;
		std::string mode;					//generation mode of the app, e.g. "TerrainGen"
		int width = 0, height = 0;			//map size in texels
		float metresPerUnit = 1.0f;			//metres of height value 1
		heightmap::ValueRange range{ 0.0f, 1.0f };	//values mapped to 0 - 65535 by the 16-bit files
		heightmap::ValueRange mapValues{ 0.0f, 0.0f };	//smallest and largest value of the main height map
		std::optional<float> seaLevel;		//height value of the sea surface; none = the mode has no sea
		std::string generation;				//generation parameters as preset JSON; empty = none, invalid JSON is stored as text
		std::vector<File> files;
		std::vector<Material> materials;	//empty = no material maps exported
		std::vector<Biome> biomes;			//empty = no biome maps exported
	};

	//Unreal Engine landscape import of a 16-bit file of the pack: Z scale 100 spans 512 m around the actor's Z
	struct UnrealImport {
		float zScale = 100.0f;
		float locationZ = 0.0f;		//centimetres
	};
	UnrealImport Unreal(const Manifest& manifest);

	//File name of the manifest of a pack
	inline std::string ManifestName(const std::string& name) { return name + ".json"; }

	std::string ToJson(const Manifest& manifest);
}
