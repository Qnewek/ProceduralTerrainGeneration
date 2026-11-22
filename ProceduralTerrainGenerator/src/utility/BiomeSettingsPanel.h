#pragma once

#include "Biome.h"
#include "WorldConfig.h"

//ImGui sections of the biome settings shared by TerrainGen and Imported map (the biome table has its own editor, BiomeEditor).
//Each one edits the config in place and returns true when a value changed
namespace biomepanel {
	//Blend width of the biome colors
	bool Blend(BiomeConfig& config);
	//"Climate": lapse rate, coast humidity and its range
	bool Climate(biome::Climate& climate);
	//"Surface materials": material blend width and the patches
	//@param showSeed - also the detail seed, which chooses the pattern of the patches (TerrainGen shows it with the terrain modifiers)
	bool Materials(BiomeConfig& config, bool showSeed);
}
