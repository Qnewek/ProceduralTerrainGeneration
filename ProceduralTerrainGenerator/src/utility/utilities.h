#pragma once

#include <vector>

#include "glm/glm.hpp"

#include "Noise.h"

namespace utilities
{
    //Values match displayMode in Terrain_evalTes.shader (TEXTURED: MODE_TEXTURED in Terrain_fragment.shader)
    enum class heightMapMode
    {
        GREYSCALE,
        TOPOGRAPHICAL,
        MONOCOLOR,
        BIOMES,
        SLOPE,
        CURVATURE,
        TEXTURED,
        LAND_COVER		//classes of the land cover (Imported map with land cover, TerrainView::SetLandCover)
	};

	void GenerateVerticesForResolution(float* vertices, const int& width, const int& height, int resolution, const unsigned int& stride, unsigned int posOffset, unsigned int texOffset);

    //ImGui interface functions
    bool NoiseImGui(noise::NoiseConfigParameters& noiseConfig);
	bool MapSizeImGui(int& width, int& height);
    //@param infGen - infinite generation switch, nullptr hides it in modes without generation
    //@param landCover - LAND_COVER is offered (a class map is shown)
    bool DisplayModeImGui(float& modelSclae, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d, bool* infGen,
        bool landCover = false);
	//Slider of the terrain tessellation of the 3D view (pixels per triangle edge)
	void TerrainDetailImGui(float& tessPixels);
	bool ImGuiButtonWrapper(const char* label, bool disabled);
	//Combo of the coordinate systems a map can be placed in (geo::ChoosableSystems); @return true if the choice changed
	bool SystemComboImGui(const char* label, int& epsg);
}