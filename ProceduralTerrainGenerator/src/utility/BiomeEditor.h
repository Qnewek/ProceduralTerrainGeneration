#pragma once

#include <string>
#include <vector>

#include "Biome.h"

//ImGui editor of the biome table (BiomeConfig::table): list of biomes, adding, removing and reordering them,
//and the name, color, the five value ranges, the terrain modifier and the surface materials of the selected one. It edits the table in place;
//the subsystem regenerates the biomes when ImGuiDraw reports a change.
class BiomeEditor
{
public:
	//@return true if the table changed in a way that changes the biome map (anything but a name)
	bool ImGuiDraw(std::vector<biome::Biome>& table);

	//Forgets the selection, for a table replaced from outside (a loaded preset)
	void Reset();

	//Names of the surface materials in the order of their ids (TerrainMaterials); without them the materials show as numbers
	void SetMaterialNames(std::vector<std::string> names);

private:
	//Biomes as rectangles of their temperature and humidity ranges; click selects, the selected one can be dragged
	//@return true if the ranges of the selected biome changed
	bool WhittakerDiagram(std::vector<biome::Biome>& table);
	//Material layers of a biome: material and weight of each; keeps at least one weight above 0
	//@return true if a layer changed
	bool MaterialLayers(biome::Biome& b);

	//Index of the selected biome; kept inside the table every frame
	int selected = 0;
	std::vector<std::string> materialNames;
};
