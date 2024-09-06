#pragma once

#include <iostream>
#include <chrono>
#include <type_traits>

namespace utilities
{
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height);

    template <typename Func, typename... Args>
    void benchmark_void(Func func, std::string funcName, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();

        func(std::forward<Args>(args)...);

        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "Function '"<<funcName<<"' took: " << duration.count() << " ms" << std::endl;
    }
}