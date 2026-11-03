#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "WorldConfig.h"

//Saving and loading generation parameters as JSON files.
//Enums are stored by name, so reordering them does not break existing files. A field missing from a file keeps
//its default value, which lets older presets load after new parameters are added; unknown fields are ignored.
//Render settings (height scale, display mode) and biome definitions are not part of a preset.
namespace preset {
	std::string ToJson(const WorldConfig& config);
	std::string ToJson(const NoiseMapConfig& config);

	//Parses and validates a preset. Values that generation cannot handle are rejected here
	//@param out - replaced only on success
	//@param error - description of the problem on failure
	//@return true on success
	bool FromJson(const std::string& text, WorldConfig& out, std::string& error);
	bool FromJson(const std::string& text, NoiseMapConfig& out, std::string& error);

	//Writes a validated config, creating missing folders. An existing file is replaced only after a successful write
	//@param error - description of the problem on failure
	//@return true on success
	bool Save(const std::filesystem::path& path, const WorldConfig& config, std::string& error);
	bool Save(const std::filesystem::path& path, const NoiseMapConfig& config, std::string& error);

	//@param out - replaced only on success
	//@param error - description of the problem on failure
	//@return true on success
	bool Load(const std::filesystem::path& path, WorldConfig& out, std::string& error);
	bool Load(const std::filesystem::path& path, NoiseMapConfig& out, std::string& error);

	//A name safe to use as a file name: ASCII letters, digits, spaces, '-' and '_', at most 64 characters,
	//no leading or trailing space
	bool IsValidName(const std::string& name);

	//@return .json files in folder sorted by path; empty if the folder does not exist
	std::vector<std::filesystem::path> List(const std::filesystem::path& folder);
}
