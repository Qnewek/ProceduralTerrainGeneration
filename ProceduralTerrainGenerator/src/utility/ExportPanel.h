#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ExportManifest.h"
#include "HeightmapExport.h"
#include "MaterialExport.h"

//ImGui "Saving" section writing an export pack: a folder <exports>/<name>/ with the height maps, optionally the material and
//biome maps, and <name>.json describing them (exportpack::Manifest)
class ExportPanel
{
public:
	//One height map to write, each one goes to its own file
	struct Target {
		int width, height;
		const char* suffix;		//added to the file name and the content, e.g. "_eroded_pipes"; "" for the main map
		std::function<HeightField()> read;	//called only when exporting, because a map may have to come from the GPU
	};

	//Shares of the surface materials (splat map) of the main map
	struct MaterialSource {
		std::vector<std::string> names;		//every material in the order of their ids
		std::function<splatmap::Shares()> read;	//called only when exporting; one channel per name
	};

	//Biome ids and colors of the main map
	struct BiomeSource {
		std::vector<exportpack::Biome> table;	//id, name and color of every biome; the panel measures the coverage
		std::function<BiomeMap()> readIds;
		std::function<std::vector<float>()> readColors;	//RGBA floats per texel
	};

	//What the subsystem exports
	struct Pack {
		const char* mode = "";				//generation mode, stored in the manifest
		heightmap::ValueRange range{ 0.0f, 1.0f };	//values mapped to the 16-bit range
		float metresPerUnit = 1.0f;			//metres of height value 1 (display height scale / world units per metre)
		std::optional<float> seaLevel;		//height value of the sea surface, if the mode has a sea
		std::function<std::string()> generation;	//generation parameters as preset JSON, called only when exporting
		std::vector<Target> heightmaps;		//the first one is the main map
		std::optional<MaterialSource> materials;	//none = the map has no material maps now
		std::optional<BiomeSource> biomes;			//none = the map has no biomes now
		const char* layersMissing = nullptr;		//why the material and biome maps are missing, shown instead of their checkboxes
	};

	//@param _folder - folder holding the packs, created by the first export
	explicit ExportPanel(std::filesystem::path _folder);

	void ImGuiDraw(const Pack& pack);

private:
	std::filesystem::path PackFolder() const { return folder / name; }
	std::string HeightFileName(const Target& target) const;
	//Writes a file of the pack and adds it to the manifest; on failure sets the status
	bool WritePng(const std::filesystem::path& packFolder, const exportpack::File& file, const std::vector<uint8_t>& pixels, int width, int height,
		int channels, exportpack::Manifest& manifest, std::string& saved);
	bool ExportMaterials(const MaterialSource& source, const std::filesystem::path& packFolder, exportpack::Manifest& manifest, std::string& saved);
	bool ExportBiomes(const BiomeSource& source, const std::filesystem::path& packFolder, exportpack::Manifest& manifest, std::string& saved);
	void ExportAll(const Pack& pack);
	void SetError(const std::string& fileName, const std::string& error, const std::string& saved);

	std::filesystem::path folder;
	char name[65] = "";
	heightmap::Format format = heightmap::Format::PNG16;
	bool exportMaterials = true;
	bool exportBiomes = true;
	std::string status;
	bool statusIsError = false;
};
