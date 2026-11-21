#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "WorldConfig.h"

//Saving and loading generation parameters as JSON files.
//Enums are stored by name, so reordering them does not break existing files. A field missing from a file keeps
//its default value, which lets older presets load after new parameters are added; unknown fields are ignored.
//Render settings (height scale, display mode) and biome definitions are not part of a preset.
//A preset may also hold where the partial map lies in the world ("mapOffset": world map position of its texel (0, 0));
//it is optional, presets without it load as before.
namespace preset {
	//Largest |coordinate| of a map offset: generation adds texel positions to it in floats, which hold whole numbers exactly up to 2^24
	constexpr int maxMapOffset = 1 << 24;
	bool ValidMapOffset(glm::ivec2 offset);

	//@param mapOffset - written when given
	std::string ToJson(const WorldConfig& config, std::optional<glm::ivec2> mapOffset = std::nullopt);
	std::string ToJson(const NoiseMapConfig& config, std::optional<glm::ivec2> mapOffset = std::nullopt);

	//Parses and validates a preset. Values that generation cannot handle are rejected here
	//@param out - replaced only on success
	//@param mapOffset - replaced only on success: the map offset of the preset, nullopt if it has none
	//@param error - description of the problem on failure
	//@return true on success
	bool FromJson(const std::string& text, WorldConfig& out, std::string& error);
	bool FromJson(const std::string& text, NoiseMapConfig& out, std::string& error);
	bool FromJson(const std::string& text, WorldConfig& out, std::optional<glm::ivec2>& mapOffset, std::string& error);
	bool FromJson(const std::string& text, NoiseMapConfig& out, std::optional<glm::ivec2>& mapOffset, std::string& error);

	//Writes a validated config, creating missing folders. An existing file is replaced only after a successful write
	//@param mapOffset - written when given
	//@param error - description of the problem on failure
	//@return true on success
	bool Save(const std::filesystem::path& path, const WorldConfig& config, std::string& error);
	bool Save(const std::filesystem::path& path, const NoiseMapConfig& config, std::string& error);
	bool Save(const std::filesystem::path& path, const WorldConfig& config, std::optional<glm::ivec2> mapOffset, std::string& error);
	bool Save(const std::filesystem::path& path, const NoiseMapConfig& config, std::optional<glm::ivec2> mapOffset, std::string& error);

	//@param out - replaced only on success
	//@param mapOffset - replaced only on success: the map offset of the preset, nullopt if it has none
	//@param error - description of the problem on failure
	//@return true on success
	bool Load(const std::filesystem::path& path, WorldConfig& out, std::string& error);
	bool Load(const std::filesystem::path& path, NoiseMapConfig& out, std::string& error);
	bool Load(const std::filesystem::path& path, WorldConfig& out, std::optional<glm::ivec2>& mapOffset, std::string& error);
	bool Load(const std::filesystem::path& path, NoiseMapConfig& out, std::optional<glm::ivec2>& mapOffset, std::string& error);

	//A name safe to use as a file name: ASCII letters, digits, spaces, '-' and '_', at most 64 characters,
	//no leading or trailing space
	bool IsValidName(const std::string& name);

	//River settings alone as JSON (the "rivers" object of a world preset), e.g. for a map project
	std::string RiversJson(const rivers::RiverConfig& config);
	//@param out - replaced only on success; fields missing in the text keep their default values
	//@param error - description of the problem on failure (not JSON, a field of a wrong type, a value generation cannot use)
	bool RiversFromJson(const std::string& text, rivers::RiverConfig& out, std::string& error);

	//@return .json files in folder sorted by path; empty if the folder does not exist
	std::vector<std::filesystem::path> List(const std::filesystem::path& folder);
}
