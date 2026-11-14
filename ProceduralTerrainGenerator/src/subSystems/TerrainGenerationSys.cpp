#include "TerrainGenerationSys.h"

#include <iostream>
#include <algorithm>

#include "imgui.h"
#include "implot.h"

#include "Preset.h"
#include "SplineSampling.h"
#include "VegetationModels.h"

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
	MeasureLand();
	if (infiniteGeneration) {
		UpdateChunks(camera);
		VegetationLayer::World vegetationWorld;
		vegetationWorld.biomes = biomesGeneration ? &world.biomes.table : nullptr;
		vegetationWorld.waterHeight = world.terrain.WaterHeight();
		vegetationWorld.heightScale = renderSettings.heightScale;
		vegetationWorld.seed = static_cast<uint32_t>(world.biomes.detailSeed);
		vegetationLayer.Update(chunks, vegetationModels, vegetationWorld);
	}
	else {
		GenerateFinite();
	}
	//After generating: the map size may have changed meanwhile
	if (leftInfinite) {
		view.BringCameraToMap(camera, renderSettings);
	}
}

void TerrainGenerationSys::MeasureLand()
{
	//Any terrain change makes the shown statistics old (checked before the generation clears the request)
	if (pending.terrain && landStats) {
		landStatsOld = true;
	}
	if (!measureLand) {
		return;
	}
	measureLand = false;
	//Around the middle of the finite map, with the island and biome modifiers as the terrain shown
	const HeightField sample = gpu.SampleTerrain(world.terrain, glm::vec2(world.width, world.height) * 0.5f, land::sampleSize, land::sampleStep,
		glm::ivec2(world.width, world.height), biomesGeneration ? &world.biomes : nullptr);
	landStats = land::Measure(sample, world.terrain.WaterHeight());
	landStatsOld = false;
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
	chunks.GenerateNext([&](const MapArea& area, TextureClass& height, TextureClass* colors, TextureClass* ids, TextureClass* splat) {
		//The maps of a chunk are generated together, so with biomes one pass gives all of them
		if (colors && ids) {
			gpu.GenerateWorld(world.terrain, world.biomes, area, height, *colors, *ids, splat);
		}
		else {
			gpu.GenerateTerrain(world.terrain, area, height, biomesGeneration ? &world.biomes : nullptr);
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

	if (request.biomes && (!biomeIds || biomeIds->GetWidth() != width || biomeIds->GetHeight() != height)) {
		biomeIds = std::make_unique<TextureClass>(width, height, GL_R32I);
	}
	gpu.BeginMeasure();
	if (request.terrain && request.biomes && biomesGeneration) {
		//Both in one pass: the noises and the biome table are evaluated once
		gpu.GenerateWorld(world.terrain, world.biomes, MapArea{}, view.LayerTexture(TerrainView::Layer::TERRAIN, width, height),
			view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds, &view.SplatTexture(width, height));
	}
	else {
		if (request.terrain) {
			gpu.GenerateTerrain(world.terrain, MapArea{}, view.LayerTexture(TerrainView::Layer::TERRAIN, width, height), biomesGeneration ? &world.biomes : nullptr);
		}
		if (request.biomes) {
			gpu.GenerateBiomes(world.biomes, world.terrain, MapArea{}, view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds,
				&view.SplatTexture(width, height));
		}
	}
	gpu.EndMeasure();

	if (request.biomes) {
		biomeMap.Resize(width, height);
		biomeIds->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(biomeMap.Size() * sizeof(int)), biomeMap.Data());
	}
}

const noise::NoiseConfigParameters* TerrainGenerationSys::EditedNoise() const
{
	//The noise of the open tab; selected as of the last frame, since generation runs before the UI
	if (generationTabs.Selected() == tabNoises) {
		return &world.terrain.Noise(editedComponent);
	}
	if (biomesGeneration && generationTabs.Selected() == tabBiomeNoises) {
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
	//The climate of biomes depends on the terrain height
	if (biomesGeneration) {
		pending.biomes = true;
	}
}

void TerrainGenerationSys::SetVegetationModels(VegetationModels* models)
{
	vegetationModels = models;
	std::vector<BiomeEditor::PlantModel> names;
	if (models) {
		for (const VegetationModels::Model& m : models->GetModels()) {
			names.push_back({ m.name, vegetation::LayerOfKind(m.kind) });
		}
	}
	biomeEditor.SetPlantModels(std::move(names));
}

void TerrainGenerationSys::RequestBiomes()
{
	pending.biomes = true;
	//The biome modifiers change the terrain, so any biome change may change it
	if (world.biomes.modifyTerrain) {
		pending.terrain = true;
	}
}

void TerrainGenerationSys::ImGuiPresets()
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
	splineDraggedPoint = -1;
	changeTerrain = false;
	biomeEditor.Reset();

	RequestPreview();
	RequestTerrain();
	if (biomesGeneration) {
		RequestBiomes();
	}
}

void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	if (chunksShown) {
		//Plants first: the terrain behind them then fails the depth test before its (expensive) shader runs; in a forest this
		//halves the cost of the terrain
		vegetationLayer.Draw(camera, light, renderSettings, vegetationModels);
		if (terrainMaterials) {
			grass.Draw(chunks, camera, light, renderSettings, *terrainMaterials);
		}
		chunks.Draw(renderer, camera, light, renderSettings);
		//Blended over the terrain, so after it
		water.Draw(chunks, camera, light, renderSettings, world.terrain.WaterHeight() * renderSettings.heightScale);
		return;
	}
	view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::TERRAIN);
}
void TerrainGenerationSys::ImGuiRightPanel()
{
	const PanelTabs::Tab tabs[] = {
		{ tabTerrain, [this] { TerrainImGui(); } },
		{ tabNoises, [this] { NoiseEditor(); } },
		{ tabSplines, [this] { SplineEditor(); } },
		{ tabBiomes, [this] { BiomesEditor(); } },
		{ tabBiomeTable, [this] { BiomeTableEditor(); } },
		{ tabBiomeNoises, [this] { BiomeNoisesEditor(); } },
	};
	generationTabs.Draw("generation", tabs);
	//The 2D picture shows the noise of the open tab
	if (generationTabs.Changed()) {
		RequestPreview();
	}
	if (changeTerrain) {
		RequestTerrain();
		changeTerrain = false;
	}
}

void TerrainGenerationSys::TerrainImGui()
{
	if (utilities::MapSizeImGui(world.width, world.height)) {
		//Every map shown has to get the new size; biomes only if they are shown
		RequestTerrain();
		RequestPreview();
		if (biomesGeneration) {
			RequestBiomes();
		}
	}

	ImGui::SeparatorText("Terrain method");
	int evaluatingOption = static_cast<int>(world.terrain.method);
	const char* methodOptions[] = { "Linear", "Spline", "Layered" };
	if (ImGui::BeginCombo("Method", methodOptions[evaluatingOption]))
	{
		for (int n = 0; n < IM_ARRAYSIZE(methodOptions); n++)
		{
			bool is_selected = (evaluatingOption == n);
			if (ImGui::Selectable(methodOptions[n], is_selected)) {
				evaluatingOption = n;
				world.terrain.method = static_cast<EvaluationMethod>(evaluatingOption);
				changeTerrain = true;
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
	LandStatsImGui();
}

void TerrainGenerationSys::LandStatsImGui()
{
	ImGui::SeparatorText("Land statistics");
	if (ImGui::Button("Measure")) {
		measureLand = true;
	}
	const int side = static_cast<int>(static_cast<float>(land::sampleSize) * land::sampleStep);
	ImGui::SetItemTooltip("Samples %dx%d texels around the map (every %.0f texels): far more than the finite map, so the whole world counts",
		side, side, land::sampleStep);
	if (!landStats) {
		ImGui::SameLine();
		ImGui::TextDisabled("not measured yet");
		return;
	}
	if (landStatsOld) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "terrain changed since");
	}
	const LandStats& s = *landStats;
	ImGui::Text("Water: %.1f%%   Land: %.1f%%", 100.0f * s.waterShare, 100.0f * (1.0f - s.waterShare));
	ImGui::Text("Landmasses: %d", s.landmasses);
	ImGui::Text("Largest landmass: %.1f%% of the land", 100.0f * s.largestShare);
	ImGui::Text("In continents: %.1f%% of the land", 100.0f * s.continentalShare);
	ImGui::SetItemTooltip("Land in landmasses of at least %.0f%% of the sampled area; the rest are islands", 100.0f * land::continentMinShare);
}

void TerrainGenerationSys::ImGuiDisplay()
{
	TerrainRenderSettings& s = renderSettings;
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, &infiniteGeneration)) {
		if(!biomesGeneration && s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
		}
	}
	utilities::TerrainDetailImGui(s.tessPixels);
	if (infiniteGeneration) {
		ImGui::SeparatorText("Infinite generation");
		chunks.ImGuiDraw();
		water.ImGuiDraw();
	}
}

void TerrainGenerationSys::ImGuiSaving()
{
	if (infiniteGeneration) {
		ImGui::TextWrapped("Saving works on the finite map: switch to Partial generation in the Display tab.");
		return;
	}
	exporter.ImGuiDraw({ { view.GetMapWidth(), view.GetMapHeight(), "", [this] { return view.ReadLayer(TerrainView::Layer::TERRAIN); } } }, heightmap::terrainRange);
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
	//Ids are indices in the table; one generated before the table shrank may be out of range for a frame
	if (id >= 0 && id < static_cast<int>(world.biomes.table.size())) {
		ImGui::Text("Biome at camera position: %s", world.biomes.table[id].name.c_str());
	}
}

void TerrainGenerationSys::NoiseEditor()
{
	const std::pair<WorldGenParameter, const char*> components[] = {
		{ WorldGenParameter::CONTINENTALNESS, "Continentalness" },
		{ WorldGenParameter::MOUNTAINOUSNESS, "Mountainousness" },
		{ WorldGenParameter::WEIRDNESS, "Weirdness" },
	};
	for (const auto& [component, name] : components) {
		if (component != components[0].first) {
			ImGui::SameLine();
		}
		if (utilities::ImGuiButtonWrapper(name, editedComponent == component)) {
			editedComponent = component;
			RequestPreview();
		}
	}

	ImGui::TextDisabled("The view shows the resulting terrain, changes apply at once");
	NoisePreviewImGui(world.terrain.Noise(editedComponent));
	ImGui::Separator();
	const bool noiseChanged = utilities::NoiseImGui(world.terrain.Noise(editedComponent));
	if (noiseChanged) {
		RequestPreview();
		changeTerrain = true;
		//Biomes are chosen by the terrain noises too
		if (biomesGeneration) {
			RequestBiomes();
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

bool TerrainGenerationSys::BiomesOffNotice()
{
	if (biomesGeneration) {
		return false;
	}
	ImGui::TextWrapped("Biomes are off. Turn them on in the %s tab.", tabBiomes);
	return true;
}

void TerrainGenerationSys::BiomeTableEditor()
{
	if (BiomesOffNotice()) {
		return;
	}
	if (biomeEditor.ImGuiDraw(world.biomes.table)) {
		RequestBiomes();
	}
}

void TerrainGenerationSys::BiomesEditor()
{
	if (ImGui::Checkbox("Biomes generation", &biomesGeneration)) {
		//The textured view stays: with biomes it shows their materials, without them the material chosen in "Terrain materials"
		const bool textured = renderSettings.displayMode == utilities::heightMapMode::TEXTURED;
		if (biomesGeneration) {
			RequestBiomes();
			if (!textured) {
				renderSettings.displayMode = utilities::heightMapMode::BIOMES;
			}
		}
		else {
			view.ReleaseSplat();
			if (!textured) {
				renderSettings.displayMode = utilities::heightMapMode::TOPOGRAPHICAL;
			}
			//Without biomes the terrain loses their modifiers
			if (world.biomes.modifyTerrain) {
				RequestTerrain();
			}
		}
	}
	if (!biomesGeneration) {
		return;
	}
	if (ImGui::SliderFloat("Blend width", &world.biomes.blendWidth, 0.0f, 0.5f)) {
		RequestBiomes();
	}
	ImGui::SetItemTooltip("How far (in noise values) a biome fades into the nearest one; 0 = sharp borders");

	ImGui::SeparatorText("Climate");
	biome::Climate& climate = world.biomes.climate;
	bool climateChanged = ImGui::SliderFloat("Lapse rate", &climate.lapseRate, 0.0f, 3.0f);
	ImGui::SetItemTooltip("Temperature drop per unit of terrain height above the water (heights are in [0, 1]).\n"
		"Makes high mountains cold: snowy peaks without a rule of their own");
	climateChanged |= ImGui::SliderFloat("Coast humidity", &climate.coastHumidity, -1.0f, 1.0f);
	ImGui::SetItemTooltip("Humidity added at the water surface, fading out with height");
	//Divides the height in the shader, so it stays above 0
	climateChanged |= ImGui::SliderFloat("Coast range", &climate.coastRange, 0.005f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Height above the water where the coast humidity fades to 0");
	if (world.terrain.method != EvaluationMethod::LAYERED) {
		ImGui::TextDisabled("This terrain method has no sea: heights are measured from 0");
	}
	if (climateChanged) {
		RequestBiomes();
	}

	ImGui::SeparatorText("Terrain modifiers");
	BiomeConfig& b = world.biomes;
	if (ImGui::Checkbox("Biomes modify the terrain", &b.modifyTerrain)) {
		//Turning them off has to remove them from the terrain too
		RequestTerrain();
	}
	ImGui::SetItemTooltip("Each biome changes the terrain by its modifiers (in the biome table), mixed across borders");
	ImGui::BeginDisabled(!b.modifyTerrain);
	bool modifiersChanged = ImGui::SliderFloat("Terrain blend width", &b.terrainBlendWidth, 0.01f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("How far (in noise values) the modifiers of neighbouring biomes mix.\n"
		"Always above 0, so the terrain does not jump at biome borders even with sharp colors");
	modifiersChanged |= ImGui::InputInt("Detail seed", &b.detailSeed);
	ImGui::EndDisabled();
	if (modifiersChanged) {
		RequestBiomes();
	}

	ImGui::SeparatorText("Surface materials");
	ImGui::TextDisabled("Seen in the TEXTURED display mode; each biome's mix is in the biome table");
	//Both divide in the shader, so they stay above 0
	bool materialsChanged = ImGui::SliderFloat("Material blend width", &b.materialBlendWidth, 0.01f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("How far (in noise values) the materials of neighbouring biomes mix");
	materialsChanged |= ImGui::SliderFloat("Patchiness", &b.materialPatchiness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("How strongly noise breaks a biome's mix into patches of its materials; 0 = an even mix.\n"
		"At 1 each material covers about its share of the biome's area");
	materialsChanged |= ImGui::SliderFloat("Patch contrast", &b.materialPatchContrast, 1.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("How sharp the edges of the patches are; 1 = soft gradients. Takes full effect at patchiness 1");
	materialsChanged |= ImGui::SliderFloat("Patch size", &b.materialPatchSize, 2.0f, 256.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Wavelength of the patch noise in map texels; the detail seed above chooses its pattern");
	if (materialsChanged) {
		RequestBiomes();
	}
}

void TerrainGenerationSys::BiomeNoisesEditor()
{
	if (BiomesOffNotice()) {
		return;
	}
	if (utilities::ImGuiButtonWrapper("Temperature", editedBiomeComponent == BiomeParameter::TEMPERATURE)) {
		editedBiomeComponent = BiomeParameter::TEMPERATURE;
		RequestPreview();
	}
	ImGui::SameLine();
	if (utilities::ImGuiButtonWrapper("Humidity", editedBiomeComponent == BiomeParameter::HUMIDITY)) {
		editedBiomeComponent = BiomeParameter::HUMIDITY;
		RequestPreview();
	}

	NoisePreviewImGui(world.biomes.Noise(editedBiomeComponent));
	ImGui::Separator();
	if (utilities::NoiseImGui(world.biomes.Noise(editedBiomeComponent))) {
		RequestPreview();
		RequestBiomes();
	}
}

void TerrainGenerationSys::SplineEditor()
{
	const bool layered = world.terrain.method == EvaluationMethod::LAYERED;
	if (world.terrain.method == EvaluationMethod::LINEAR_COMBINE) {
		//Edits would change nothing, so the editor offers a method that uses them instead
		ImGui::TextWrapped("The Linear method uses no splines. Switch to a method that does:");
		if (ImGui::Button("Layered##splineMethod")) {
			world.terrain.method = EvaluationMethod::LAYERED;
			changeTerrain = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Spline##splineMethod")) {
			world.terrain.method = EvaluationMethod::SPLINE_COMBINE;
			changeTerrain = true;
		}
		return;
	}
	const std::pair<WorldGenParameter, const char*> components[] = {
		{ WorldGenParameter::CONTINENTALNESS, "Continentalness" },
		{ WorldGenParameter::MOUNTAINOUSNESS, "Mountainousness" },
		{ WorldGenParameter::WEIRDNESS, layered ? "Peaks & valleys" : "Weirdness" },
	};
	for (const auto& [component, name] : components) {
		if (component != components[0].first) {
			ImGui::SameLine();
		}
		if (utilities::ImGuiButtonWrapper(name, editedSplineComponent == component)) {
			editedSplineComponent = component;
			splineDraggedPoint = -1;
		}
		if (layered && component == WorldGenParameter::WEIRDNESS) {
			ImGui::SetItemTooltip("Shape of mountains from weirdness: low = valleys, high = peaks.\n"
				"Ridge on weirdness (gain 3, offset 1) folds it into peaks and valleys as in Minecraft 1.18");
		}
	}

	//Edited in place: every change regenerates the terrain at once. Which spline weirdness uses depends on the method
	SplinePoints& spline = world.terrain.MethodSpline(editedSplineComponent);
	if (spline.x.size() < 3 || spline.x.size() != spline.y.size()) {
		std::cout << "[ERROR] Spline points not set correctly\n";
		return;
	}
	changeTerrain |= ImGui::Checkbox("Smooth (cubic)", &spline.cubic);
	ImGui::SetItemTooltip("Cubic: a smooth curve through the points, which can bend past them.\nOff: straight segments between the points");
	ImGui::TextDisabled("Drag a point, double-click to add one, right-click a point to remove it");

	const double xmin = -1.0, xmax = 1.0;
	//Points stay in [0, 1]; the view shows a little more, where a cubic curve can overshoot
	const double ymin = 0.0, ymax = 1.0;
	//Distance in pixels at which the mouse picks a point
	const float pickRadius = 10.0f;
	//Smallest distance in x between neighbouring points, keeps x strictly ascending
	const double minGap = 0.01;

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
	if (ImPlot::BeginPlot("##plot", ImVec2(-1, 320), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxis(ImAxis_X1, xLabel, ImPlotAxisFlags_Lock);
		ImPlot::SetupAxis(ImAxis_Y1, yLabel, ImPlotAxisFlags_Lock);
		ImPlot::SetupAxisLimits(ImAxis_X1, xmin, xmax, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, ymin - 0.1, ymax + 0.1, ImGuiCond_Always);

		//The curve the generation uses, sampled the same way as for the GPU
		constexpr int curveSamples = 256;
		const std::vector<float> curve = SampleSpline(spline, curveSamples);
		std::vector<float> curveX(curveSamples);
		for (int i = 0; i < curveSamples; i++) {
			curveX[i] = -1.0f + 2.0f * i / (curveSamples - 1);
		}
		ImPlot::PlotLine("Spline", curveX.data(), curve.data(), curveSamples);
		ImPlot::PlotScatter("Points", spline.x.data(), spline.y.data(), static_cast<int>(spline.x.size()));

		const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
		const ImVec2 mousePixels = ImGui::GetMousePos();
		//Point under the mouse, -1 if none; picked in pixels, since the axes have different scales
		int hovered = -1;
		if (ImPlot::IsPlotHovered()) {
			for (int i = 0; i < static_cast<int>(spline.x.size()); i++) {
				const ImVec2 p = ImPlot::PlotToPixels(spline.x[i], spline.y[i]);
				const float dx = p.x - mousePixels.x, dy = p.y - mousePixels.y;
				if (dx * dx + dy * dy <= pickRadius * pickRadius) {
					hovered = i;
					break;
				}
			}
		}
		const int last = static_cast<int>(spline.x.size()) - 1;

		if (splineDraggedPoint == -1 && hovered != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			splineDraggedPoint = hovered;
		}
		if (splineDraggedPoint != -1) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && splineDraggedPoint <= last) {
				//The end points stay at -1 and 1, so the spline covers the whole noise range
				if (splineDraggedPoint != 0 && splineDraggedPoint != last) {
					spline.x[splineDraggedPoint] = std::clamp(mouse.x, spline.x[splineDraggedPoint - 1] + minGap, spline.x[splineDraggedPoint + 1] - minGap);
				}
				spline.y[splineDraggedPoint] = std::clamp(mouse.y, ymin, ymax);
				changeTerrain = true;
			}
			else {
				splineDraggedPoint = -1;
			}
		}
		else if (hovered != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			//The end points and the minimum of 3 points stay
			if (hovered != 0 && hovered != last && spline.x.size() > 3) {
				spline.x.erase(spline.x.begin() + hovered);
				spline.y.erase(spline.y.begin() + hovered);
				changeTerrain = true;
			}
		}
		else if (hovered == -1 && ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			//Inserted where it keeps x ascending, if there is room next to the neighbours
			const auto next = std::upper_bound(spline.x.begin(), spline.x.end(), mouse.x);
			if (next != spline.x.begin() && next != spline.x.end() && mouse.x - *(next - 1) >= minGap && *next - mouse.x >= minGap) {
				const auto index = next - spline.x.begin();
				spline.x.insert(next, mouse.x);
				spline.y.insert(spline.y.begin() + index, std::clamp(mouse.y, ymin, ymax));
				changeTerrain = true;
			}
		}

		if (splineDraggedPoint != -1 || hovered != -1) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		ImPlot::EndPlot();
	}
}
