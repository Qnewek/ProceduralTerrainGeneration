#include "utilities.h"

#include <iostream>
#include "glm/glm.hpp"
#include "imgui.h"

namespace utilities
{
	//Fills vertices with a resolution x resolution grid of quad patches (4 vertices each) centred at the origin
	//@param width, height - size of the whole grid in world units
	//@param stride - number of floats per vertex
	//@param posOffset, texOffset - offsets of the position (3 floats) and texture coordinates (2 floats) in a vertex
	void GenerateVerticesForResolution(float* vertices, const int& width, const int& height, int resolution, const unsigned int& stride, unsigned int posOffset, unsigned int texOffset){
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

	//Color of every map cell taken from its biome (RGB, row-major like the map)
	//@param biomeGen - generator holding a generated biome map
	//@param width, height - size of the biome map
	//@return empty if the biome map is not generated or contains an unknown biome
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
		return false;
	}
	//ImGui interface for modifying map size
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	bool MapSizeImGui(int& width, int& height)
	{
		if (ImGui::CollapsingHeader("Size settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Resize map");
			ImGui::InputInt("Width", &width);
			ImGui::InputInt("Height", &height);
			if (ImGui::Button("Resize")) {
				return true;
			}
		}
		return false;
	}
	bool DisplayModeImGui(float& modelScale, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d, bool& infGen)
	{
		if (ImGui::CollapsingHeader("Display settings:", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Model scale:");
			ImGui::SliderFloat("Model scale", &modelScale, 0.1f, 5.0f);
			ImGui::Text("Isopleth:");
			ImGui::SliderFloat("Step", &topoStep, 1.0f, heightScale);
			ImGui::SliderFloat("Band width", &topoBandWidth, 0.1f, 1.0f);

			static const char* displayOptions[] = { "GREYSCALE", "TOPOGRAPHICAL", "MONOCOLOR", "BIOMES"};
			int currentDisplayOption = static_cast<int>(m);
			bool change = false;

			if (ImGui::BeginCombo("Mode", displayOptions[currentDisplayOption]))
			{
				for (int n = 0; n < IM_ARRAYSIZE(displayOptions); n++)
				{
					bool is_selected = (currentDisplayOption == n);
					if (ImGui::Selectable(displayOptions[n], is_selected)) {
						currentDisplayOption = n;
						m = static_cast<utilities::heightMapMode>(n);
						change = true;
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Checkbox("WireFrame", &wireFrame);

			ImGui::Checkbox("2D map", &map2d);
			if (ImGuiButtonWrapper("Partial generation", !infGen)) {
				infGen = !infGen;
			}
			ImGui::SameLine();
			if(ImGuiButtonWrapper("Infinite generation", infGen)) {
				infGen = !infGen;
			}
			return change;
		}
		return false;
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
