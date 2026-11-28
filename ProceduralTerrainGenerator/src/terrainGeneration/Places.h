#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "glm/glm.hpp"

//Named places of a generation mode, kept as one JSON file per mode: where the camera stood and looked and where the partial map lay.
//They do not hold the generation parameters, so a place shows the same terrain only with the same parameters (preset).
namespace places {
	struct Place {
		std::string name{};
		glm::vec3 camera = glm::vec3(0.0f);		//camera position in world units before Model scale
		float yaw = -90.0f;						//camera angles in degrees, as Camera keeps them
		float pitch = 0.0f;
		glm::ivec2 mapOffset = glm::ivec2(0);	//world map position of texel (0, 0) of the partial map
	};

	//Longest name in bytes
	constexpr size_t maxNameLength = 64;
	//Most places a file can hold
	constexpr size_t maxPlaces = 256;

	std::string ToJson(const std::vector<Place>& list);

	//Parses and validates a list of places
	//@param out - replaced only on success
	//@param error - description of the problem on failure
	//@return true on success
	bool FromJson(const std::string& text, std::vector<Place>& out, std::string& error);

	//Writes the list, creating missing folders. An existing file is replaced only after a successful write
	//@param error - description of the problem on failure
	//@return true on success
	bool Save(const std::filesystem::path& path, const std::vector<Place>& list, std::string& error);

	//A missing file is an empty list
	//@param out - replaced only on success
	//@param error - description of the problem on failure
	//@return true on success
	bool Load(const std::filesystem::path& path, std::vector<Place>& out, std::string& error);
}
