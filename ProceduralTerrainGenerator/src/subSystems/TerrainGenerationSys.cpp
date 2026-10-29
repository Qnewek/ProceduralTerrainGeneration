#include "TerrainGenerationSys.h"

#include <iostream>
#include <algorithm>
#include <chrono>

#include "imgui/imgui.h"
#include "ImPlot/implot.h"

TerrainGenerationSys::TerrainGenerationSys() : width(0), height(0), terrainGen(), biomeGen() {
	renderSettings.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
	renderSettings.topoBandWidth = 0.0f;
}
TerrainGenerationSys::~TerrainGenerationSys() {
}
bool TerrainGenerationSys::Initialize(int _width, int _height, float _heightScale) {
	width = _width;
	height = _height;
	renderSettings.heightScale = _heightScale;

	if (!view.Initialize(width, height)) {
		return false;
	}
	terrainGen.Initialize(width, height);
	terrainGen.GenerateNoises(terrainConfig);
	biomeGen.Initialize(width, height);
	GenerateTerrain(0.0f, 0.0f);

	std::cout << "[LOG] TerrainGenerationSys initialized\n";
	return true;

}
bool TerrainGenerationSys::Resize() {
	if (terrainGen.GetHeight() != height || terrainGen.GetWidth() != width) {
		terrainGen.Resize(width, height);
		terrainGen.GenerateNoises(terrainConfig);
		biomeGen.Resize(width, height);
		biomeGen.GenerateComponentNoises();
		view.Resize(width, height);
		return  true;
	}
	return false;
}
bool TerrainGenerationSys::GenerateTerrain(float originx, float originy) {
	Resize();

	auto start = std::chrono::steady_clock::now();
	if (!terrainGen.GenerateTerrain(terrainConfig, originx, originy)) {
		return false;
	}
	lastGenerationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

	view.SetLayer(TerrainView::Layer::TERRAIN, terrainGen.GetHeightField());

	return true;
}
bool TerrainGenerationSys::GenerateBiomes()
{
	if (biomeGen.IsGenerated()) {
		return false;
	}
	if(!biomeGen.Biomify(terrainGen.GetSelectedNoise(WorldGenParameter::CONTINENTALNESS), 
		terrainGen.GetSelectedNoise(WorldGenParameter::MOUNTAINOUSNESS), 
		terrainGen.GetSelectedNoise(WorldGenParameter::WEIRDNESS))) {
		std::cout << "[ERROR] Biomes couldnt be generated\n";
		return false;
	}
	view.SetBiomeColors(utilities::GetBiomeColorMap(biomeGen, width, height), width, height);
	return true;
}
void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	if (infiniteGeneration) {
		if (oldCamPos.x != camera.GetPosition().x || oldCamPos.z != camera.GetPosition().z) {
			GenerateTerrain(camera.GetPosition().x, camera.GetPosition().z);
			oldCamPos = camera.GetPosition();
		}
		camera.CameraAnchor(true);
	}
	TerrainView::Layer heightLayer = showPreview && view.HasLayer(TerrainView::Layer::OVERLAY) ? TerrainView::Layer::OVERLAY : TerrainView::Layer::TERRAIN;
	view.Draw(renderer, camera, light, renderSettings, heightLayer);
}
void TerrainGenerationSys::ImGuiRightPanel() {
	if(utilities::MapSizeImGui(width, height)) {
		GenerateTerrain(0.0f, 0.0f);
	}

	if (ImGui::CollapsingHeader("Terrain settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Evaluating method");
		int evaluatingOption = static_cast<int>(terrainConfig.method);
		const char* methodOptions[] = { "Linear", "Spline" };
		if (ImGui::BeginCombo("Method: ", methodOptions[evaluatingOption]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(methodOptions); n++)
			{
				bool is_selected = (evaluatingOption == n);
				if (ImGui::Selectable(methodOptions[n], is_selected)) {
					evaluatingOption = n;
					terrainConfig.method = static_cast<EvaluationMethod>(evaluatingOption);
					changeTerrain = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SliderInt("Sampling resolution", &samplingResolution, 10, 1000);
		if (ImGui::Button("Change resolution")) {
			terrainConfig.continentalness.resolution = samplingResolution;
			terrainConfig.mountainousness.resolution = samplingResolution;
			terrainConfig.weirdness.resolution = samplingResolution;
			terrainGen.GenerateNoises(terrainConfig);
			GenerateTerrain(0.0f, 0.0f);
		}
		NoiseEditor();
		if (!editNoise) {
			SplineEditor();
			BiomesEditor();
			if (changeTerrain) {
				GenerateTerrain(0.0f, 0.0f);
				biomeGen.Regenerate();
				changeTerrain = false;
			}
		}
	}
}
void TerrainGenerationSys::ImGuiLeftPanel() {
	TerrainRenderSettings& s = renderSettings;
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, infiniteGeneration)) {
		if(!biomesGeneration && s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
		}
	}
	utilities::SavingImGui();
}
void TerrainGenerationSys::ImGuiOutput(glm::vec3 pos) {
	ImGui::Text("Terrain generation: %.1f ms (%dx%d)", lastGenerationMs, terrainGen.GetWidth(), terrainGen.GetHeight());
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
	ImGui::Separator();
	if (ImGui::Checkbox("Edit component noise", &editNoise)) {
		noisePressedButton = 0;
		showPreview = editNoise;
	}
	if (editNoise) {
		if (ImGui::CollapsingHeader("Terrain noise editor", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Select noise to edit:");
			if (utilities::ImGuiButtonWrapper("Mountainousness", noisePressedButton == 1 ? true : false)) {
				noisePressedButton = 1;
				editedComponent = WorldGenParameter::MOUNTAINOUSNESS;
				view.SetLayer(TerrainView::Layer::OVERLAY, terrainGen.GetSelectedNoise(editedComponent).GetField());
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness", noisePressedButton == 2 ? true : false)) {
				noisePressedButton = 2;
				editedComponent = WorldGenParameter::CONTINENTALNESS;
				view.SetLayer(TerrainView::Layer::OVERLAY, terrainGen.GetSelectedNoise(editedComponent).GetField());
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Weirdness", noisePressedButton == 3 ? true : false)) {
				noisePressedButton = 3;
				editedComponent = WorldGenParameter::WEIRDNESS;
				view.SetLayer(TerrainView::Layer::OVERLAY, terrainGen.GetSelectedNoise(editedComponent).GetField());
			}
			if (noisePressedButton == 0) {
				ImGui::Text("[Currently no noise is beeing changed]");
				return;
			}

			biomesGeneration = false;
			if (utilities::NoiseImGui(terrainConfig.Noise(editedComponent))) {
				terrainGen.GetSelectedNoise(editedComponent).GenerateFractalNoise(terrainConfig.Noise(editedComponent), 0.0f, 0.0f);
				changeTerrain = true;
				view.SetLayer(TerrainView::Layer::OVERLAY, terrainGen.GetSelectedNoise(editedComponent).GetField());
			}
			if (ImGui::Button("Accept changes")) {
				editNoise = false;
				noisePressedButton = 0;
				GenerateTerrain(0.0f, 0.0f);
				showPreview = false;
			}
		}
	}
}

void TerrainGenerationSys::BiomesEditor()
{
	ImGui::Separator();
	if (ImGui::Checkbox("Biomes generation", &biomesGeneration)) {
		if (biomesGeneration) {
			GenerateBiomes();
			renderSettings.displayMode = utilities::heightMapMode::BIOMES;
		}
		else {
			renderSettings.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
		}
	}
	if (biomesGeneration) {
		if (ImGui::CollapsingHeader("Biome generation settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			BiomeNoisesEditor();
			NoisesLevelsForBiomes();
		}
	}
}

void TerrainGenerationSys::BiomeNoisesEditor()
{
	ImGui::Text("Select noise to edit:");
	if (utilities::ImGuiButtonWrapper("Temperature", biomeNoisePressedButton == 1 ? true : false)) {
		biomeNoisePressedButton = 1;
		editedBiomeComponent = BiomeParameter::TEMPERATURE;
		view.SetLayer(TerrainView::Layer::OVERLAY, biomeGen.GetNoiseByParameter(editedBiomeComponent).GetField());
	}
	ImGui::SameLine();
	if (utilities::ImGuiButtonWrapper("Humidity", biomeNoisePressedButton == 2 ? true : false)) {
		biomeNoisePressedButton = 2;
		editedBiomeComponent = BiomeParameter::HUMIDITY;
		view.SetLayer(TerrainView::Layer::OVERLAY, biomeGen.GetNoiseByParameter(editedBiomeComponent).GetField());

	}
	if (biomeNoisePressedButton == 0) {
		ImGui::Text("[Currently no noise is beeing changed]");
		return;
	}

	showPreview = true;

	if (utilities::NoiseImGui(biomeGen.GetNoiseConfigByParameter(editedBiomeComponent))) {
		biomeGen.GetNoiseByParameter(editedBiomeComponent).GenerateFractalNoise(biomeGen.GetNoiseConfigByParameter(editedBiomeComponent), 0.0f, 0.0f);
		biomeGen.Regenerate();
		view.SetLayer(TerrainView::Layer::OVERLAY, biomeGen.GetNoiseByParameter(editedBiomeComponent).GetField());
	}

	if (ImGui::Button("Accept changes")) {
		biomeNoisePressedButton = 0;
		GenerateBiomes();
		showPreview = false;
	}
}

void TerrainGenerationSys::SplineEditor()
{
	ImGui::Separator();
	if (ImGui::Checkbox("Edit spline", &editSpline)) {
		splinePressedButton = 0;
		splineDraggedPoint = -1;
	}
	if (editSpline) {
		if (ImGui::CollapsingHeader("Spline editor", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Select spline to edit:");
			if (utilities::ImGuiButtonWrapper("Mountainousness", splinePressedButton == 1 ? true : false)) {
				splinePressedButton = 1;
				editedComponent = WorldGenParameter::MOUNTAINOUSNESS;
				splinePlotPoints = terrainConfig.Spline(editedComponent);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness", splinePressedButton == 2 ? true : false)) {
				splinePressedButton = 2;
				editedComponent = WorldGenParameter::CONTINENTALNESS;
				splinePlotPoints = terrainConfig.Spline(editedComponent);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Weirdness", splinePressedButton == 3 ? true : false)) {
				splinePressedButton = 3;
				editedComponent = WorldGenParameter::WEIRDNESS;
				splinePlotPoints = terrainConfig.Spline(editedComponent);
			}

			if (splinePressedButton == 0) {
				ImGui::Text("[Currently no spline is beeing changed]");
				return;
			}

			if (splinePlotPoints.x.size() < 3 || splinePlotPoints.x.size() != splinePlotPoints.y.size()) {
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

				std::vector<double> xs, ys; xs.reserve(splinePlotPoints.x.size()); ys.reserve(splinePlotPoints.y.size());
				for (auto& p : splinePlotPoints.x) { xs.push_back(p); }
				for (auto& p : splinePlotPoints.y) { ys.push_back(p); }
				ImPlot::PlotLine("Spline (lin.)", xs.data(), ys.data(), (int)splinePlotPoints.x.size());
				ImPlot::PlotScatter("Spline points", xs.data(), ys.data(), (int)splinePlotPoints.x.size());

				ImPlotPoint mouse = ImPlot::GetPlotMousePos();
				if (splineDraggedPoint == -1 && ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
					for (int i = 0; i < (int)splinePlotPoints.x.size(); ++i) {
						double dx = (double)mouse.x - splinePlotPoints.x[i];
						double dy = (double)mouse.y - splinePlotPoints.y[i];
						if (dx * dx + dy * dy <= pick_radius * pick_radius) {
							splineDraggedPoint = i;
							break;
						}
					}
				}

				if (splineDraggedPoint != -1) {
					if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
						if (splineDraggedPoint == 0 || splineDraggedPoint == (int)splinePlotPoints.x.size() - 1) {
							splinePlotPoints.y[splineDraggedPoint] = std::clamp((double)mouse.y, ymin, ymax);
						}
						else {
							double xmin_drag = splinePlotPoints.x[splineDraggedPoint - 1] + 0.01;
							double xmax_drag = splinePlotPoints.x[splineDraggedPoint + 1] - 0.01;

							splinePlotPoints.x[splineDraggedPoint] = std::clamp((double)mouse.x, xmin_drag, xmax_drag);
							splinePlotPoints.y[splineDraggedPoint] = std::clamp((double)mouse.y, ymin, ymax);
						}
					}
					else {
						splineDraggedPoint = -1;
					}
				}

				if (splineDraggedPoint == -1 && ImPlot::IsPlotHovered()) {
					for (int i = 0; i < (int)splinePlotPoints.x.size(); i++) {
						double dx = (double)mouse.x - splinePlotPoints.x[i], dy = (double)mouse.y - splinePlotPoints.y[i];
						if (dx * dx + dy * dy <= pick_radius * pick_radius) {
							ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
							break;
						}
					}
				}
				ImPlot::EndPlot();
			}
			if (ImGui::Button("Set new spline points")) {
				//Edited splines are smoothed (cubic), the defaults are linear - behaviour kept from before A3
				splinePlotPoints.cubic = true;
				terrainConfig.Spline(editedComponent) = splinePlotPoints;
				changeTerrain = true;
			}
		}
	}
}
void TerrainGenerationSys::NoisesLevelsForBiomes() {
	if (ImGui::CollapsingHeader("Biome levels editor", ImGuiTreeNodeFlags_DefaultOpen)) {
		SegmentDrag(biomeGen.GetLevelsByParameter(BiomeParameter::TEMPERATURE), "Temperature levels");
		SegmentDrag(biomeGen.GetLevelsByParameter(BiomeParameter::HUMIDITY), "Humidity levels");
		SegmentDrag(biomeGen.GetLevelsByParameter(BiomeParameter::CONTINENTALNESS), "Continentalness levels");
		SegmentDrag(biomeGen.GetLevelsByParameter(BiomeParameter::MOUNTAINOUSNESS), "Mountainousness levels");
		SegmentDrag(biomeGen.GetLevelsByParameter(BiomeParameter::WEIRDNESS), "Weirdness levels");
	}
}

void TerrainGenerationSys::SegmentDrag(std::vector<float>& boundaries, std::string s)
{
	if (ImPlot::BeginPlot(s.c_str(), ImVec2(-1, 80), ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxes("Value", "", ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
		ImPlot::SetupAxisLimits(ImAxis_X1, -1, 1, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImGuiCond_Always);

		for (size_t i = 0; i < boundaries.size()-1; i++) {
			double xs[4] = { boundaries[i], boundaries[i + 1], boundaries[i + 1], boundaries[i] };
			double ys[4] = { 0, 0, 1, 1 };

			ImPlot::PlotShaded(std::to_string(i).c_str(), xs, ys, 4);
			ImPlot::PlotLine(std::to_string(i).c_str(), xs, ys, 4);

			double mid = 0.5 * (boundaries[i] + boundaries[i + 1]);
			ImPlot::Annotation(mid, 0.5, ImVec4(1, 1, 1, 1), ImVec2(0, 0), true, "%d", static_cast<int>(i));
		}

		for (size_t i = 1; i < boundaries.size() - 1; i++) {
			double tmp = boundaries[i];
			if (ImPlot::DragLineX((int)i, &tmp, ImVec4(1, 0, 0, 1), 1.5f)) {
				boundaries[i] = std::clamp((float)tmp, -1.0f, 1.0f);			
			}
			if (boundaries[i] > boundaries[i + 1]) {
				boundaries[i + 1] = boundaries[i];
			}
			//Boundary 0 is the fixed lower bound (-1), dragged values are clamped to it
			int j = static_cast<int>(i) - 1;
			while(j > 0 && boundaries[j] > boundaries[i]) {
				boundaries[j] = boundaries[i];
				j--;
			}

			ImPlot::Annotation(boundaries[i], 1.02, ImVec4(0, 0.5f, 1, 1), ImVec2(0, 0), true, "%.2f", boundaries[i]);
		}

		ImPlot::EndPlot();
	}
}
