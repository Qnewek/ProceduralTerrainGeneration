#pragma once

#include <string>
#include <vector>

#include "Biome.h"
#include "VegetationPlacement.h"

//ImGui editor of the biome table (BiomeConfig::table): list of biomes, adding, removing and reordering them,
//and the name, color, the value ranges, the terrain modifier, the surface materials and the plants of the selected one. It edits the table in place;
//the subsystem regenerates the biomes when ImGuiDraw reports a change.
class BiomeEditor
{
public:
	//@return true if the table changed in a way that changes the biome map (anything but the name and the plants;
	//the vegetation notices changed plants itself)
	bool ImGuiDraw(std::vector<biome::Biome>& table);

	//Forgets the selection, for a table replaced from outside (a loaded preset)
	void Reset();

	//Names of the surface materials in the order of their ids (TerrainMaterials); without them the materials show as numbers
	void SetMaterialNames(std::vector<std::string> names);

	//A model plants can name (vegetation_models.json)
	struct PlantModel {
		std::string name;
		vegetation::Layer layer = vegetation::Layer::TREES;
	};
	//Models offered for plants; a plant may still name one not in the list (it shows as unknown)
	void SetPlantModels(std::vector<PlantModel> models);

private:
	//Biomes as rectangles of their temperature and humidity ranges; click selects, the selected one can be dragged
	//@return true if the ranges of the selected biome changed
	bool WhittakerDiagram(std::vector<biome::Biome>& table);
	//Material layers of a biome: material and weight of each; keeps at least one weight above 0
	//@return true if a layer changed
	bool MaterialLayers(biome::Biome& b);
	//Plants of a biome: adding, removing, reordering and their settings
	void Plants(biome::Biome& b);

	//Index of the selected biome; kept inside the table every frame
	int selected = 0;
	std::vector<std::string> materialNames;
	std::vector<PlantModel> plantModels;
};
