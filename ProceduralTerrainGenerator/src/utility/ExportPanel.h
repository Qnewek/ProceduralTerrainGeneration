#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ExportManifest.h"
#include "HeightmapExport.h"

//ImGui "Saving" section writing an export pack: a folder <exports>/<name>/ with the height maps and <name>.json describing them
//(exportpack::Manifest)
class ExportPanel
{
public:
	//One height map to write, each one goes to its own file
	struct Target {
		int width, height;
		const char* suffix;		//added to the file name and the content, e.g. "_eroded_pipes"; "" for the main map
		std::function<HeightField()> read;	//called only when exporting, because a map may have to come from the GPU
	};

	//What the subsystem exports
	struct Pack {
		const char* mode = "";				//generation mode, stored in the manifest
		heightmap::ValueRange range{ 0.0f, 1.0f };	//values mapped to the 16-bit range
		float metresPerUnit = 1.0f;			//metres of height value 1 (display height scale / world units per metre)
		std::optional<float> seaLevel;		//height value of the sea surface, if the mode has a sea
		std::function<std::string()> generation;	//generation parameters as preset JSON, called only when exporting
		std::vector<Target> heightmaps;		//the first one is the main map
	};

	//@param _folder - folder holding the packs, created by the first export
	explicit ExportPanel(std::filesystem::path _folder);

	void ImGuiDraw(const Pack& pack);

private:
	std::filesystem::path PackFolder() const { return folder / name; }
	std::string HeightFileName(const Target& target) const;
	void ExportAll(const Pack& pack);

	std::filesystem::path folder;
	char name[65] = "";
	heightmap::Format format = heightmap::Format::PNG16;
	std::string status;
	bool statusIsError = false;
};
