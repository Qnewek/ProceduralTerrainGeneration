#include "TerrainGenerationSys.h"

#include <iostream>

#include "imgui/imgui.h"
#include "ImPlot/implot.h"

TerrainGenerationSys::TerrainGenerationSys() : terrainVertices(nullptr), terrainIndices(nullptr), noiseVertices(nullptr),
heightScale(1.0f), modelScale(1.0f), stride(9), width(0), height(0), terrainGen(), biomeGen() {

}
TerrainGenerationSys::~TerrainGenerationSys() {
	if(terrainVertices) {
		delete[] terrainVertices;
		terrainVertices = nullptr;
	}
	if(terrainIndices) {
		delete[] terrainIndices;
		terrainIndices = nullptr;
	}
}
bool TerrainGenerationSys::Initialize(unsigned int _height, unsigned int _width, float _heightScale) {
	mainVAO = std::make_unique<VertexArray>();
	mainShader = std::make_unique<Shader>("res/shaders/Terrain_vertex.shader", "res/shaders/Terrain_fragment.shader");

	layout.Push<float>(3);
	layout.Push<float>(3);
	layout.Push<float>(3);

	height = _height;
	width = _width;
	heightScale = _heightScale;
	
	terrainGen.Initialize(width, height);
	biomeGen.Initialize(width, height);
	GenerateTerrain();

	std::cout << "[LOG] TerrainGenerationSys initialized\n";
	return true;

}
bool TerrainGenerationSys::GenerateTerrain() {
	if(!terrainVertices || terrainGen.GetHeight() != height || terrainGen.GetWidth() != width) {
		if(terrainVertices) {
			delete[] terrainVertices;
		}
		if(terrainIndices) {
			delete[] terrainIndices;
		}
		if(noiseVertices) {
			delete[] noiseVertices;
		}
		if (!terrainGen.Resize(width, height)) {
			std::cout << "[ERROR] TerrainGen size couldnt be set\n";
			return false;
		}
		terrainVertices = new float[width * height * stride];
		noiseVertices = new float[width * height * stride];
		terrainIndices = new unsigned int[(height - 1) * width * 2]; // indices for strips probably will be changed

		utilities::MeshIndicesStrips(terrainIndices, width, height);
		mainIndexBuffer = std::make_unique<IndexBuffer>(terrainIndices, (height - 1) * width * 2);;
	}

	terrainGen.GenerateTerrain();
	utilities::MapToVertices(terrainGen.GetHeightMap(), terrainVertices, terrainIndices, height, width, stride, heightScale, displayMode, true, false, true);
	UpdateVertex(mainVertexBuffer, terrainVertices);
	return true;
}
bool TerrainGenerationSys::GenerateBiomes()
{
	if(!biomeGen.Biomify(terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::CONTINENTALNESS), 
		terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS), 
		terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::WEIRDNESS))) {
		std::cout << "[ERROR] Biomes couldnt be generated\n";
	}
	return true;
}
void TerrainGenerationSys::UpdateVertex(std::unique_ptr<VertexBuffer>& vb, float* v)
{
	vb = std::make_unique<VertexBuffer>(v, width * height * stride * sizeof(float));
	mainVAO->AddBuffer(*vb, layout);
}
void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(modelScale, modelScale, modelScale));
	light.SetLightUniforms(*mainShader);
	camera.SetUniforms(*mainShader);
	mainShader->SetModel(model);
	mainShader->SetUniform1i("flatten", map2d);
	mainShader->SetUniform1i("size", height/2);

	renderer.DrawTriangleStrips(*mainVAO, *mainIndexBuffer, *mainShader, height- 1, width * 2);
}
void TerrainGenerationSys::ImGuiRightPanel() {
	if(utilities::MapSizeImGui(height, width)) {
		GenerateTerrain();
	}

	if (ImGui::CollapsingHeader("Terrain settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool regenerate = false;
		ImGui::Text("Evaluating method");
		static int evaluatingOption = 0;
		const char* methodOptions[] = { "Linear", "Spline" };
		if (ImGui::BeginCombo("Method: ", methodOptions[evaluatingOption]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(methodOptions); n++)
			{
				bool is_selected = (evaluatingOption == n);
				if (ImGui::Selectable(methodOptions[n], is_selected)) {
					evaluatingOption = n;
					terrainGen.GetEvaluationMethod() = static_cast<TerrainGenerator::EvaluationMethod>(evaluatingOption);
					changeTerrain = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SliderInt("Sampling resolution", &terrainGen.GetResolitionRef(), 100, 1000);
		if (ImGui::Button("Change resolution")) {
			terrainGen.SetResolution();
			GenerateTerrain();
		}
		ImGui::Text("Edit component noise:");
		static int editedNoise = 0;
		const char* noiseOptions[] = { "None", "Continentalness", "Mountainousness", "WEIRDNESS" };
		if (ImGui::BeginCombo("Noise: ", noiseOptions[editedNoise]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(noiseOptions); n++)
			{
				bool is_selected = (editedNoise == n);
				if (ImGui::Selectable(noiseOptions[n], is_selected)) {
					editedNoise = n;
					regenerate = true;
					biomesGeneration = false;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (editedNoise != 0) {
			TerrainGenerator::WorldGenParameter param = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
			switch (editedNoise) {
			case 1:
				param = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
				break;
			case 2:
				param = TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS;
				break;
			case 3:
				param = TerrainGenerator::WorldGenParameter::WEIRDNESS;
				break;
			default:
				break;
			}
			if (utilities::NoiseImGui(terrainGen.GetSelectedNoiseConfig(param)) || regenerate) {
				terrainGen.GetSelectedNoise(param).GenerateFractalNoise();
				utilities::MapToVertices(terrainGen.GetSelectedNoise(param).GetMap(), noiseVertices, terrainIndices, height, width, stride, heightScale, displayMode, true, false, true);
				UpdateVertex(noiseVertexBuffer, noiseVertices);
				changeTerrain = true;
			}
		}
		else {
			SplineEditor();
			BiomesImGui();
			if (changeTerrain) {
				GenerateTerrain();
				biomeGen.Regenerate();
				changeTerrain = false;
			}
			else {
				mainVAO->AddBuffer(*mainVertexBuffer, layout);
			}
		}
	}
}
void TerrainGenerationSys::ImGuiLeftPanel() {
	// Trash variables for function call
		float topoStep = 0.1f, topoBandWidth = 0.05f;
	//
	if (utilities::DisplayModeImGui(modelScale, topoStep, topoBandWidth, heightScale, displayMode, wireFrame, map2d)) {
		utilities::PaintVerticesByHeight(terrainVertices, width, height, heightScale, stride, displayMode, 1, 6);
		UpdateVertex(mainVertexBuffer, terrainVertices);
		biomesGeneration = false;
	}
	utilities::SavingImGui();
}
void TerrainGenerationSys::ImGuiOutput() {

}

void TerrainGenerationSys::BiomesImGui()
{
	if (ImGui::CollapsingHeader("Biome generation settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("Biomes generation", &biomesGeneration)) {
			if (biomesGeneration) {
				if (!biomeGen.IsGenerated()) {
					GenerateBiomes();
				}
				utilities::PaintVerticesByBiome(terrainVertices, biomeGen, width, height, stride, 6);
			}
			else {
				utilities::PaintVerticesByHeight(terrainVertices, width, height, heightScale, stride, displayMode, 1, 6);
			}
			UpdateVertex(mainVertexBuffer, terrainVertices);
		}
		if (biomesGeneration) {

		}
	}
}

void TerrainGenerationSys::SplineEditor()
{
	static int splinePressedButton = 0;
	if (ImGui::CollapsingHeader("Spline editor", ImGuiTreeNodeFlags_DefaultOpen)) {
		if(ImGui::Checkbox("Edit spline", &editSpline)) {
			splinePressedButton = 0;
		}
		if (editSpline) {

			ImGui::Text("Select spline to edit:");	
			if(utilities::ImGuiButtonWrapper("Mountainousness", splinePressedButton == 1 ? true : false)) {
				splinePressedButton = 1;
				editedSpline = TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS;
				splinePlotPoints = terrainGen.GetSplinePoints(editedSpline);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness", splinePressedButton == 2 ? true : false)) {
				splinePressedButton = 2;
				editedSpline = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
				splinePlotPoints = terrainGen.GetSplinePoints(editedSpline);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Weirdness", splinePressedButton == 3 ? true : false)) {
				splinePressedButton = 3;
				editedSpline = TerrainGenerator::WorldGenParameter::WEIRDNESS;
				splinePlotPoints = terrainGen.GetSplinePoints(editedSpline);
			}

			if(splinePressedButton == 0) {
				ImGui::Text("[Currently no spline is beeing changed]");
				return;
			}

			static int dragging = -1;
			if(splinePlotPoints.size() != 2 || splinePlotPoints[0].size() != splinePlotPoints[1].size()) {
				std::cout << "[ERROR] Spline points not set correctly\n";
				return;
			}

			const double xmin = -1.0, xmax = 1.0;
			const double ymin = 0.0, ymax = 1.0;
			const double pick_radius = 0.08;

			if (ImPlot::BeginPlot("##plot", ImVec2(-1, 320), ImPlotFlags_NoMenus)) {
				ImPlot::SetupAxis(ImAxis_X1, "X", ImPlotAxisFlags_Lock);
				ImPlot::SetupAxis(ImAxis_Y1, "Y", ImPlotAxisFlags_Lock);
				ImPlot::SetupAxisLimits(ImAxis_X1, xmin, xmax, ImGuiCond_Always);
				ImPlot::SetupAxisLimits(ImAxis_Y1, ymin, ymax, ImGuiCond_Always);

				std::vector<double> xs, ys; xs.reserve(splinePlotPoints[0].size()); ys.reserve(splinePlotPoints[1].size());
				for (auto& p : splinePlotPoints[0]) { xs.push_back(p); }
				for (auto& p : splinePlotPoints[1]) { ys.push_back(p); }
				ImPlot::PlotLine("Spline (lin.)", xs.data(), ys.data(), (int)splinePlotPoints[0].size());
				ImPlot::PlotScatter("Spline points", xs.data(), ys.data(), (int)splinePlotPoints[0].size());

				ImPlotPoint mouse = ImPlot::GetPlotMousePos();
				if (dragging == -1 && ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
					for (int i = 0; i < (int)splinePlotPoints[0].size(); ++i) {
						double dx = (double)mouse.x - splinePlotPoints[0][i];
						double dy = (double)mouse.y - splinePlotPoints[1][i];
						if (dx * dx + dy * dy <= pick_radius * pick_radius) {
							dragging = i;
							break;
						}
					}
				}

				if (dragging != -1) {
					if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
						if (dragging == 0 || dragging == (int)splinePlotPoints[0].size() - 1) {
							splinePlotPoints[1][dragging] = std::clamp((double)mouse.y, ymin, ymax);
						}
						else {
							double xmin_drag = splinePlotPoints[0][dragging - 1] + 0.01;
							double xmax_drag = splinePlotPoints[0][dragging + 1] - 0.01;

							splinePlotPoints[0][dragging] = std::clamp((double)mouse.x, xmin_drag, xmax_drag);
							splinePlotPoints[1][dragging] = std::clamp((double)mouse.y, ymin, ymax);
						}
					}
					else {
						dragging = -1;
					}
				}

				if (dragging == -1 && ImPlot::IsPlotHovered()) {
					for (int i = 0; i < splinePlotPoints[0].size(); i++) {
						double dx = (double)mouse.x - splinePlotPoints[0][i], dy = (double)mouse.y - splinePlotPoints[1][i];
						if (dx * dx + dy * dy <= pick_radius * pick_radius) {
							ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
							break;
						}
					}
				}
				ImPlot::EndPlot();
			}
			if (ImGui::Button("Set new spline points")) {
				terrainGen.SetSpline(editedSpline, splinePlotPoints);
				changeTerrain = true;
			}
		}
	}
}