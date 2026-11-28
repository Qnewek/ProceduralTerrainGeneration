#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "ExportManifest.h"
#include "Grid.h"

//Mesh files of an export pack: a file per level of detail of the terrain with its tiles as nodes (terrain_<x>_<z>_LOD<n>) and a file with
//the water surfaces. Works only on data handed to it, so it can run on a background thread (no OpenGL).
namespace meshexport {
	enum class Format { GLB, OBJ };

	struct Settings {
		std::string name{};				//pack name, the start of every file name
		Format format = Format::GLB;
		int lodLevels = 3;				//LOD 0 = the full grid, each next level aims at a quarter of the triangles of the previous one
		float tileMetres = 256.0f;		//<= 0 = one tile per level
		float metresPerUnit = 1.0f;		//metres of height value 1
		float metresPerTexel = 1.0f;	//metres between neighbouring map samples
		std::string terrainTexture{};		//base color image of the terrain (a file of the pack, glTF only); empty = plain grey
		bool water = false;				//write the water file (if it has anything)
		std::optional<float> seaLevel{};	//height value of the sea surface, a quad over the map; none = no sea
		std::vector<glm::vec3> riverWater{};	//triangles (3 vertices each, metres) of the rivers and lakes; may be empty
	};

	struct Result {
		bool ok = false;
		std::string error{};
		std::vector<exportpack::File> files{};
		std::vector<exportpack::MeshLevel> levels{};
		std::string waterFile{};			//empty = none written
		std::vector<std::string> waterNodes{};
		double ms = 0.0;
	};

	//Builds and writes the meshes of a map into folder
	Result Write(const HeightField& map, const Settings& settings, const std::filesystem::path& folder);

	//Extension of the files of a format, with the dot
	inline const char* Extension(Format format) { return format == Format::GLB ? ".glb" : ".obj"; }
}
