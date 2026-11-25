#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ExportManifest.h"

//What an export pack needs beyond its maps to be opened in a game engine: the textures of its surface materials and the importer
//scripts (res/engine: Unity editor script, Godot editor plugin). No OpenGL here.
namespace engineexport {
	//Where the textures of one material are and how they are laid on the terrain; one per material id
	struct MaterialTextures {
		std::filesystem::path folder;	//res/textures/terrain/<asset>; may not exist (textures not downloaded)
		float tileMetres = 0.0f;		//metres covered by one repeat of the texture
		float fallbackColor[3] = { 0.5f, 0.5f, 0.5f };	//drawn by the app when the textures are missing
	};

	//Metres covered by one repeat of a material texture as the app draws it: tileSize world units (TerrainView::worldUnitsPerTexel = 4 per texel)
	//times the "Texture scale", on texels of metresPerTexel (materials repeat in texels on imported maps)
	inline float TileMetres(float tileSize, float textureScale, float metresPerTexel)
	{
		constexpr float worldUnitsPerTexel = 4.0f;
		return tileSize * textureScale / worldUnitsPerTexel * metresPerTexel;
	}

	//Copies albedo.*, normal.* and roughness.* (the first file with that stem) of every material on the map (coverage > 0) to
	//materials/<folder name>/ in the pack and sets their paths; every material gets its fallback color and repeat.
	//A missing folder or file only leaves its path empty
	//@param files - the copied files are added
	//@return false if a copy failed (error says which)
	bool CopyMaterialTextures(const std::vector<MaterialTextures>& sources, std::vector<exportpack::Material>& materials,
		const std::filesystem::path& packFolder, std::vector<exportpack::File>& files, std::string& error);

	//Copies the folder of the importer scripts with everything in it to <pack>/engine/
	//@param files - the copied files are added
	bool CopyEngineScripts(const std::filesystem::path& source, const std::filesystem::path& packFolder, std::vector<exportpack::File>& files,
		std::string& error);
}
