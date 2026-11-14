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

	//ImGui interface for modifying noise parameters
	//@param noise - Perlin noise object
	//@return - boolean value indicating if the noise parameters were modified
	bool NoiseImGui(noise::NoiseConfigParameters& noiseConfig)
	{
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

		// Shape operations; 0 turns each of them off
		ImGui::SeparatorText("Shape");
		regenerate |= ImGui::SliderFloat("Warp strength", &noiseConfig.warpStrength, 0.0f, 2.0f);
		ImGui::SetItemTooltip("Domain warping: moves the noise by a second noise, which bends coastlines and mountain ranges.\n"
			"1 = by about one feature of the noise. Has no effect on symmetrical noise");
		if (noiseConfig.warpStrength != 0.0f) {
			regenerate |= ImGui::SliderFloat("Warp scale", &noiseConfig.warpScale, 0.1f, 4.0f);
			ImGui::SetItemTooltip("Frequency of the moving noise relative to this one; higher = finer swirls");
		}
		regenerate |= ImGui::SliderFloat("Slope erosion", &noiseConfig.slopeErosion, 0.0f, 2.0f);
		ImGui::SetItemTooltip("Weakens fine octaves on steep slopes: smooth flanks, details in valleys and on ridges, like eroded terrain");
		regenerate |= ImGui::SliderInt("Terrace steps", &noiseConfig.terraceSteps, 0, 32);
		ImGui::SetItemTooltip("Splits the height into flat ledges, this many per unit of height; 0 = off");
		if (noiseConfig.terraceSteps > 0) {
			regenerate |= ImGui::SliderFloat("Terrace sharpness", &noiseConfig.terraceSharpness, 0.0f, 1.0f);
			ImGui::SetItemTooltip("0 = gentle steps, 1 = flat ledges with steep risers");
		}
		return regenerate;
	}
	//ImGui interface for modifying map size
	//@param width - width of the noise map in chunks
	//@param height - height of the noise map in chunks
	bool MapSizeImGui(int& width, int& height)
	{
		ImGui::SeparatorText("Map size");
		ImGui::InputInt("Width", &width);
		ImGui::InputInt("Height", &height);
		return ImGui::Button("Resize");
	}
	bool DisplayModeImGui(float& modelScale, float& topoStep, float& topoBandWidth, float& heightScale, heightMapMode& m, bool& wireFrame, bool& map2d, bool* infGen)
	{
		if (infGen) {
			if (ImGuiButtonWrapper("Partial generation", !*infGen)) {
				*infGen = !*infGen;
			}
			ImGui::SameLine();
			if (ImGuiButtonWrapper("Infinite generation", *infGen)) {
				*infGen = !*infGen;
			}
		}

		static const char* displayOptions[] = { "GREYSCALE", "TOPOGRAPHICAL", "MONOCOLOR", "BIOMES", "SLOPE", "CURVATURE", "TEXTURED" };
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
		ImGui::SameLine();
		ImGui::Checkbox("2D map", &map2d);
		ImGui::SliderFloat("Model scale", &modelScale, 0.1f, 5.0f);
		//Only the view: the maps stay in [0, 1], so nothing has to be generated again
		ImGui::SliderFloat("Height scale", &heightScale, 16.0f, 4096.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("World units of height 1 (a texel is 4 units wide)");

		ImGui::SeparatorText("Isopleth");
		ImGui::SliderFloat("Step", &topoStep, 1.0f, heightScale);
		ImGui::SliderFloat("Band width", &topoBandWidth, 0.1f, 1.0f);
		return change;
	}
	void TerrainDetailImGui(float& tessPixels)
	{
		//Reversed range: to the right = more detail
		ImGui::SliderFloat("Terrain detail", &tessPixels, 24.0f, 2.0f, "%.1f px per triangle", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("3D view: terrain triangles are about this many pixels wide at any distance (at most 64 per patch side).\n"
			"Fewer pixels = sharper distant terrain and more GPU time. The 2D map is not affected");
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
