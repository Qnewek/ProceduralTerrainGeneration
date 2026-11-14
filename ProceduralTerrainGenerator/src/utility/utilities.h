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
        TEXTURED
	};

	void GenerateVerticesForResolution(float* vertices, const int& width, const int& height, int resolution, const unsigned int& stride, unsigned int posOffset, unsigned int texOffset);

    //ImGui interface functions
    bool NoiseImGui(noise::NoiseConfigParameters& noiseConfig);
	bool MapSizeImGui(int& width, int& height);
    //@param infGen - infinite generation switch, nullptr hides it in modes without generation
    bool DisplayModeImGui(float& modelSclae, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d, bool* infGen);
	//Slider of the terrain tessellation of the 3D view (pixels per triangle edge)
	void TerrainDetailImGui(float& tessPixels);
	bool ImGuiButtonWrapper(const char* label, bool disabled);
}