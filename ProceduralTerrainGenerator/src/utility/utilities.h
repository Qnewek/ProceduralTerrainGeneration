#pragma once

#include <iostream>
#include <chrono>
#include <type_traits>
#include <optional>

#include "glm/glm.hpp"

#include "Noise.h"
#include "Erosion.h"
#include "TerrainGenerator.h"
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

	void ConvertToGrayscaleImage(float* data, unsigned char* image, const int& width, const int& height);
    void ParseNoiseIntoVertices(float* vertices, float* map, const int& width, const int& height, float scale, const unsigned int stride, unsigned int offset);
	void GenerateVerticesForResolution(float* vertices, const int& height, const int& width, int resolution, const unsigned int& stride, unsigned int posOffset, unsigned int texOffset);
    void MeshIndicesStrips(unsigned int* indices, const int& width, const int& height);
    bool CalculateHeightMapNormals(float* vertices, const unsigned int& stride, unsigned int offSet, const unsigned int& width, const unsigned int& height);
	bool PaintVerticesByHeight(float* vertices, const int& width, const int& height, const float& heightScale, const unsigned int& stride, heightMapMode m, unsigned int heightOffSet , unsigned int colorOffset);
    std::vector<glm::vec3> GetBiomeColorMap(BiomeGenerator& biomeGen, const int& width, const int& height);

    void MapToVertices(float* map, float* vertices, unsigned int* indices, const int height, const int width, const unsigned int stride, const float& heightScale, heightMapMode mode, bool normalsCalculation, bool indexGeneration, bool paint);
    void PerformErosion(erosion::Erosion& erosion, float* vertices, float scalingFactor, std::optional<float*> Track, int stride, heightMapMode mode);
    
    //ImGui interface functions
    bool NoiseImGui(noise::NoiseConfigParameters& noiseConfig);
	bool MapSizeImGui(int& height, int& width);
    bool DisplayModeImGui(float& modelSclae, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d, bool& infGen);
	bool SavingImGui();
	bool ImGuiButtonWrapper(const char* label, bool disabled);

    //-----
	//Other
    //-----
    template <typename Func, typename... Args>
    void benchmarkVoid(Func func, std::string funcName, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();

        func(std::forward<Args>(args)...);

        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "[LOG] Function '"<<funcName<<"' took: " << duration.count() << " ms" << std::endl;
    }
}