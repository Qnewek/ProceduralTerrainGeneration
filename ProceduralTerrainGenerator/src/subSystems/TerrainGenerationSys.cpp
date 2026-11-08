#include "TerrainGenerationSys.h"

#include <iostream>
#include <algorithm>

#include "imgui.h"
#include "implot.h"

#include "Preset.h"

TerrainGenerationSys::TerrainGenerationSys() {
	renderSettings.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
	renderSettings.topoBandWidth = 0.0f;
}
TerrainGenerationSys::~TerrainGenerationSys() {
}
bool TerrainGenerationSys::Initialize(int _width, int _height, float _heightScale) {
	world.width = _width;
	world.height = _height;
	renderSettings.heightScale = _heightScale;

	if (!view.Initialize(world.width, world.height) || !chunks.Initialize() || !gpu.Initialize() || !noisePreview.Initialize()) {
		return false;
	}

	//The first frame needs a map
	RequestTerrain();
	GenerateFinite();

	std::cout << "[LOG] TerrainGenerationSys initialized\n";
	return true;

}

void TerrainGenerationSys::Update(Camera& camera)
{
	gpu.PollMeasure(lastGenerationMs);
	const bool leftInfinite = chunksShown && !infiniteGeneration;
	chunksShown = infiniteGeneration;
	if (leftInfinite) {
		//Changes made meanwhile went only to the chunks
		RequestTerrain();
		if (biomesGeneration) {
			RequestBiomes();
		}
	}
	GeneratePreview();
	if (infiniteGeneration) {
		UpdateChunks(camera);
	}
	else {
		GenerateFinite();
	}
	//After generating: the map size may have changed meanwhile
	if (leftInfinite) {
		view.BringCameraToMap(camera, renderSettings);
	}
}

void TerrainGenerationSys::UpdateChunks(const Camera& camera)
{
	if (pending.Any()) {
		chunks.Invalidate();
		pending = WorldRequest();
	}
	chunks.SetBiomes(biomesGeneration);
	//An invalid size waiting in the UI keeps the previous one
	if (world.width >= 2 && world.height >= 2 && world.width <= 8192 && world.height <= 8192) {
		chunks.AlignWithMap(world.width, world.height);
	}

	if (chunks.Plan(ChunkWorld::ViewFrom(camera, renderSettings)) == 0) {
		return;
	}
	gpu.BeginMeasure();
	chunks.GenerateNext([&](const MapArea& area, TextureClass& height, TextureClass* colors, TextureClass* ids) {
		gpu.GenerateTerrain(world.terrain, area, height);
		if (colors && ids) {
			gpu.GenerateBiomes(world.biomes, world.terrain, biomeTable, area, *colors, *ids);
		}
	});
	gpu.EndMeasure();
}

void TerrainGenerationSys::GenerateFinite()
{
	if (!pending.Any()) {
		return;
	}
	const WorldRequest request = pending;
	pending = WorldRequest();

	const int width = world.width, height = world.height;
	if (width < 2 || height < 2 || width > 8192 || height > 8192) {
		std::cout << "[ERROR] Map size must be between 2 and 8192, keeping the previous maps\n";
		return;
	}
	view.Resize(width, height);

	gpu.BeginMeasure();
	if (request.terrain) {
		gpu.GenerateTerrain(world.terrain, MapArea{}, view.LayerTexture(TerrainView::Layer::TERRAIN, width, height));
	}
	if (request.biomes) {
		if (!biomeIds || biomeIds->GetWidth() != width || biomeIds->GetHeight() != height) {
			biomeIds = std::make_unique<TextureClass>(width, height, GL_R32I);
		}
		gpu.GenerateBiomes(world.biomes, world.terrain, biomeTable, MapArea{}, view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds);
	}
	gpu.EndMeasure();

	if (request.biomes) {
		biomeMap.Resize(width, height);
		biomeIds->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(biomeMap.Size() * sizeof(int)), biomeMap.Data());
	}
}

const noise::NoiseConfigParameters* TerrainGenerationSys::EditedNoise() const
{
	if (editNoise && noisePressedButton != 0) {
		return &world.terrain.Noise(editedComponent);
	}
	if (biomesGeneration && biomeNoisePressedButton != 0) {
		return &world.biomes.Noise(editedBiomeComponent);
	}
	return nullptr;
}

void TerrainGenerationSys::GeneratePreview()
{
	if (!pending.preview) {
		return;
	}
	pending.preview = false;
	const noise::NoiseConfigParameters* edited = EditedNoise();
	//An invalid size waiting in the UI keeps the previous picture
	if (edited && showNoisePreview && world.width >= 2 && world.height >= 2 && world.width <= 8192 && world.height <= 8192) {
		noisePreview.Generate(gpu, *edited, world.width, world.height);
	}
}

void TerrainGenerationSys::RequestPreview()
{
	pending.preview = true;
}

void TerrainGenerationSys::RequestTerrain()
{
	pending.terrain = true;
}

void TerrainGenerationSys::RequestBiomes()
{
	pending.biomes = true;
}

void TerrainGenerationSys::PresetsImGui()
{
	std::string error;
	switch (presets.ImGuiDraw()) {
	case PresetPanel::Action::SAVE:
		if (preset::Save(presets.SavePath(), world, error)) {
			presets.ReportSaved();
		}
		else {
			presets.ReportError(error);
		}
		break;
	case PresetPanel::Action::LOAD: {
		WorldConfig loaded;
		if (preset::Load(presets.LoadPath(), loaded, error)) {
			ApplyWorld(loaded);
			presets.ReportLoaded();
		}
		else {
			presets.ReportError(error);
		}
		break;
	}
	case PresetPanel::Action::NONE:
		break;
	}
}

void TerrainGenerationSys::ApplyWorld(const WorldConfig& loaded)
{
	world = loaded;

	samplingResolution = world.terrain.continentalness.resolution;
	splinePressedButton = 0;
	splineDraggedPoint = -1;
	changeTerrain = false;

	RequestPreview();
	RequestTerrain();
	if (biomesGeneration) {
		RequestBiomes();
	}
}

void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	if (chunksShown) {
		chunks.Draw(renderer, camera, light, renderSettings);
		return;
	}
	view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::TERRAIN);
}
void TerrainGenerationSys::ImGuiRightPanel() {
	if(utilities::MapSizeImGui(world.width, world.height)) {
		//Every map shown has to get the new size; biomes only if they are shown
		RequestTerrain();
		RequestPreview();
		if (biomesGeneration) {
			RequestBiomes();
		}
	}

	if (ImGui::CollapsingHeader("Terrain settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Evaluating method");
		int evaluatingOption = static_cast<int>(world.terrain.method);
		const char* methodOptions[] = { "Linear", "Spline", "Layered" };
		if (ImGui::BeginCombo("Method: ", methodOptions[evaluatingOption]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(methodOptions); n++)
			{
				bool is_selected = (evaluatingOption == n);
				if (ImGui::Selectable(methodOptions[n], is_selected)) {
					evaluatingOption = n;
					world.terrain.method = static_cast<EvaluationMethod>(evaluatingOption);
					changeTerrain = true;
					//The method decides which spline weirdness uses, so an open editor takes the new one
					if (splinePressedButton != 0) {
						splinePlotPoints = world.terrain.MethodSpline(editedSplineComponent);
					}
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (world.terrain.method == EvaluationMethod::LAYERED) {
			changeTerrain |= ImGui::SliderFloat("Sea level", &world.terrain.seaLevel, 0.0f, 1.0f);
			ImGui::SetItemTooltip("Value of the continentalness spline where land begins; mountains grow only above it");
			changeTerrain |= ImGui::SliderFloat("Coast width", &world.terrain.coastWidth, 0.01f, 1.0f);
			ImGui::SetItemTooltip("How far inland (in continentalness spline values) mountains reach full height");
			changeTerrain |= ImGui::SliderFloat("Mountain share", &world.terrain.mountainShare, 0.0f, 1.0f);
			ImGui::SetItemTooltip("Part of the height range for mountains; the continentalness base takes the rest");
		}
		//Applied at once to all three terrain noises, like the other parameters
		if (ImGui::SliderInt("Sampling resolution", &samplingResolution, 10, 1000)) {
			world.terrain.continentalness.resolution = samplingResolution;
			world.terrain.mountainousness.resolution = samplingResolution;
			world.terrain.weirdness.resolution = samplingResolution;
			RequestPreview();
			RequestTerrain();
			if (biomesGeneration) {
				RequestBiomes();
			}
		}
		NoiseEditor();
		SplineEditor();
		BiomesEditor();
		if (changeTerrain) {
			RequestTerrain();
			changeTerrain = false;
		}
	}
}
void TerrainGenerationSys::ImGuiLeftPanel() {
	TerrainRenderSettings& s = renderSettings;
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, &infiniteGeneration)) {
		if(!biomesGeneration && s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
		}
	}
	if (infiniteGeneration) {
		chunks.ImGuiDraw();
	}
	PresetsImGui();
	if (infiniteGeneration) {
		ImGui::TextDisabled("Saving works on the finite map (Partial generation)");
	}
	else {
		exporter.ImGuiDraw({ { view.GetMapWidth(), view.GetMapHeight(), "", [this] { return view.ReadLayer(TerrainView::Layer::TERRAIN); } } }, heightmap::terrainRange);
	}
}
void TerrainGenerationSys::ImGuiOutput(glm::vec3 pos) {
	//Camera in world units before modelScale, like the maps
	const glm::vec2 position = glm::vec2(pos.x, pos.z) / renderSettings.modelScale;
	int id = -1;
	if (chunksShown) {
		ImGui::Text("Last generation (GPU): %.2f ms (up to %d chunks of %dx%d)", lastGenerationMs, ChunkWorld::maxChunksPerFrame, ChunkWorld::chunkSize, ChunkWorld::chunkSize);
		ImGui::Text("Chunks: %d drawn of %d in range, %d cached, %d waiting", chunks.GetVisibleCount(), chunks.GetInRangeCount(), chunks.GetCachedCount(), chunks.GetPendingCount());
		ImGui::Text("Coarsest level in view: %d (1 sample per %d texels)", chunks.GetCoarsestLevel(), 1 << chunks.GetCoarsestLevel());
		chunks.ImGuiUpdateTimes();
		id = chunks.BiomeAt(position);
	}
	else {
		ImGui::Text("Last generation (GPU): %.2f ms (%dx%d)", lastGenerationMs, view.GetMapWidth(), view.GetMapHeight());
		if (biomesGeneration && !biomeMap.Empty()) {
			//Biome map size, not world.width/height - those may already hold an unapplied resize. TerrainView centers the map
			const int width = biomeMap.GetWidth(), height = biomeMap.GetHeight();
			const glm::ivec2 texel(glm::floor(position / TerrainView::worldUnitsPerTexel + glm::vec2(width, height) / 2.0f));
			if (texel.x >= 0 && texel.x < width && texel.y >= 0 && texel.y < height) {
				id = biomeMap.At(texel.x, texel.y);
			}
		}
	}
	if (id >= 0) {
		auto found = std::find_if(biomeTable.begin(), biomeTable.end(), [id](const biome::Biome& b) { return b.GetId() == id; });
		if (found != biomeTable.end()) {
			ImGui::Text("Biome at camera position: %s", found->GetName().c_str());
		}
	}
}

void TerrainGenerationSys::NoiseEditor()
{
	ImGui::Separator();
	if (ImGui::Checkbox("Edit component noise", &editNoise)) {
		noisePressedButton = 0;
		//The biome noise editor may own the picture now
		RequestPreview();
	}
	if (editNoise) {
		if (ImGui::CollapsingHeader("Terrain noise editor", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Select noise to edit:");
			if (utilities::ImGuiButtonWrapper("Mountainousness", noisePressedButton == 1 ? true : false)) {
				noisePressedButton = 1;
				editedComponent = WorldGenParameter::MOUNTAINOUSNESS;
				RequestPreview();
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness", noisePressedButton == 2 ? true : false)) {
				noisePressedButton = 2;
				editedComponent = WorldGenParameter::CONTINENTALNESS;
				RequestPreview();
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Weirdness", noisePressedButton == 3 ? true : false)) {
				noisePressedButton = 3;
				editedComponent = WorldGenParameter::WEIRDNESS;
				RequestPreview();
			}
			if (noisePressedButton == 0) {
				ImGui::Text("[Currently no noise is beeing changed]");
				return;
			}

			ImGui::TextDisabled("The view shows the resulting terrain, changes apply at once");
			NoisePreviewImGui(world.terrain.Noise(editedComponent));
			//The biome noise editor can be open at the same time, with widgets of the same names
			ImGui::PushID("terrainNoise");
			const bool noiseChanged = utilities::NoiseImGui(world.terrain.Noise(editedComponent));
			ImGui::PopID();
			if (noiseChanged) {
				RequestPreview();
				changeTerrain = true;
				//Biomes are chosen by the terrain noises too
				if (biomesGeneration) {
					RequestBiomes();
				}
			}
			if (ImGui::Button("Done")) {
				editNoise = false;
				noisePressedButton = 0;
				RequestPreview();
			}
		}
	}
}

void TerrainGenerationSys::NoisePreviewImGui(const noise::NoiseConfigParameters& noise)
{
	//Both noise editors can be open; the picture belongs to the one EditedNoise picks
	if (EditedNoise() != &noise) {
		return;
	}
	if (ImGui::Checkbox("Show noise as 2D map", &showNoisePreview) && showNoisePreview) {
		RequestPreview();
	}
	if (showNoisePreview) {
		noisePreview.ImGuiDraw();
	}
}

void TerrainGenerationSys::BiomesEditor()
{
	ImGui::Separator();
	if (ImGui::Checkbox("Biomes generation", &biomesGeneration)) {
		if (biomesGeneration) {
			RequestBiomes();
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
		RequestPreview();
	}
	ImGui::SameLine();
	if (utilities::ImGuiButtonWrapper("Humidity", biomeNoisePressedButton == 2 ? true : false)) {
		biomeNoisePressedButton = 2;
		editedBiomeComponent = BiomeParameter::HUMIDITY;
		RequestPreview();
	}
	if (biomeNoisePressedButton == 0) {
		ImGui::Text("[Currently no noise is beeing changed]");
		return;
	}

	NoisePreviewImGui(world.biomes.Noise(editedBiomeComponent));
	ImGui::PushID("biomeNoise");
	const bool noiseChanged = utilities::NoiseImGui(world.biomes.Noise(editedBiomeComponent));
	ImGui::PopID();
	if (noiseChanged) {
		RequestPreview();
		RequestBiomes();
	}

	if (ImGui::Button("Done##biomeNoise")) {
		biomeNoisePressedButton = 0;
		RequestPreview();
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
			const bool layered = world.terrain.method == EvaluationMethod::LAYERED;
			if (world.terrain.method == EvaluationMethod::LINEAR_COMBINE) {
				ImGui::TextDisabled("The Linear method uses no splines");
			}
			ImGui::Text("Select spline to edit:");
			if (utilities::ImGuiButtonWrapper("Mountainousness##spline", splinePressedButton == 1 ? true : false)) {
				splinePressedButton = 1;
				editedSplineComponent = WorldGenParameter::MOUNTAINOUSNESS;
				splinePlotPoints = world.terrain.MethodSpline(editedSplineComponent);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper("Continentalness##spline", splinePressedButton == 2 ? true : false)) {
				splinePressedButton = 2;
				editedSplineComponent = WorldGenParameter::CONTINENTALNESS;
				splinePlotPoints = world.terrain.MethodSpline(editedSplineComponent);
			}
			ImGui::SameLine();
			if (utilities::ImGuiButtonWrapper(layered ? "Peaks & valleys" : "Weirdness##spline", splinePressedButton == 3 ? true : false)) {
				splinePressedButton = 3;
				editedSplineComponent = WorldGenParameter::WEIRDNESS;
				splinePlotPoints = world.terrain.MethodSpline(editedSplineComponent);
			}
			if (layered) {
				ImGui::SetItemTooltip("Shape of mountains from weirdness: low = valleys, high = peaks.\n"
					"Ridge on weirdness (gain 3, offset 1) folds it into peaks and valleys as in Minecraft 1.18");
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

			//What the axes mean depends on the method: LAYERED gives each spline its own role
			const char* xLabel = "Continentalness";
			const char* yLabel = layered ? "Base height" : "Height factor";
			if (editedSplineComponent == WorldGenParameter::MOUNTAINOUSNESS) {
				xLabel = "Mountainousness";
				yLabel = layered ? "Mountain amplitude" : "Height factor";
			}
			else if (editedSplineComponent == WorldGenParameter::WEIRDNESS) {
				xLabel = layered ? "Weirdness (valleys -> peaks)" : "Weirdness";
				yLabel = layered ? "Mountain shape" : "Height factor";
			}
			if (ImPlot::BeginPlot("##plot", ImVec2(-1, 320), ImPlotFlags_NoMenus)) {
				ImPlot::SetupAxis(ImAxis_X1, xLabel, ImPlotAxisFlags_Lock);
				ImPlot::SetupAxis(ImAxis_Y1, yLabel, ImPlotAxisFlags_Lock);
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
				world.terrain.MethodSpline(editedSplineComponent) = splinePlotPoints;
				changeTerrain = true;
			}
		}
	}
}
void TerrainGenerationSys::NoisesLevelsForBiomes() {
	if (ImGui::CollapsingHeader("Biome levels editor", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool levelsChanged = SegmentDrag(world.biomes.temperatureLevels, "Temperature levels");
		levelsChanged |= SegmentDrag(world.biomes.humidityLevels, "Humidity levels");
		levelsChanged |= SegmentDrag(world.biomes.continentalnessLevels, "Continentalness levels");
		levelsChanged |= SegmentDrag(world.biomes.mountainousnessLevels, "Mountainousness levels");
		levelsChanged |= SegmentDrag(world.biomes.weirdnessLevels, "Weirdness levels");
		if (levelsChanged) {
			RequestBiomes();
		}
	}
}

//Plot with draggable level boundaries
//@return true if a boundary was moved this frame
bool TerrainGenerationSys::SegmentDrag(std::vector<float>& boundaries, std::string s)
{
	bool changed = false;
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
				changed = true;
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
	return changed;
}
