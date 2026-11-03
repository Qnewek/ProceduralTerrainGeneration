#pragma once

#include <vector>

#include "glm/glm.hpp"

#include "Noise.h"
#include "BiomeGenerator.h"

namespace utilities
{
    enum class heightMapMode
    {
        GREYSCALE,
        TOPOGRAPHICAL,
        MONOCOLOR,
        BIOMES
	};

	void GenerateVerticesForResolution(float* vertices, const int& width, const int& height, int resolution, const unsigned int& stride, unsigned int posOffset, unsigned int texOffset);
    std::vector<glm::vec3> GetBiomeColorMap(BiomeGenerator& biomeGen, const int& width, const int& height);

    //ImGui interface functions
    bool NoiseImGui(noise::NoiseConfigParameters& noiseConfig);
	bool MapSizeImGui(int& width, int& height);
    //@param infGen - infinite generation switch, nullptr hides it in modes without generation
    bool DisplayModeImGui(float& modelSclae, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d, bool* infGen);
	bool ImGuiButtonWrapper(const char* label, bool disabled);
}