#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "HeightmapExport.h"

//ImGui "Saving" section writing height maps to files in one folder
class ExportPanel
{
public:
	//One map to write, each one goes to its own file
	struct Target {
		const HeightField& map;
		const char* suffix;		//added to the file name, e.g. "_eroded"
	};

	//@param _folder - folder with the exports, created by the first export
	explicit ExportPanel(std::filesystem::path _folder);

	//@param targets - maps written by one export
	//@param range - values mapped to the 16-bit range
	void ImGuiDraw(const std::vector<Target>& targets, heightmap::ValueRange range);

private:
	std::filesystem::path PathOf(const Target& target) const;
	void ExportAll(const std::vector<Target>& targets, heightmap::ValueRange range);

	std::filesystem::path folder;
	char name[65] = "";
	heightmap::Format format = heightmap::Format::PNG16;
	std::string status;
	bool statusIsError = false;
};
