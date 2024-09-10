#include "utilities.h"

#include "math.h"

namespace utilities
{
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height) {
		for (int i = 0; i < width * height; ++i) {
			image[i] = static_cast<unsigned char>(data[i] * 255.0f);
		}
	}
	void GenCubeLayout(float* vertices, unsigned int* indices) {

		vertices = new float[24];
		indices = new unsigned int[36];

        float cubeVertices[] = {
            // Front row
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            // Back row
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f
        };

		//Cube indices
        unsigned int cubeIndices[] = {
            // Front 
            0, 1, 2, 2, 3, 0,
            // Back
            4, 5, 6, 6, 7, 4,
            // Left
            4, 0, 3, 3, 7, 4,
            // Right
            1, 5, 6, 6, 2, 1,
            // Top
            3, 2, 6, 6, 7, 3,
            // Bottom
            4, 5, 1, 1, 0, 4
        };

        for (int i = 0; i < 24; ++i) {
            vertices[i] = cubeVertices[i];
        }

        for (int i = 0; i < 36; ++i) {
            indices[i] = cubeIndices[i];
        }
	}

	void SimpleMeshIndicies(unsigned int* indices, int width, int height) {
		int index = 0;
		for (int y = 0; y < height - 1; y++) {
			for (int x = 0; x < width - 1; x++) {
				indices[index++] = x + y * width;
				indices[index++] = x + (y + 1) * width;
				indices[index++] = x + 1 + y * width;

				indices[index++] = x + 1 + y * width;
				indices[index++] = x + (y + 1) * width;
				indices[index++] = x + 1 + (y + 1) * width;
			}
		}
	}
}


