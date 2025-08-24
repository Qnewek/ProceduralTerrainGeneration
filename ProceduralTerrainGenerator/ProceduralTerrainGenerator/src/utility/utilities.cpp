#define TINYOBJLOADER_IMPLEMENTATION

#include "utilities.h"

#include <math.h>
#include <fstream>
#include <sstream>

#include <iostream>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ObjLoader/tiny_obj_loader.h"

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

	//Parses noise map into vertices for openGL to draw as a mesh, stride is the number of floats per vertex
	//
	//@param vertices - array of vertices to be filled with data
	//@param map - noise map
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	//@param scale - scaling factor for generating height values
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void ParseNoiseIntoVertices(float* vertices, float* map, int width, int height, float scale, unsigned int stride, unsigned int offset) {
		if (!vertices || !map) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return;
		}
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				vertices[((y * width) + x) * stride + offset] = -width / 2.0f + x;
				vertices[((y * width) + x) * stride + offset + 1] = map[y * width + x] * scale;
				vertices[((y * width) + x) * stride + offset + 2] = -height / 2.0f + y;
			}
		}
	}

	//Generates indices for a mesh, by dividing the mesh into strips of triangles
	//Method of indexing vertices in the grid shown below:
	// 0---2
	// | / |
	// 1---3
	//@param indices - pointer to the array of indices to be filled with index data
	//@param width - width of the noise map (columns)
	//@param height - height of the noise map (rows)
	void MeshIndicesStrips(unsigned int* indices, int width, int height) {
		int index = 0;
		for (int y = 0; y < height - 1; y++) {
			for (int x = 0; x < width; x++) {
				for (int k = 0; k < 2; k++) {
					indices[index++] = width * (y + k) + x;
				}
			}
		}
	}

	//Calculates normals for the height map based on the vertices and their positions
	//This function uses the cross product of the horizontal and vertical vectors of neighbouring vertices to calculate the normal vector
	//@param vertices - array of vertices to be filled with data
	//@param stride - number of floats per vertex
	//@param offSet - offset in the vertex array to start with when filling the normals
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	bool CalculateHeightMapNormals(float* vertices, unsigned int stride, unsigned int offSet, unsigned int width, unsigned int height)
	{
		if (!vertices) {
			return false;
		}
		glm::vec3 tmp(0.0f, 0.0f, 0.0f);
		glm::vec3 vx(0.0f, 0.0f, 0.0f);
		glm::vec3 vz(0.0f, 0.0f, 0.0f);
		for (int z = 0; z < height; z++) {
			for (int x = 0; x < width; x++) {
				//Vertical vector
				if (height == 1) {
					vz = glm::vec3(0.0f, 0.0f, 0.0f);
				}
				else if (z == 0) {
					vz = glm::vec3(0.0f, vertices[((z + 1) * width + x) * stride + 1] - vertices[(z * width + x) * stride + 1], 1.0f);
				}
				else if (z == height - 1) {
					vz = glm::vec3(0.0f, vertices[((z - 1) * width + x) * stride + 1] - vertices[(z * width + x) * stride + 1], 1.0f);
				}
				else
				{
					vz = glm::vec3(0.0f, vertices[((z + 1) * width + x) * stride + 1] - vertices[((z - 1) * width + x) * stride + 1], 2.0f);
				}
				//Horizontal vector
				if (width == 1) {
					vx = glm::vec3(0.0f, 0.0f, 0.0f);
				}
				else if (x == 0) {
					vx = glm::vec3(1.0f, vertices[(z * width + x + 1) * stride + 1] - vertices[(z * width + x) * stride + 1], 0.0f);
				}
				else if (x == width - 1) {
					vx = glm::vec3(1.0f, vertices[(z * width + x - 1) * stride + 1] - vertices[(z * width + x) * stride + 1], 0.0f);
				}
				else
				{
					vx = glm::vec3(2.0f, vertices[(z * width + x + 1) * stride + 1] - vertices[(z * width + x - 1) * stride + 1], 0.0f);
				}
				//Cross product to get the normal vector and normalize it
				tmp = glm::normalize(glm::cross(vz, vx));
				//Set the normal vector to the vertex
				vertices[(z * width + x) * stride + offSet] = tmp.x;
				vertices[(z * width + x) * stride + offSet + 1] = tmp.y;
				vertices[(z * width + x) * stride + offSet + 2] = tmp.z;
			}
		}
		return true;
	}

	//Generates a color based on the height value, using a gradient from blue to green to red
	//@param hNorm - normalized height value (0 to 1)
	glm::vec3 getTopoColor(float hNorm) {
		// Od niebieskiego (0) przez zielony (0.5) do czerwonego (1)
		if (hNorm < 0.5f)
			return glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), hNorm * 2.0f);
		else
			return glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), (hNorm - 0.5f) * 2.0f);
	}

	//Paints vertices based on their height, using either grayscale or topographical coloring
	//@param vertices - array of vertices to be filled with data
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	//@param heightScale - scaling factor for generating height values
	//@param stride - number of floats per vertex
	//@param m - mode of coloring (grayscale or topographical)
	//@param heightOffSet - offset in the vertex array to start with when filling the height data
	//@param colorOffset - offset in the vertex array to start with when filling the color data
	//@param topo_Step - step value for topographical coloring
	bool PaintVerticesByHeight(float* vertices, const int& width, const int& height, const float& heightScale, const unsigned int& stride, heightMapMode m, unsigned int heightOffSet, unsigned int colorOffset)
	{
		if (!vertices) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return false;
		}
		if (m == heightMapMode::GREYSCALE) {
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					float h = vertices[(y * width + x) * stride + heightOffSet];
					float hNorm = std::clamp((h + 16.0f) / heightScale, 0.0f, 1.0f);

					vertices[(y * width + x) * stride + colorOffset] = hNorm;
					vertices[(y * width + x) * stride + colorOffset + 1] = hNorm;
					vertices[(y * width + x) * stride + colorOffset + 2] = hNorm;
				}
			}
			std::cout << "[LOG] Greyscale painting applied" << std::endl;
		}
		else if (m == heightMapMode::TOPOGRAPHICAL) {
			float bandWidth = 0.2f;
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					float h = vertices[(y * width + x) * stride + heightOffSet];
					float hNorm = std::clamp((h + 16.0f) / heightScale, 0.0f, 1.0f);

					glm::vec3 topoColor = getTopoColor(hNorm);

					vertices[(y * width + x) * stride + colorOffset] = topoColor.r;
					vertices[(y * width + x) * stride + colorOffset + 1] = topoColor.g;
					vertices[(y * width + x) * stride + colorOffset + 2] = topoColor.b;
				}
			}
			std::cout << "[LOG] Topographical painting applied" << std::endl;
		}
		else {
			std::cout << "[LOG] Painting mode not recognized" << std::endl;
		}
		return true;
	}

	//Generates terrain map using Perlin Fractal Noise, transforming it into drawable mesh and
	//also dealing with initialization and calculations of normals for lightning purposes
	//@param noise - Perlin noise object
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param stride - number of floats per vertex
	//@param normals - boolean value to determine if normals should be calculated
	//@param first - boolean value to determine if indices should be generated
	void CreateTerrainMesh(noise::SimplexNoiseClass& noise, float* vertices, unsigned int* indices, float scalingFactor, unsigned int stride, bool normals, bool first, heightMapMode mode)
	{
		noise.GenerateFractalNoise();
		ParseNoiseIntoVertices(vertices, noise.GetMap(), noise.GetWidth(), noise.GetHeight(), scalingFactor, stride, 0);
		if (first)
			MeshIndicesStrips(indices, noise.GetWidth(), noise.GetHeight());
		if (normals) {
			CalculateHeightMapNormals(vertices, stride, 3, noise.GetWidth(), noise.GetHeight());
		}
		PaintVerticesByHeight(vertices, noise.GetWidth(), noise.GetHeight(), scalingFactor, stride, mode, 1, 6);
	}

	//Performs erosion simulation on the terrain map, updating vertices, indices and normals
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param Track - optional array of floats representing the track of the erosion
	//@param stride - number of floats per vertex
	//@param positionsOffset - offset in the vertex array to start with when filling the data
	//@param normalsOffset - offset in the vertex array to start with when filling the normals
	//@param erosion - erosion object
	void PerformErosion(erosion::Erosion& erosion, float* vertices, float scalingFactor, std::optional<float*> Track, int stride, heightMapMode mode) {
		erosion.Erode(Track);
		ParseNoiseIntoVertices(vertices, erosion.GetMap(), erosion.GetWidth(), erosion.GetHeight(), scalingFactor, stride, 0);
		CalculateHeightMapNormals(vertices, stride, 3, erosion.GetWidth(), erosion.GetHeight());
		PaintVerticesByHeight(vertices, erosion.GetWidth(), erosion.GetHeight(), scalingFactor, stride, mode, 1, 6);
	}
}