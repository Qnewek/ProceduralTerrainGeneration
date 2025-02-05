#pragma once

#include <iostream>
#include <chrono>
#include <type_traits>
#include <optional>

#include "glm/glm.hpp"

#include "Noise.h"
#include "Erosion.h"
#include "TerrainGenerator.h"
#include "Object.h"

namespace utilities
{
	//Different utility functions
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height);
	void SimpleMeshIndicies(unsigned int* indices, int width, int height);
	void GenCubeLayout(float* vertices, unsigned int* indices, float scalingFactor);
    void ParseNoiseIntoVerticesetationLevelRef(float* vertices, int width, int height, float* map, float scalingFactor, unsigned int stride, unsigned int offset);
    void ParseNoiseChunksIntoVertices(float* vertices, int width, int height, int chunkX, int chunkY, float* map, float scalingFactor, unsigned int stride, unsigned int offset);
    bool CreateIndicesTiledField(unsigned int* indices, int width, int height);
    bool CreateTiledVertices(float* vertices, int width, int height, float* map, float scalingFactor, unsigned int stride, unsigned int offset);
    void PaintNotByTexture(float* vertices, int width, int height, unsigned int stride, unsigned int offset);
    object::Object* LoadObj(const std::string& dirPath, const std::string& name);
	bool saveToObj(const std::string& dirPath, const std::string& name, float* vertices, unsigned int* indices, unsigned int stride, unsigned int indexSize, unsigned int verticesCount, bool mtl);

	//Functions for dealing with 3D vectors
    bool InitializeNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount);
    bool CalculateNormals(float* vertices, unsigned int* indices, unsigned int stride, unsigned int offSet, unsigned int indexSize);
	void AddVector3f(float* vertices, unsigned int index, glm::vec3 vector3f);
	bool NormalizeVector3f(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount);
	
	//Terrain generation functions
    void GenerateTerrainMap(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, unsigned int stride);
    void CreateTerrainMesh(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, float scalingFactor, unsigned int stride, bool normals, bool first);
    void PerformErosion(float* vertices, unsigned int* indices, float scalingFactor, std::optional<float*> Track, int stride, int positionsOffset, int normalsOffset, erosion::Erosion& erosion);
    void PaintBiome(float* vertices, float* map, int width, int height, unsigned int stride, unsigned int offset);
	void AssignBiome(float* vertices, int* biomeMap, int width, int height, unsigned int stride, unsigned int offset);
    void AssignTexturesByBiomes(TerrainGenerator& terraGen, float* vertices, int width, int height, int texAtlasSize, unsigned int stride, unsigned int offset);

	//Benchmarking function
    template <typename Func, typename... Args>
    void benchmarkVoid(Func func, std::string funcName, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();

        func(std::forward<Args>(args)...);

        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "[LOG] Function '"<<funcName<<"' took: " << duration.count() << " ms" << std::endl;
    }
}