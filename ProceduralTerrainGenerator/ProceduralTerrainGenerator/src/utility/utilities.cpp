#include "utilities.h"

#include <math.h>

#include <iostream>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace utilities
{
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height) {
		for (int i = 0; i < width * height; ++i) {
			image[i] = static_cast<unsigned char>(data[i] * 255.0f);
		}
	}
	void GenCubeLayout(float* vertices, unsigned int* indices) {
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

	void CreateTerrainMesh(noise::SimplexNoiseClass &noise, float* vertices, unsigned int* indices, unsigned int stride, bool normals, bool first)
	{
		noise.generateFractalNoise();
		parseNoiseIntoVertices(vertices, noise, stride, 0);
		if (first)
			SimpleMeshIndicies(indices, noise.getWidth(), noise.getHeight());
		if (normals) {
			InitializeNormals(vertices, stride, 3, noise.getHeight() * noise.getWidth());
			CalculateNormals (vertices, indices, stride, 3, (noise.getWidth() - 1) * (noise.getHeight() - 1) * 6);
			NormalizeVector3f(vertices, stride, 3, noise.getWidth() * noise.getHeight());
		}
	}

	void parseNoiseIntoVertices(float* vertices, noise::SimplexNoiseClass &noise, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < noise.getHeight(); y++)
		{
			for (int x = 0; x < noise.getWidth(); x++)
			{
				vertices[((y * noise.getWidth()) + x) * stride + offset    ] = x / (float)noise.getWidth();
				vertices[((y * noise.getWidth()) + x) * stride + offset + 1] = noise.getMap()[y * noise.getWidth() + x];
				vertices[((y * noise.getWidth()) + x) * stride + offset + 2] = y / (float)noise.getHeight();
			}
		}
	}

	void PerformErosion(float* vertices, std::optional<float*> Track, int stride, int offset, float* map, erosion::Erosion& erosion) {
		erosion.Erode(map, Track);
		for (int y = 0; y < erosion.getHeight(); y++) {
			for (int x = 0; x < erosion.getWidth(); x++) {
				vertices[((y * erosion.getWidth()) + x) * stride + offset] = map[y * erosion.getWidth() + x];
			}
		}
	}

	void SimpleMeshIndicies(unsigned int* indices, int width, int height) {
		int index = 0;
		for (int y = 0; y < height - 1; y++) {
			for (int x = 0; x < width - 1; x++) {
				indices[index++] = x + (y * width);
				indices[index++] = x + ((y + 1) * width);
				indices[index++] = x + 1 + (y * width);

				indices[index++] = x + 1 + (y * width);
				indices[index++] = x + ((y + 1) * width);
				indices[index++] = x + 1 + ((y + 1) * width);
			}
		}
	}

	void PaintBiome(float* vertices, noise::SimplexNoiseClass &noiseHeights, noise::SimplexNoiseClass &noiseBiome, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < noiseHeights.getHeight(); y++) {
			for (int x = 0; x < noiseHeights.getWidth(); x++)
			{
				vertices[((y * noiseHeights.getWidth()) + x) * stride + offset]		= noiseBiome.getMap()	[y * noiseHeights.getWidth() + x] > 0.0f ? noiseBiome.getMap()	[y * noiseHeights.getWidth() + x] : 0.0f;
				vertices[((y * noiseHeights.getWidth()) + x) * stride + offset + 1] = noiseHeights.getMap()	[y * noiseHeights.getWidth() + x] > 0.0f ? noiseHeights.getMap()[y * noiseHeights.getWidth() + x] : 0.0f;
			}
		}
	}

	void InitializeNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount) {
		for (int i = 0; i < verticesCount; i++) {
			vertices[i * stride + offSet] = 0.0f;
			vertices[i * stride + offSet + 1] = 0.0f;
			vertices[i * stride + offSet + 2] = 0.0f;
		}
	}

	void CalculateNormals(float* vertices, unsigned int* indices, unsigned int stride, unsigned int offSet, unsigned int indexSize) {
		if (indexSize % 3 != 0) {
			std::cout << "Index size is not a multiple of 3, hence its not a set of triangles" << std::endl;
			return;
		}
		glm::vec3 tmp;
		for (int i = 0; i < indexSize; i += 3) {
			tmp = glm::vec3(
				vertices[indices[i] * stride],
				vertices[indices[i] * stride + 1],
				vertices[indices[i] * stride + 2]);
			tmp = glm::cross(
				glm::vec3(
					vertices[indices[i + 1] * stride],
					vertices[indices[i + 1] * stride + 1],
					vertices[indices[i + 1] * stride + 2]) - tmp,
				glm::vec3(
					vertices[indices[i + 2] * stride],
					vertices[indices[i + 2] * stride + 1],
					vertices[indices[i + 2] * stride + 2]) - tmp);
			AddVector3f(vertices, indices[i] * stride + offSet, tmp);
			AddVector3f(vertices, indices[i + 1] * stride + offSet, tmp);
			AddVector3f(vertices, indices[i + 2] * stride + offSet, tmp);
		}
	}
	//Requires floats in vertices representing a normal vector to be initialized with some value (Func initializeNomals {0.0f, 0.0f, 0.0f)
	void AddVector3f(float* vertices, unsigned int index, glm::vec3 vector3f) {
		vertices[index] += vector3f.x;
		vertices[index + 1] += vector3f.y;
		vertices[index + 2] += vector3f.z;
	}

	void NormalizeVector3f(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount) {
		glm::vec3 tmp;
		for (int i = 0; i < verticesCount; i++) {
			tmp = glm::vec3(
				vertices[i * stride + offSet],
				vertices[i * stride + offSet + 1],
				vertices[i * stride + offSet + 2]);
			tmp = glm::normalize(tmp);
			vertices[i * stride + offSet] = tmp.x;
			vertices[i * stride + offSet + 1] = tmp.y;
			vertices[i * stride + offSet + 2] = tmp.z;
		}
	}

}


