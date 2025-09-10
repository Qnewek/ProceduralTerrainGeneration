#define TINYOBJLOADER_IMPLEMENTATION

#include "utilities.h"

#include <math.h>
#include <fstream>
#include <sstream>
#include <GL/glew.h>

#include <iostream>
#include "glm/glm.hpp"
#include "imgui/imgui.h"
#include "glm/gtc/matrix_transform.hpp"
#include "ObjLoader/tiny_obj_loader.h"

namespace utilities
{
	//Converts float data to unsigned char image
	//@param data - array of floats to be converted
	//@param image - array of unsigned chars to be filled with data
	//@param width - width of the image
	//@param height - height of the image
	void ConvertToGrayscaleImage(float* data, unsigned char* image, const int& width, const int& height) {
		for (int i = 0; i < width * height; i++) {
			image[i] = static_cast<unsigned char>(data[i] * 255.0f);
		}
	}

	//Parses noise map into vertices for openGL to draw as a mesh, stride is the number of floats per vertex
	//@param vertices - array of vertices to be filled with data
	//@param map - noise map
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	//@param scale - scaling factor for generating height values
	//@param stride - number of floats per vertex
	//@param offset - offset in the vertex array to start with when filling the data
	void ParseNoiseIntoVertices(float* vertices, float* map, const int& width, const int& height, float scale, const unsigned int stride, unsigned int offset){
		if (!vertices) {
			std::cout << "[ERROR] Vertices array not initialized" << std::endl;
			return;
		}
		if(!map) {
			std::cout << "[ERROR] Noise map not initialized" << std::endl;
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

	void GenerateVerticesForResolution(float* vertices, const int& height, const int& width, int resolution, const unsigned int& stride, unsigned int posOffset, unsigned int texOffset){
		for (int i = 0; i < resolution; i++)
		{
			for (int j = 0; j < resolution; j++)
			{
				//Bottom left
				vertices[(i * resolution + j) * 4 * stride + (stride * 0) + posOffset + 0] = -width / 2.0f + (i) * (float)width / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 0) + posOffset + 1] = 0.0f;
				vertices[(i * resolution + j) * 4 * stride + (stride * 0) + posOffset + 2] = -height / 2.0f + (j) * (float)height / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 0) + texOffset + 0] = (float)(i) / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 0) + texOffset + 1] = (float)(j) / (float)(resolution);
				//Bottom right
				vertices[(i * resolution + j) * 4 * stride + (stride * 1) + posOffset + 0] = -width / 2.0f + (i + 1) * (float)width / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 1) + posOffset + 1] = 0.0f;
				vertices[(i * resolution + j) * 4 * stride + (stride * 1) + posOffset + 2] = -height / 2.0f + (j) * (float)height / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 1) + texOffset + 0] = (float)(i + 1) / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 1) + texOffset + 1] = (float)(j) / (float)(resolution);
				//Top left
				vertices[(i * resolution + j) * 4 * stride + (stride * 2) + posOffset + 0] = -width / 2.0f + (i) * (float)width / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 2) + posOffset + 1] = 0.0f;
				vertices[(i * resolution + j) * 4 * stride + (stride * 2) + posOffset + 2] = -height / 2.0f + (j+1) * (float)height / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 2) + texOffset + 0] = (float)(i) / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 2) + texOffset + 1] = (float)(j + 1) / (float)(resolution);
				//Top right
				vertices[(i * resolution + j) * 4 * stride + (stride * 3) + posOffset + 0] = -width / 2.0f + (i + 1) * (float)width / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 3) + posOffset + 1] = 0.0f;
				vertices[(i * resolution + j) * 4 * stride + (stride * 3) + posOffset + 2] = -height / 2.0f + (j + 1) * (float)height / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 3) + texOffset + 0] = (float)(i + 1) / (float)(resolution);
				vertices[(i * resolution + j) * 4 * stride + (stride * 3) + texOffset + 1] = (float)(j + 1) / (float)(resolution);
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
	void MeshIndicesStrips(unsigned int* indices, const int& width, const int& height) {
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
	bool CalculateHeightMapNormals(float* vertices, const unsigned int& stride, unsigned int offSet, const unsigned int& width, const unsigned int& height)
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
		else if(m == heightMapMode::MONOCOLOR) {
			glm::vec3 monoColor = glm::vec3(0.6f, 0.6f, 0.6f);
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					vertices[(y * width + x) * stride + colorOffset] = monoColor.r;
					vertices[(y * width + x) * stride + colorOffset + 1] = monoColor.g;
					vertices[(y * width + x) * stride + colorOffset + 2] = monoColor.b;
				}
			}
			std::cout << "[LOG] Monocolor painting applied" << std::endl;
		}
		else {
			std::cout << "[LOG] Painting mode not recognized" << std::endl;
		}
		return true;
	}


	//Paints vertices based on their biome, using the biome generator to determine the color
	//@param vertices - array of vertices to be filled with data
	//@param biomeGen - biome generator object
	//@param width - width of the noise
	//@param height - height of the noise map
	//@param stride - number of floats per vertex
	//@param colorOffset - offset in the vertex array to start with when filling the color data
	std::vector<glm::vec3> GetBiomeColorMap(BiomeGenerator& biomeGen, const int& width, const int& height)
	{
		if (!biomeGen.IsGenerated()) {
			std::cout << "[ERROR] Biome map not generated!" << std::endl;
			return {};
		}

		std::vector<glm::vec3> colors(width * height);

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int id = biomeGen.GetBiomeAt(x, y);
				if (id < 0) {
					std::cout << "[ERROR] Biome not found!\n";
					return {};
				}
				glm::vec3 color = biomeGen.GetBiome(id).GetColor();
				colors[y * width + x] = color;
			}
		}
		return colors;
	}

	//Generates terrain map using Perlin Fractal Noise, transforming it into drawable mesh and
	//also dealing with initialization and calculations of normals for lightning purposes
	//@param map - noise map
	//@param vertices - array of vertices to be filled with data
	//@param indices - array of indices to be filled with data
	//@param height - height of the noise map in chunks
	//@param width - width of the noise map in chunks
	//@param stride - number of floats per vertex
	//@param heightScale - scaling factor for generating height values
	//@param mode - mode of coloring (grayscale or topographical)
	//@param normalsCalculation - boolean value indicating if normals should be calculated
	//@param indexGeneration - boolean value indicating if indices should be generated
	//@param paint - boolean value indicating if painting should be applied
	void MapToVertices(float* map, float* vertices, unsigned int* indices, int const height, const int width, const unsigned int stride, const float& heightScale, heightMapMode mode, bool normalsCalculation, bool indexGeneration, bool paint)
	{
		ParseNoiseIntoVertices(vertices, map, width, height, heightScale, stride, 0);
		if (indexGeneration)
			MeshIndicesStrips(indices, width, height);
		if (normalsCalculation) {
			CalculateHeightMapNormals(vertices, stride, 3, width, height);
		}
		if (paint) {
			PaintVerticesByHeight(vertices, width, height, heightScale, stride, mode, 1, 6);
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
	void PerformErosion(erosion::Erosion& erosion, float* vertices, float scalingFactor, std::optional<float*> Track, int stride, heightMapMode mode) {
		erosion.Erode(Track);
		ParseNoiseIntoVertices(vertices, erosion.GetMap(), erosion.GetWidth(), erosion.GetHeight(), scalingFactor, stride, 0);
		CalculateHeightMapNormals(vertices, stride, 3, erosion.GetWidth(), erosion.GetHeight());
		PaintVerticesByHeight(vertices, erosion.GetWidth(), erosion.GetHeight(), scalingFactor, stride, mode, 1, 6);
	}

	//ImGui interface for modifying noise parameters
	//@param noise - Perlin noise object
	//@return - boolean value indicating if the noise parameters were modified
	bool NoiseImGui(noise::NoiseConfigParameters& noiseConfig)
	{
		if (ImGui::CollapsingHeader("Noise Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool regenerate = false;
			regenerate |= ImGui::InputInt("Seed", &noiseConfig.seed);
			regenerate |= ImGui::SliderInt("Octaves", &noiseConfig.octaves, 1, 8);
			regenerate |= ImGui::SliderFloat("Offset x", &noiseConfig.xoffset, 0.0f, 5.0f);
			regenerate |= ImGui::SliderFloat("Offset y", &noiseConfig.yoffset, 0.0f, 5.0f);
			regenerate |= ImGui::SliderInt("Resolution", &noiseConfig.resolution, 100, 1000);
			regenerate |= ImGui::SliderFloat("Scale", &noiseConfig.scale, 0.01f, 3.0f);
			regenerate |= ImGui::SliderFloat("Constrast", &noiseConfig.constrast, 0.1f, 2.0f);
			regenerate |= ImGui::SliderFloat("Redistribution", &noiseConfig.redistribution, 0.1f, 10.0f);
			regenerate |= ImGui::SliderFloat("Lacunarity", &noiseConfig.lacunarity, 0.1f, 10.0f);
			regenerate |= ImGui::SliderFloat("Persistance", &noiseConfig.persistance, 0.1f, 1.0f);

			static const char* options[] = { "REFIT_ALL", "FLATTEN_NEGATIVES", "REVERT_NEGATIVES", "NOTHING" };
			int current_option = static_cast<int>(noiseConfig.option);

			if (ImGui::BeginCombo("Negatives: ", options[current_option]))
			{
				for (int n = 0; n < IM_ARRAYSIZE(options); n++)
				{
					bool is_selected = (current_option == n);
					if (ImGui::Selectable(options[n], is_selected)) {
						current_option = n;
						noiseConfig.option = static_cast<noise::Options>(n);
						regenerate = true;
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (noiseConfig.option == noise::Options::REVERT_NEGATIVES)
				regenerate |= ImGui::SliderFloat("Revert Gain", &noiseConfig.revertGain, 0.1f, 1.0f);

			// Ridged noise settings
			regenerate |= ImGui::Checkbox("Ridge", &noiseConfig.Ridge);
			if (noiseConfig.Ridge)
			{
				regenerate |= ImGui::SliderFloat("Ridge Gain", &noiseConfig.RidgeGain, 0.1f, 10.0f);
				regenerate |= ImGui::SliderFloat("Ridge Offset", &noiseConfig.RidgeOffset, 0.1f, 10.0f);
			}

			// Island settings
			regenerate |= ImGui::Checkbox("Island", &noiseConfig.island);
			if (noiseConfig.island)
			{
				static const char* islandTypes[] = { "CONE", "DIAGONAL", "EUKLIDEAN_SQUARED",
													 "SQUARE_BUMP","HYPERBOLOID", "SQUIRCLE",
													 "TRIG" };
				int current_island = static_cast<int>(noiseConfig.islandType);

				if (ImGui::BeginCombo("Island type: ", islandTypes[current_island]))
				{
					for (int n = 0; n < IM_ARRAYSIZE(islandTypes); n++)
					{
						bool is_selected = (current_island == n);
						if (ImGui::Selectable(islandTypes[n], is_selected)) {
							current_island = n;
							noiseConfig.islandType = static_cast<noise::IslandType>(n);
							regenerate = true;
						}
						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				regenerate |= ImGui::SliderFloat("Mix Power", &noiseConfig.mixPower, 0.0f, 1.0f);
			}
			return regenerate;
		}
	}
	//ImGui interface for modifying map size
	//@param height - height of the noise map in chunks
	//@param width - width of the noise map in chunks
	bool MapSizeImGui(int& height, int& width)
	{
		if (ImGui::CollapsingHeader("Size settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Resize map");
			ImGui::InputInt("Height", &height);
			ImGui::InputInt("Width", &width);
			if (ImGui::Button("Resize")) {
				return true;
			}
		}
		return false;
	}
	bool DisplayModeImGui(float& modelScale, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d)
	{
		if (ImGui::CollapsingHeader("Display settings:", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Model scale:");
			ImGui::SliderFloat("Model scale", &modelScale, 0.1f, 5.0f);
			ImGui::Text("Isopleth:");
			ImGui::SliderFloat("Step", &topoStep, 1.0f, heightScale);
			ImGui::SliderFloat("Band width", &topoBandWidth, 0.1f, 1.0f);

			static const char* displayOptions[] = { "GREYSCALE", "TOPOGRAPHICAL", "MONOCOLOR", "BIOMES"};
			int currentDisplayOption = static_cast<int>(m);
			bool paint = false;

			if (ImGui::BeginCombo("Mode", displayOptions[currentDisplayOption]))
			{
				for (int n = 0; n < IM_ARRAYSIZE(displayOptions); n++)
				{
					bool is_selected = (currentDisplayOption == n);
					if (ImGui::Selectable(displayOptions[n], is_selected)) {
						currentDisplayOption = n;
						m = static_cast<utilities::heightMapMode>(n);
						paint = true;
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Checkbox("WireFrame", &wireFrame)) {
				if (wireFrame) {
					glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
					glDisable(GL_CULL_FACE);
				}
				else {
					glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
					//glEnable(GL_CULL_FACE);
				}
			}

			ImGui::Checkbox("2D map", &map2d);
			return paint;
		}
	}
	bool SavingImGui()
	{
		if (ImGui::CollapsingHeader("Saving")) {
			if (ImGui::Button("Save heightMap as an image")) {
				std::cout << "[LOG] Well it will work one day i promise!" << std::endl;
			}
			if (ImGui::Button("Save heightMap as an 3d object")) {
				std::cout << "[LOG] Well it will work one day i promise!" << std::endl;
			}
		}
		return true;
	}
	bool ImGuiButtonWrapper(const char* label, bool disabled)
	{
		if (disabled) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.8f, 0.1f, 1.0f));       
			ImGui::BeginDisabled();
		}

		bool clicked = ImGui::Button(label);

		if (disabled) {
			ImGui::PopStyleColor(1);
			ImGui::EndDisabled();
		}
		return clicked;
	}
}