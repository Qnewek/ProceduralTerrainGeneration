#pragma once

#include <iostream>
#include <chrono>
#include <type_traits>

#include "glm/glm.hpp"

#include "Noise.h"

namespace utilities
{
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height);
	void SimpleMeshIndicies(unsigned int* indices, int width, int height);
	void GenCubeLayout(float* vertices, unsigned int* indices);
    void CalculateNormals(float* vertices, unsigned int* indices, unsigned int stride, unsigned int offSet, unsigned int indexSize);
	void AddVector3f(float* vertices, unsigned int index, glm::vec3 vector3f);
	void NormalizeVector3f(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount);
	void CreateTerrainMesh(float* mesh, unsigned int* indices, int mapWidth, int mapHeigth, float scale, int octaves, float constrast, noise::Options opt, bool normals, bool first);

    template <typename Func, typename... Args>
    void benchmark_void(Func func, std::string funcName, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();

        func(std::forward<Args>(args)...);

        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "Function '"<<funcName<<"' took: " << duration.count() << " ms" << std::endl;
    }
}