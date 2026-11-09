#pragma once

#include <vector>

#include "Biome.h"

//ImGui editor of the biome table (BiomeConfig::table): list of biomes, adding, removing and reordering them,
//and the name, color, the five value ranges and the terrain modifier of the selected one. It edits the table in place;
//the subsystem regenerates the biomes when ImGuiDraw reports a change.
class BiomeEditor
{
public:
	//@return true if the table changed in a way that changes the biome map (anything but a name)
	bool ImGuiDraw(std::vector<biome::Biome>& table);

	//Forgets the selection, for a table replaced from outside (a loaded preset)
	void Reset();

private:
	//Biomes as rectangles of their temperature and humidity ranges; click selects, the selected one can be dragged
	//@return true if the ranges of the selected biome changed
	bool WhittakerDiagram(std::vector<biome::Biome>& table);

	//Index of the selected biome; kept inside the table every frame
	int selected = 0;
};
