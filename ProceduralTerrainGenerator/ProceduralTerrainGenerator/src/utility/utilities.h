#pragma once

#include <iostream>
#include <chrono>
#include <type_traits>

#include "glm/glm.hpp"

#include "Noise.h"
#include "Erosion.h"

namespace utilities
{
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height);
	void SimpleMeshIndicies(unsigned int* indices, int width, int height);
	void GenCubeLayout(float* vertices, unsigned int* indices);
    void parseNoiseIntoVertices(float* vertices, noise::SimplexNoiseClass& noise, unsigned int stride, unsigned int offset);

    void InitializeNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount);
    void CalculateNormals(float* vertices, unsigned int* indices, unsigned int stride, unsigned int offSet, unsigned int indexSize);
	void AddVector3f(float* vertices, unsigned int index, glm::vec3 vector3f);
	void NormalizeVector3f(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount);
	
    void CreateTerrainMesh(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, unsigned int stride, bool normals, bool first);
	void PaintBiome(float* vertices, noise::SimplexNoiseClass& noiseHeights, noise::SimplexNoiseClass& noiseBiome, unsigned int stride, unsigned int offset);
    void PerformErosion(float* vertices, int stride, int offset, float* map, erosion::Erosion& erosion);

    template <typename Func, typename... Args>
    void benchmark_void(Func func, std::string funcName, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();

        func(std::forward<Args>(args)...);

        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "[LOG] Function '"<<funcName<<"' took: " << duration.count() << " ms" << std::endl;
    }
}