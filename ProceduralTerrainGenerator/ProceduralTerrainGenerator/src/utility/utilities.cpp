#include "utilities.h"

#include <math.h>

#include <iostream>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace utilities
{
	//Converts float data to unsigned char image
	//@param data - array of floats to be converted
	//@param image - array of unsigned chars to be filled with data
	//@param width - width of the image
	//@param height - height of the image
	void ConvertToGrayscaleImage(float* data, unsigned char* image, int width, int height) {
		for (int i = 0; i < width * height; ++i) {
			image[i] = static_cast<unsigned char>(data[i] * 255.0f);
		}
	}

	//Generates cube layout for openGL to draw
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	void GenCubeLayout(float* vertices, unsigned int* indices, float scalingFactor) {
		float cubeVertices[] = {
			// Front row
			-scalingFactor, -scalingFactor,  scalingFactor,
			 scalingFactor, -scalingFactor,  scalingFactor,
			 scalingFactor,  scalingFactor,  scalingFactor,
			-scalingFactor,  scalingFactor,  scalingFactor,
			// Back row
			-scalingFactor, -scalingFactor, -scalingFactor,
			 scalingFactor, -scalingFactor, -scalingFactor,
			 scalingFactor,  scalingFactor, -scalingFactor,
			-scalingFactor,  scalingFactor, -scalingFactor
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

	//Parses noise map into vertices for openGL to draw as a mesh, stride is the number of floats per vertex
	// 
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map
	//@param height - height of the noise map
	//@param map - noise map
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data

	void parseNoiseIntoVertices(float* vertices, int width, int height, float* map, float scalingFactor, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset    ] = x / (float)width * scalingFactor;
				vertices[((y * width) + x) * stride + offset + 1] = map[y * width + x] * scalingFactor;
				vertices[((y * width) + x) * stride + offset + 2] = y / (float)height * scalingFactor;
			}
		}
	}

	//Parses noise map into vertices for openGL to draw as a mesh, stride is the number of floats per vertex
	//
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	//@param chunkX - width of the chunk
	//@param chunkY - height of the chunk
	//@param map - noise map
	//@param scalingFactor - scaling factor for the vertices
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void parseNoiseChunksIntoVertices(float* vertices, int width, int height, int chunkX, int chunkY, float* map, float scalingFactor, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < height * chunkY; y++)
		{
			for (int x = 0; x < width * chunkX; x++)
			{
				vertices[((y * width * chunkX) + x) * stride + offset] = x / (float)chunkX * scalingFactor;
				vertices[((y * width * chunkX) + x) * stride + offset + 1] = map[y * width * chunkX + x] * scalingFactor;
				vertices[((y * width * chunkX) + x) * stride + offset + 2] = y / (float)chunkY * scalingFactor;
			}
		}
	}

	//Generates indices for simple mesh by conesting 4 vertices on the one cell of the grid to form 2 triangles
	//@param indices - array of indices to be filled with data
	//@param width - width of the noise map
	//@param height - height of the noise map
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

	void GenerateTerrainMap(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, unsigned int stride) {
		noise.initMap();
		noise.generateFractalNoiseByChunks();
		parseNoiseChunksIntoVertices(vertices, noise.getWidth(), noise.getHeight(), noise.getChunkWidth(), noise.getChunkHeight(), noise.getMap(), 1.0f / noise.getConfigRef().scale, stride, 0);
		SimpleMeshIndicies(indices, noise.getWidth() * noise.getChunkWidth(), noise.getHeight() * noise.getChunkHeight());
		InitializeNormals(vertices, stride, 3, noise.getHeight() * noise.getChunkHeight() * noise.getWidth() * noise.getChunkWidth());
		CalculateNormals(vertices, indices, stride, 3, (noise.getWidth() * noise.getChunkWidth() - 1) * (noise.getHeight() * noise.getChunkHeight() - 1) * 6);
		NormalizeVector3f(vertices, stride, 3, noise.getWidth() * noise.getChunkWidth() * noise.getHeight() * noise.getChunkHeight());
	}

	//Generates terrain map using Perlin Fractal Noise, transforming it into drawable mesh and
	//also dealing with initialization and calculations of normals for lightning purposes
	//@param noise - Perlin noise object
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param stride - number of floats per vertex
	//@param normals - boolean value to determine if normals should be calculated
	//@param first - boolean value to determine if indices should be generated
	void CreateTerrainMesh(noise::SimplexNoiseClass &noise, float* vertices, unsigned int* indices, float scalingFactor, unsigned int stride, bool normals, bool first)
	{
		noise.generateFractalNoise();
		parseNoiseIntoVertices(vertices, noise.getWidth(), noise.getHeight(), noise.getMap(), scalingFactor, stride, 0);
		if (first)
			SimpleMeshIndicies(indices, noise.getWidth(), noise.getHeight());
		if (normals) {
			InitializeNormals(vertices, stride, 3, noise.getHeight() * noise.getWidth());
			CalculateNormals (vertices, indices, stride, 3, (noise.getWidth() - 1) * (noise.getHeight() - 1) * 6);
			NormalizeVector3f(vertices, stride, 3, noise.getWidth() * noise.getHeight());
		}
	}

	//Performs erosion simulation on the terrain map, updating vertices, indices and normals
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param Track - optional array of floats representing the track of the erosion
	//@param stride - number of floats per vertex
	//@param positionsOffset - offset in the vertex array to start with when filling the data
	//@param normalsOffset - offset in the vertex array to start with when filling the normals
	//@param erosion - erosion object
	void PerformErosion(float* vertices, unsigned int* indices, float scalingFactor, std::optional<float*> Track, int stride, int positionsOffset, int normalsOffset, erosion::Erosion& erosion) {
		erosion.Erode(Track);
		parseNoiseIntoVertices(vertices, erosion.getWidth(), erosion.getHeight(), erosion.getMap(), scalingFactor, stride, positionsOffset);
		InitializeNormals(vertices, stride, normalsOffset, erosion.getHeight() * erosion.getWidth());
		CalculateNormals(vertices, indices, stride, normalsOffset, (erosion.getWidth() - 1) * (erosion.getHeight() - 1) * 6);
		NormalizeVector3f(vertices, stride, normalsOffset, erosion.getWidth() * erosion.getHeight());
	}

	//Generates basic Perlin Fractal Noise and sets coords for texture sampling (painting biome)
	//Its a very basic function, yet could be usefull for some simple terrain generation
	//@param vertices - array of vertices to be filled with data
	//@param map - noise map
	//@param width - width of the noise map
	//@param height - height of the noise map
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void PaintBiome(float* vertices, float* map, int width, int height, unsigned int stride, unsigned int offset) {
		noise::SimplexNoiseClass* noiseBiome = new noise::SimplexNoiseClass();
		noiseBiome->setMapSize(width, height);
		noiseBiome->initMap();
		noiseBiome->generateFractalNoise();
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset]		= noiseBiome->getMap()[y * width + x] > 0.0f ? noiseBiome->getMap()[y * width + x] : 0.0f;
				vertices[((y * width) + x) * stride + offset + 1] = map	[y * width + x] > 0.0f ? map[y * width + x] : 0.0f;
			}
		}
		delete noiseBiome;
	}
	//The functions sets the coords vertices of texture sampling what can be used as a condition
	//for painting the terrain not based on the texture despite having the texture sampling layout of vertices
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map
	//@param height - height of the noise map
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void PaintNotByTexture(float* vertices, int width, int height, unsigned int stride, unsigned int offset) {
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset] = -1.0f;
				vertices[((y * width) + x) * stride + offset + 1] = -1.0f;
			}
		}
	}

	//Initializes normals for the vertices to be 0.0f, its needed for the AddVector3f function
	//@param vertices - array of vertices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the data
	//@param verticesCount - number of vertices
	void InitializeNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int verticesCount) {
		for (int i = 0; i < verticesCount; i++) {
			vertices[i * stride + offSet] = 0.0f;
			vertices[i * stride + offSet + 1] = 0.0f;
			vertices[i * stride + offSet + 2] = 0.0f;
		}
	}

	//Calculates normals for the vertices based on the indices of the triangles
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the data
	//@param indexSize - number of indices
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
	//Adds vector3f to the normal vector in the vertices array
	//@param vertices - array of vertices to be filled with data
	//@param index - index of the vertex
	//@param vector3f - vector to be added to the normal vector
	void AddVector3f(float* vertices, unsigned int index, glm::vec3 vector3f) {
		vertices[index] += vector3f.x;
		vertices[index + 1] += vector3f.y;
		vertices[index + 2] += vector3f.z;
	}

	//Normalizes the normal vectors in the vertices array
	//@param vertices - array of vertices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the data
	//@param verticesCount - number of vertices
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


