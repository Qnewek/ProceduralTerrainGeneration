#include "utilities.h"

#include "math.h"

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

	void CreateTerrainMesh(float* mesh, unsigned int* indices, int mapWidth, int mapHeigth, float scale, int octaves, float constrast, noise::Options opt, bool normals, bool first) {
		noise::getNoiseMesh(mesh, mapWidth, mapHeigth, scale, octaves, constrast, opt, normals);
		if(first)
			SimpleMeshIndicies(indices, mapWidth, mapHeigth);
		if (normals) {
			CalculateNormals(mesh, indices, 6, 3, (mapWidth - 1) * (mapHeigth - 1) * 6);
			NormalizeVector3f(mesh, 6, 3, mapWidth * mapHeigth);
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
	//Requires floats in vertices representing a normal vector to be initialized with some value
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


