#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Grid.h"
#include "HeightmapExport.h"

//Reading height maps from files. The format is chosen by the file extension; a new format needs a reader in
//HeightmapImport.cpp and its extension added there. Rows are read from y = 0, the same order Export writes them.
namespace heightmap {
	struct ImportInfo {
		int bitsPerSample = 0;			//precision stored in the file
		bool convertedFromColor = false;	//a color PNG was turned into greyscale (mean of R, G and B)
	};

	//Files do not store the value range, so the caller chooses what the lowest and the highest sample mean.
	//Use the range the map was exported with to get back the same values.
	//@param range - values the sample range is mapped to
	//@param out - replaced only on success
	//@param info - details of the file, set on success
	//@param error - description of the problem on failure
	//@return true on success
	bool Import(const std::filesystem::path& path, ValueRange range, HeightField& out, ImportInfo& info, std::string& error);

	//@return files in folder with a supported extension, sorted by path; empty if the folder does not exist
	std::vector<std::filesystem::path> ListImportable(const std::filesystem::path& folder);
}
