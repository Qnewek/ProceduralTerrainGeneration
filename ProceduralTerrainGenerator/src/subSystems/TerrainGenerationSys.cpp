#include "TerrainGenerationSys.h"

#include <iostream>

#include "imgui/imgui.h"
#include "ImPlot/implot.h"

TerrainGenerationSys::TerrainGenerationSys() : terrainVertices(nullptr), mapResolution(20),
heightScale(1.0f), modelScale(1.0f), stride(5), width(0), height(0), terrainGen(), biomeGen() {

}
TerrainGenerationSys::~TerrainGenerationSys() {
	if(terrainVertices) {
		delete[] terrainVertices;
		terrainVertices = nullptr;
	}
}
bool TerrainGenerationSys::Initialize(unsigned int _height, unsigned int _width, float _heightScale) {
	mainVAO = std::make_unique<VertexArray>();
	mainShader = std::make_unique<Shader>("res/shaders/TerrainShaders/Terrain_vertex.shader", "res/shaders/TerrainShaders/Terrain_fragment.shader", "res/shaders/TerrainShaders/Terrain_controlTes.shader", "res/shaders/TerrainShaders/Terrain_evalTes.shader");
	layout.Push<float>(3);
	layout.Push<float>(2);
	mainShader->Bind();
	mainShader->SetUniform1i("displayMode", static_cast<int>(displayMode));
	mainShader->SetUniform1i("heightMap", 0);

	height = _height;
	width = _width;
	heightScale = _heightScale;
	
	terrainVertices = new float[mapResolution * mapResolution * stride * 4];
	utilities::GenerateVerticesForResolution(terrainVertices, height, width, mapResolution, stride, 0, 3);
	mainVertexBuffer = std::make_unique<VertexBuffer>(terrainVertices, (mapResolution * mapResolution) * stride * 4 * sizeof(float));
	mainVAO->AddBuffer(*mainVertexBuffer, layout);
	terrainGen.Initialize(width, height);
	biomeGen.Initialize(width, height);
	GenerateTerrain();

	std::cout << "[LOG] TerrainGenerationSys initialized\n";
	return true;

}
bool TerrainGenerationSys::Resize() {
	if (!terrainVertices || terrainGen.GetHeight() != height || terrainGen.GetWidth() != width) {
		if (terrainVertices) {
			delete[] terrainVertices;
		}
		terrainGen.Resize(width, height);
		terrainVertices = new float[mapResolution * mapResolution * stride * 4];
		utilities::GenerateVerticesForResolution(terrainVertices, height, width, mapResolution, stride, 0, 3);
		return  true;
	}
	return false;
}
bool TerrainGenerationSys::GenerateTerrain() {
	Resize();

	if (!terrainGen.GenerateTerrain()) {
		return false;
	}

	terrainTxt = std::make_unique<TextureClass>(terrainGen.GetHeightMap(), width, height);

	return true;
}
bool TerrainGenerationSys::GenerateBiomes()
{
	if (biomeGen.IsGenerated()) {
		return false;
	}
	if(!biomeGen.Biomify(terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::CONTINENTALNESS), 
		terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS), 
		terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::WEIRDNESS))) {
		std::cout << "[ERROR] Biomes couldnt be generated\n";
		return false;
	}
	biomeTxt = std::make_unique<TextureClass>(utilities::GetBiomeColorMap(biomeGen, width, height), width, height);
	return true;
}
void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(modelScale, modelScale, modelScale));
	light.SetLightUniforms(*mainShader);
	camera.SetUniforms(*mainShader);
	mainShader->SetModel(model);
	mainShader->SetUniform1i("size", height / 2);
	mainShader->SetUniform1i("flatten", map2d);
	mainShader->SetUniform1f("heightScale", heightScale);

	terrainTxt->Bind(0);
	if(noiseTxt)
		noiseTxt->Bind(1);
	if (biomesGeneration && biomeTxt) {
		biomeTxt->Bind(2);
		mainShader->SetUniform1i("biomeMap", 2);
	}
	renderer.DrawPatches(*mainVAO, *mainShader, mapResolution * mapResolution, 4);
}
void TerrainGenerationSys::ImGuiRightPanel() {
	if(utilities::MapSizeImGui(height, width)) {
		GenerateTerrain();
	}

	if (ImGui::CollapsingHeader("Terrain settings", ImGuiTreeNodeFlags_DefaultOpen)) {
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
		ImGui::SliderInt("Sampling resolution", &terrainGen.GetResolitionRef(), 10, 1000);
		if (ImGui::Button("Change resolution")) {
			terrainGen.SetResolution();
			GenerateTerrain();
		}
		NoiseEditor();
		if (!editNoise) {
			SplineEditor();
			BiomesEditor();
			if (changeTerrain) {
				GenerateTerrain();
				biomeGen.Regenerate();
				changeTerrain = false;
			}
		}
	}
}
void TerrainGenerationSys::ImGuiLeftPanel() {
	// Trash variables for function call
		float topoStep = 0.1f, topoBandWidth = 0.05f;
	//
	if (utilities::DisplayModeImGui(modelScale, topoStep, topoBandWidth, heightScale, displayMode, wireFrame, map2d)) {
		if(!biomesGeneration && displayMode == utilities::heightMapMode::BIOMES) {
			displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
		}
		mainShader->Bind();
		mainShader->SetUniform1i("displayMode", static_cast<int>(displayMode));
	}
	utilities::SavingImGui();
}
void TerrainGenerationSys::ImGuiOutput(glm::vec3 pos) {
	if(biomesGeneration && biomeGen.IsGenerated()) {
		int posX = static_cast<int>(pos.x) + width/2;
		int posZ = static_cast<int>(pos.z) + height/2;
		if (posX >= 0 && posX < width && posZ >= 0 && posZ < height) {
			ImGui::Text("Biome at camera position: %s", biomeGen.GetBiome(biomeGen.GetBiomeAt(posX, posZ)).GetName().c_str());
		}
	}
}

void TerrainGenerationSys::NoiseEditor()
{
	static int noisePressedButton = 0;
	ImGui::Separator();
	if (ImGui::Checkbox("Edit component noise", &editNoise)) {
		noisePressedButton = 0;
		mainShader->SetUniform1i("heightMap", static_cast<int>(editNoise));
	}
	if (editNoise) {
		if (ImGui::CollapsingHeader("Terrain noise editor", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Select noise to edit:");
			if (utilities::ImGuiButtonWrapper("Mountainousness", noisePressedButton == 1 ? true : false)) {
				noisePressedButton = 1;
				editedComponent = TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS;
				noiseTxt = std::make_unique<TextureClass>(terrainGen.GetSelectedNoise(editedComponent).GetMap(), terrainGen.GetSelectedNoise(editedComponent).GetWidth(), terrainGen.GetSelectedNoise(editedComponent).GetHeight());
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness", noisePressedButton == 2 ? true : false)) {
				noisePressedButton = 2;
				editedComponent = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
				noiseTxt = std::make_unique<TextureClass>(terrainGen.GetSelectedNoise(editedComponent).GetMap(), terrainGen.GetSelectedNoise(editedComponent).GetWidth(), terrainGen.GetSelectedNoise(editedComponent).GetHeight());
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Weirdness", noisePressedButton == 3 ? true : false)) {
				noisePressedButton = 3;
				editedComponent = TerrainGenerator::WorldGenParameter::WEIRDNESS;
				noiseTxt = std::make_unique<TextureClass>(terrainGen.GetSelectedNoise(editedComponent).GetMap(), terrainGen.GetSelectedNoise(editedComponent).GetWidth(), terrainGen.GetSelectedNoise(editedComponent).GetHeight());
			}
			if (noisePressedButton == 0) {
				ImGui::Text("[Currently no noise is beeing changed]");
				return;
			}

			biomesGeneration = false;
			if (utilities::NoiseImGui(terrainGen.GetSelectedNoiseConfig(editedComponent))) {
				terrainGen.GetSelectedNoise(editedComponent).GenerateFractalNoise();
				changeTerrain = true;
				noiseTxt = std::make_unique<TextureClass>(terrainGen.GetSelectedNoise(editedComponent).GetMap(), terrainGen.GetSelectedNoise(editedComponent).GetWidth(), terrainGen.GetSelectedNoise(editedComponent).GetHeight());
			}
			if (ImGui::Button("Accept changes")) {
				editNoise = false;
				noisePressedButton = 0;
				GenerateTerrain();
				mainShader->SetUniform1i("heightMap", 0);
			}
		}
	}
}

void ShowHorizontalSegments_Draggable() {
	static std::vector<std::string> labels = { "A", "B", "C", "D" };
	static std::vector<float> boundaries = { -1.0f, -0.3f, 0.2f, 0.7f, 1.0f };

	if (ImPlot::BeginPlot("Tmp segment Bar", ImVec2(-1, 80), ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxes("X", "", ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
		ImPlot::SetupAxisLimits(ImAxis_X1, -1, 1, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImGuiCond_Always);

		for (size_t i = 0; i < labels.size(); i++) {
			double xs[4] = { boundaries[i], boundaries[i + 1], boundaries[i + 1], boundaries[i] };
			double ys[4] = { 0, 0, 1, 1 };

			ImPlot::PlotShaded(labels[i].c_str(), xs, ys, 4);
			ImPlot::PlotLine(labels[i].c_str(), xs, ys, 4);

			double mid = 0.5 * (boundaries[i] + boundaries[i + 1]);
			ImPlot::Annotation(mid, 0.5, ImVec4(1, 1, 1, 1), ImVec2(0, 0), true, "%s", labels[i].c_str());
		}

		for (size_t i = 1; i < boundaries.size() - 1; i++) {
			double tmp = boundaries[i];
			if (ImPlot::DragLineX((int)i, &tmp, ImVec4(1, 0, 0, 1), 1.5f)) {
				boundaries[i] = (float)tmp;
			}

			ImPlot::Annotation(boundaries[i], 1.02, ImVec4(0, 0.5f, 1, 1), ImVec2(0, 0), true, "%.2f", boundaries[i]);
		}

		ImPlot::EndPlot();
	}
}

void TerrainGenerationSys::BiomesEditor()
{
	ImGui::Separator();
	if (ImGui::Checkbox("Biomes generation", &biomesGeneration)) {
		if (biomesGeneration) {
			GenerateBiomes();
			displayMode = utilities::heightMapMode::BIOMES;
		}
		else {
			displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
		}
		mainShader->SetUniform1i("displayMode", static_cast<int>(displayMode));
	}
	static int biomeNoisePressedButton = 0;
	if (biomesGeneration) {
		if (ImGui::CollapsingHeader("Biome generation settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Select noise to edit:");
			if (utilities::ImGuiButtonWrapper("Temperature", biomeNoisePressedButton == 1 ? true : false)) {
				biomeNoisePressedButton = 1;
				editedBiomeComponent = BiomeParameter::TEMPERATURE;
				noiseTxt = std::make_unique<TextureClass>(biomeGen.GetNoiseByParameter(editedBiomeComponent).GetMap(), biomeGen.GetNoiseByParameter(editedBiomeComponent).GetWidth(), biomeGen.GetNoiseByParameter(editedBiomeComponent).GetHeight());
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Humidity", biomeNoisePressedButton == 2 ? true : false)) {
				biomeNoisePressedButton = 2;
				editedBiomeComponent = BiomeParameter::HUMIDITY;
				noiseTxt = std::make_unique<TextureClass>(biomeGen.GetNoiseByParameter(editedBiomeComponent).GetMap(), biomeGen.GetNoiseByParameter(editedBiomeComponent).GetWidth(), biomeGen.GetNoiseByParameter(editedBiomeComponent).GetHeight());

			}
			if (biomeNoisePressedButton == 0) {
				ImGui::Text("[Currently no noise is beeing changed]");
				return;
			}

			mainShader->SetUniform1i("heightMap", 1);

			if (utilities::NoiseImGui(biomeGen.GetNoiseByParameter(editedBiomeComponent).GetConfigRef())) {
				biomeGen.GetNoiseByParameter(editedBiomeComponent).GenerateFractalNoise();
				biomeGen.Regenerate();
				noiseTxt = std::make_unique<TextureClass>(biomeGen.GetNoiseByParameter(editedBiomeComponent).GetMap(), biomeGen.GetNoiseByParameter(editedBiomeComponent).GetWidth(), biomeGen.GetNoiseByParameter(editedBiomeComponent).GetHeight());
			}

			if (ImGui::Button("Accept changes")) {
				biomeNoisePressedButton = 0;
				GenerateBiomes();
				mainShader->SetUniform1i("heightMap", 0);
			}
		}
	}
	ShowHorizontalSegments_Draggable();
}

void TerrainGenerationSys::SplineEditor()
{
	static int splinePressedButton = 0;
	ImGui::Separator();
	if (ImGui::Checkbox("Edit spline", &editSpline)) {
		splinePressedButton = 0;
	}
	if (editSpline) {
		if (ImGui::CollapsingHeader("Spline editor", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Select spline to edit:");
			if (utilities::ImGuiButtonWrapper("Mountainousness", splinePressedButton == 1 ? true : false)) {
				splinePressedButton = 1;
				editedComponent = TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS;
				splinePlotPoints = terrainGen.GetSplinePoints(editedComponent);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness", splinePressedButton == 2 ? true : false)) {
				splinePressedButton = 2;
				editedComponent = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
				splinePlotPoints = terrainGen.GetSplinePoints(editedComponent);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Weirdness", splinePressedButton == 3 ? true : false)) {
				splinePressedButton = 3;
				editedComponent = TerrainGenerator::WorldGenParameter::WEIRDNESS;
				splinePlotPoints = terrainGen.GetSplinePoints(editedComponent);
			}

			if (splinePressedButton == 0) {
				ImGui::Text("[Currently no spline is beeing changed]");
				return;
			}

			static int dragging = -1;
			if (splinePlotPoints.size() != 2 || splinePlotPoints[0].size() != splinePlotPoints[1].size()) {
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
				terrainGen.SetSpline(editedComponent, splinePlotPoints);
				changeTerrain = true;
			}
		}
	}
}