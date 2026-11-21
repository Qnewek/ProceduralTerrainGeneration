#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "Grid.h"

//Project file of the "Imported map" mode (.ptgmap): the working copy of a map and its original, in metres, with what the
//mode needs to show them again. Layout: 8 bytes of signature, uint32 (little-endian) length of a JSON header, the header,
//then the blocks it lists. A block is a zlib stream of the floats of a map split into 4 byte planes (plane k = byte k of
//every float), which compresses height maps much better than the floats as they are.
namespace mapproject {
	struct Project {
		HeightField working;			//metres
		HeightField original;			//metres; empty = the same as working
		glm::dvec2 pixelMetres{ 1.0 };	//size of one map pixel
		std::optional<float> seaLevel;	//metres; none = no sea
		std::string source;				//where the map came from: a file name or the generation mode and the map position
		std::string generation;			//generation parameters as preset JSON; empty = none
		float verticalExaggeration = 1.0f;	//of the view, against the true proportions
		std::string rivers;				//river settings as JSON (preset::RiversJson); empty = defaults, no rivers
	};

	//2 (2026-11-21): rivers; version 1 is still read
	inline constexpr int currentVersion = 2;
	inline constexpr const char* extension = ".ptgmap";

	//@param out - the bytes of the file, replaced on success
	//@param error - description of the problem on failure (an empty or too large map, an original of another size, a value that is not finite)
	bool Serialize(const Project& project, std::vector<uint8_t>& out, std::string& error);
	//@param out - replaced only on success
	bool Deserialize(const uint8_t* data, size_t size, Project& out, std::string& error);

	//Writes through a temporary file, so a failed save keeps the previous file
	bool Save(const std::filesystem::path& path, const Project& project, std::string& error);
	//@param out - replaced only on success
	bool Load(const std::filesystem::path& path, Project& out, std::string& error);

	//@return path ending with the project extension: added when the path has another one or none (the save dialog
	//takes "dem.v2" as a name with the extension ".v2")
	std::filesystem::path WithExtension(const std::filesystem::path& path);

	//@return .ptgmap files in folder, sorted by path; empty if the folder does not exist
	std::vector<std::filesystem::path> ListProjects(const std::filesystem::path& folder);
}
