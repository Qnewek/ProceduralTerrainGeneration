#pragma once

#include <iostream>
#include <chrono>
#include <type_traits>
#include <optional>

#include "glm/glm.hpp"

#include "Noise.h"
#include "Erosion.h"
#include "TerrainGenerator.h"

namespace utilities
{
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height);
    void ParseNoiseIntoVertices(float* vertices, float* map, int width, int height, float scale, unsigned int stride, unsigned int offset);
	void MeshIndicesStrips(unsigned int* indices, int width, int height);
    bool CalculateHeightMapNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int width, unsigned int height);
   
    void CreateTerrainMesh(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, float scalingFactor, unsigned int stride, bool normals, bool first);
    void PerformErosion(erosion::Erosion& erosion, float* vertices, float scalingFactor, std::optional<float*> Track, int stride);
    
    


    bool CreateIndicesTiledField(unsigned int* indices, int width, int height);
    bool CreateTiledVertices(float* vertices, int width, int height, float* map, float scalingFactor, unsigned int stride, unsigned int offset);
    void PaintNotByTexture(float* vertices, int width, int height, unsigned int stride, unsigned int offset);
	bool saveToObj(const std::string& dirPath, const std::string& name, float* vertices, unsigned int* indices, unsigned int stride, unsigned int indexSize, unsigned int verticesCount, bool mtl);
    void GenerateTerrainMap(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, unsigned int stride);
    void PaintBiome(float* vertices, float* map, int width, int height, unsigned int stride, unsigned int offset);
	void AssignBiome(float* vertices, int* biomeMap, int width, int height, unsigned int stride, unsigned int offset);
    void AssignTexturesByBiomes(TerrainGenerator& terraGen, float* vertices, int width, int height, int texAtlasSize, unsigned int stride, unsigned int offset);



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