#include "TerrainGenerationSys.h"

#include "SendToImported.h"

#include <iostream>
#include <algorithm>
#include <chrono>

#include "imgui.h"
#include "implot.h"

#include "BiomeSettingsPanel.h"
#include "ErosionPanel.h"
#include "Preset.h"
#include "SplineSampling.h"
#include "TerrainMaterials.h"
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

	if (!view.Initialize(world.width, world.height) || !chunks.Initialize() || !gpu.Initialize() || !noisePreview.Initialize() || !gpuErosion.Initialize()) {
		return false;
	}
	//OVERLAY holds the eroded copy of the terrain, drawn with the materials of the terrain
	view.SetOverlaySplat(true);

	//The first frame needs a map
	RequestTerrain();
	GenerateFinite();

	std::cout << "[LOG] TerrainGenerationSys initialized\n";
	return true;

}

void TerrainGenerationSys::Update(Camera& camera)
{
	gpu.PollMeasure(lastGenerationMs);
	exporter.Update();
	const bool leftInfinite = chunksShown && !infiniteGeneration;
	chunksShown = infiniteGeneration;
	if (leftInfinite) {
		//The partial map shows the part of the world under the camera
		MoveMap(OffsetUnder(camera));
		//Changes made meanwhile went only to the chunks
		RequestTerrain();
		if (biomesGeneration) {
			RequestBiomes();
		}
	}
	//Following waits while an eroded copy is shown: flying over the copy (beyond the +X edge) would move the map and drop the copy
	else if (!infiniteGeneration && (centerOnCamera || (followCamera && !eroded.shown))) {
		const glm::ivec2 under = OffsetUnder(camera);
		const glm::ivec2 shift = glm::abs(under - mapOffset);
		if (centerOnCamera || shift.x > world.width / 4 || shift.y > world.height / 4) {
			MoveMap(under);
		}
	}
	centerOnCamera = false;
	GeneratePreview();
	MeasureLand();
	UpdateRivers(camera);
	if (infiniteGeneration) {
		//No erosion in the infinite world
		if (eroded.shown || vegetationLayer.HasExtraInstances()) {
			ResetErosion();
		}
		chunks.SetBounds(std::nullopt);
		UpdateChunks(camera);
		UpdateVegetation();
	}
	else {
		//The chunks of the partial map follow every change of it
		if (pending.Any()) {
			chunks.Invalidate();
		}
		GenerateFinite();
		UpdateErosion();
		if (partialDetails) {
			chunks.SetBounds(MapRect());
			UpdateChunks(camera);
			UpdateVegetation();
		}
		else if (vegetationLayer.HasExtraInstances()) {
			//The plants of the eroded copy are drawn alone
			vegetationLayer.ForgetChunks();
		}
	}
	//After generating: the map size may have changed meanwhile
	if (leftInfinite) {
		view.BringCameraToMap(camera, renderSettings);
	}
}

void TerrainGenerationSys::UpdateVegetation()
{
	VegetationLayer::World vegetationWorld;
	vegetationWorld.biomes = biomesGeneration ? &world.biomes.table : nullptr;
	vegetationWorld.waterHeight = world.terrain.WaterHeight();
	vegetationWorld.heightScale = renderSettings.heightScale;
	vegetationWorld.seed = static_cast<uint32_t>(world.biomes.detailSeed);
	if (RiverWater()) {
		vegetationWorld.rivers = riverNetwork;
	}
	vegetationLayer.Update(chunks, vegetationModels, vegetationWorld);
}

glm::ivec2 TerrainGenerationSys::OffsetUnder(const Camera& camera) const
{
	//Map texel t of a map with offset o lies at world XZ (o + t - (size - 1) / 2) * worldUnitsPerTexel (ChunkWorld::AlignWithMap
	//with TerrainView centred on the offset), so the middle of the map is under the camera for o = camera / worldUnitsPerTexel
	const glm::vec3 position = camera.GetPosition() / renderSettings.modelScale;
	return glm::ivec2(glm::round(glm::vec2(position.x, position.z) / TerrainView::worldUnitsPerTexel));
}

void TerrainGenerationSys::MoveMap(glm::ivec2 offset)
{
	if (offset == mapOffset) {
		return;
	}
	mapOffset = offset;
	//The river region and the noise picture lie around the map too
	RequestTerrain();
	RequestPreview();
	if (biomesGeneration) {
		RequestBiomes();
	}
}

ChunkWorld::TexelRect TerrainGenerationSys::MapRect() const
{
	const glm::ivec2 offset = view.GetMapOffset();
	return { offset, offset + glm::ivec2(view.GetMapWidth(), view.GetMapHeight()) };
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
	const HeightField sample = gpu.SampleTerrain(world.terrain, MapCenter(), land::sampleSize, land::sampleStep,
		glm::ivec2(world.width, world.height), biomesGeneration ? &world.biomes : nullptr);
	landStats = land::Measure(sample, world.terrain.WaterHeight());
	landStatsOld = false;
}

glm::vec2 TerrainGenerationSys::MapPositionUnder(const Camera& camera) const
{
	//World XZ = (map position - (size - 1) / 2) * worldUnitsPerTexel (see OffsetUnder)
	const glm::vec3 position = camera.GetPosition() / renderSettings.modelScale;
	return glm::vec2(position.x, position.z) / TerrainView::worldUnitsPerTexel + (glm::vec2(world.width, world.height) - 1.0f) * 0.5f;
}

void TerrainGenerationSys::UpdateRivers(const Camera& camera)
{
	if (std::optional<RiverResult> result = riverJob.TryTake()) {
		riverMap.Upload(result->network, result->water);
		riverCells = result->network.riverCells;
		lakeCells = result->network.lakeCells;
		lakeCount = result->network.lakes;
		riverMs = result->ms;
		riverNetwork = std::make_shared<const hydrology::Network>(std::move(result->network));
		//Not RequestTerrain: that would start the network again
		if (world.rivers.method == rivers::Method::REGION) {
			pending.terrain = true;
		}
	}
	const rivers::RiverConfig& config = world.rivers;
	//The terrain did not change, so not RequestRivers / RequestTerrain: the arriving network replaces the chunks.
	//The step of the region shown, not of the edited size: dragging "Region size" applies on release
	if (config.method == rivers::Method::REGION && infiniteGeneration && riversFollowCamera && !riverJob.IsRunning() && riverRegion
		&& hydrology::LeftRegion(*riverRegion, MapPositionUnder(camera))) {
		riversStale = true;
	}
	if (config.method != rivers::Method::REGION || !riversStale || riverJob.IsRunning()) {
		return;
	}
	if (world.width < 2 || world.height < 2 || world.width > 8192 || world.height > 8192 || config.cellSize < 1) {
		return;
	}
	riversStale = false;
	//Around the middle of the finite map, or in the infinite world around the grid point under the camera (or where the region is, when it does
	//not follow the camera), with the island and biome modifiers of the terrain shown (the sample reads the GPU back)
	const float cellSize = static_cast<float>(config.cellSize);
	const int cells = std::clamp(config.regionSize / config.cellSize, 16, 2048);
	riverRegion = hydrology::PlaceRegion(infiniteGeneration, riversFollowCamera, riverRegion, MapPositionUnder(camera), MapCenter(),
		config.regionSize, config.cellSize);
	const glm::vec2 center = riverRegion->center;
	HeightField sample = gpu.SampleTerrain(world.terrain, center, cells, cellSize, glm::ivec2(world.width, world.height),
		biomesGeneration ? &world.biomes : nullptr);
	const glm::vec2 origin = center - glm::vec2(static_cast<float>(cells) * cellSize * 0.5f);
	const float waterHeight = world.terrain.WaterHeight();
	const float texelsPerHeight = renderSettings.heightScale / TerrainView::worldUnitsPerTexel;
	riverJob.Start([sample = std::move(sample), origin, cellSize, waterHeight, texelsPerHeight, config]() {
		const auto start = std::chrono::steady_clock::now();
		RiverResult result;
		result.network = hydrology::Compute(sample, origin, cellSize, waterHeight, config);
		result.water = hydrology::BuildWaterMesh(result.network, waterHeight, texelsPerHeight);
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
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
	const RiverInput rivers = Rivers();
	chunks.GenerateNext([&](const MapArea& area, TextureClass& height, TextureClass* colors, TextureClass* ids, TextureClass* splat) {
		//The maps of a chunk are generated together, so with biomes one pass gives all of them
		if (colors && ids) {
			gpu.GenerateWorld(world.terrain, world.biomes, area, height, *colors, *ids, splat, &rivers);
		}
		else {
			gpu.GenerateTerrain(world.terrain, area, height, biomesGeneration ? &world.biomes : nullptr, &rivers);
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
	//The eroded copy belongs to the terrain it was eroded from; new biomes only change what grows on it
	if (request.terrain) {
		ResetErosion();
	}
	else if (request.biomes && eroded.shown) {
		eroded.plantsWanted = true;
	}
	view.Resize(width, height);
	//Every layer is regenerated at a move (MoveMap requests all of them), so they all lie where the offset says
	view.SetMapOffset(mapOffset);
	//The part of the world at the offset; islands fill the map at the world origin, as in the infinite world
	MapArea area;
	area.origin = glm::vec2(mapOffset);
	area.islandSize = glm::ivec2(width, height);

	if (request.biomes && (!biomeIds || biomeIds->GetWidth() != width || biomeIds->GetHeight() != height)) {
		biomeIds = std::make_unique<TextureClass>(width, height, GL_R32I);
	}
	gpu.BeginMeasure();
	const RiverInput rivers = Rivers();
	if (request.terrain && request.biomes && biomesGeneration) {
		//Both in one pass: the noises and the biome table are evaluated once
		gpu.GenerateWorld(world.terrain, world.biomes, area, view.LayerTexture(TerrainView::Layer::TERRAIN, width, height),
			view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds, &view.SplatTexture(width, height), &rivers);
	}
	else {
		if (request.terrain) {
			gpu.GenerateTerrain(world.terrain, area, view.LayerTexture(TerrainView::Layer::TERRAIN, width, height), biomesGeneration ? &world.biomes : nullptr,
				&rivers);
		}
		if (request.biomes) {
			gpu.GenerateBiomes(world.biomes, world.terrain, area, view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds,
				&view.SplatTexture(width, height), &rivers);
		}
	}
	gpu.EndMeasure();

	if (request.biomes) {
		biomeMap.Resize(width, height);
		biomeIds->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(biomeMap.Size() * sizeof(int)), biomeMap.Data());
	}
}

void TerrainGenerationSys::ResetErosion()
{
	eroded.shown = false;
	eroded.plantsWanted = false;
	eroded.version++;
	vegetationLayer.ClearExtraInstances();
}

void TerrainGenerationSys::UpdateErosion()
{
	if (eroded.requested) {
		eroded.requested = false;
		if (const TextureClass* base = view.GetLayerTexture(TerrainView::Layer::TERRAIN)) {
			//The copy keeps the size of the terrain: a new size regenerates the terrain, which resets erosion
			TextureClass& copy = view.LayerTexture(TerrainView::Layer::OVERLAY, base->GetWidth(), base->GetHeight());
			gpuErosion.Erode(world.erosion, eroded.shown ? copy : *base, copy, eroded.runs++);
			eroded.shown = true;
			eroded.method = world.erosion.method;
			eroded.version++;
			eroded.plantsWanted = true;
		}
	}
	gpuErosion.PollMeasure(eroded.method, eroded.ms);
	//Biomes turned off without a terrain change (modifyTerrain off): no plants without biomes, as on the map; turning them on requests biomes,
	//which places the plants of the copy again
	if (!biomesGeneration && vegetationLayer.HasExtraInstances()) {
		vegetationLayer.ClearExtraInstances();
	}

	if (std::optional<ErodedPlants> result = erodedPlantsJob.TryTake()) {
		//Plants of an older copy (eroded again or reset meanwhile) are dropped
		if (eroded.shown && biomesGeneration && result->version == eroded.version) {
			vegetationLayer.SetExtraInstances(result->instances);
			eroded.plantsMs = result->ms;
		}
	}
	//Density, models or plants changed: the plants of the copy follow, as the map's do
	if (eroded.shown && biomesGeneration && vegetationLayer.SettingsVersion() != eroded.plantSettings) {
		eroded.plantSettings = vegetationLayer.SettingsVersion();
		eroded.plantsWanted = true;
	}
	//One placement at a time: a newer copy waits for the running one
	if (eroded.plantsWanted && !erodedPlantsJob.IsRunning()) {
		eroded.plantsWanted = false;
		StartErodedPlants();
	}
}

void TerrainGenerationSys::StartErodedPlants()
{
	eroded.plantSettings = vegetationLayer.SettingsVersion();
	const int width = view.GetMapWidth(), height = view.GetMapHeight();
	if (!eroded.shown || !biomesGeneration || !AnyVegetationModel() || biomeMap.GetWidth() != width || biomeMap.GetHeight() != height) {
		vegetationLayer.ClearExtraInstances();
		return;
	}
	vegexport::Input input = VegetationExportInput();
	eroded.treesOnly = static_cast<int64_t>(width) * height > allPlantsMaxTexels;
	if (eroded.treesOnly) {
		input.layerMask = 1u << static_cast<int>(vegetation::Layer::TREES);
	}
	HeightField copy = view.ReadLayer(TerrainView::Layer::OVERLAY);
	std::vector<glm::vec4> surface = view.ReadSurface(TerrainView::Layer::OVERLAY);
	//Where the centre of texel (0, 0) of the copy is drawn: that of the map, shifted along X
	const glm::vec2 texelZero = view.GetWorldCorner() + glm::vec2(ErodedShift(), 0.0f) + 0.5f * TerrainView::worldUnitsPerTexel;
	const float metresPerUnit = input.metresPerUnit;
	const uint64_t version = eroded.version;
	erodedPlantsJob.Start([copy = std::move(copy), surface = std::move(surface), ids = biomeMap, input = std::move(input), texelZero, metresPerUnit, version]() {
		const auto start = std::chrono::steady_clock::now();
		ErodedPlants result;
		result.instances = vegexport::ToAppInstances(vegexport::PlaceMap(copy, surface, ids, input), texelZero, metresPerUnit);
		result.version = version;
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
}

bool TerrainGenerationSys::AnyVegetationModel() const
{
	return vegetationModels && std::any_of(vegetationModels->GetModels().begin(), vegetationModels->GetModels().end(),
		[](const VegetationModels::Model& m) { return m.state == VegetationModels::State::READY && !m.variants.empty(); });
}

vegexport::Input TerrainGenerationSys::VegetationExportInput() const
{
	vegexport::Input input;
	for (const biome::Biome& b : world.biomes.table) {
		input.plants.push_back(b.plants);
	}
	for (const VegetationModels::Model& m : vegetationModels->GetModels()) {
		const bool ready = m.state == VegetationModels::State::READY && !m.variants.empty();
		input.models.push_back({ m.name, m.kind, m.asset, ready ? static_cast<int>(m.variants.size()) : 0, vegetationModels->SourceFile(m), m.lodTargets,
			vegetationModels->CacheFile(m) });
	}
	input.settings.heightScale = renderSettings.heightScale;
	input.settings.waterHeight = world.terrain.WaterHeight();
	input.settings.density = vegetationLayer.GetDensity();
	input.settings.seed = static_cast<uint32_t>(world.biomes.detailSeed);
	if (RiverWater()) {
		input.rivers = riverNetwork;
	}
	input.mapTexel = view.GetMapOffset();
	input.metresPerUnit = renderSettings.heightScale / TerrainView::worldUnitsPerTexel;
	input.lodRanges = vegetationLayer.LodRanges();
	return input;
}

void TerrainGenerationSys::ErosionEditor()
{
	if (infiniteGeneration) {
		ImGui::TextWrapped("Erosion works on the partial map: switch to Partial generation in the Display tab.");
		return;
	}
	ImGui::TextWrapped("Erodes a copy of the map, shown next to it with its water and plants. Any change of the terrain drops the copy.");
	ErosionParametersImGui(world.erosion);

	ImGui::Separator();
	if (ImGui::Button("Erode map")) {
		eroded.requested = true;
	}
	ImGui::SetItemTooltip("Again = erodes the copy further");
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		//Drops a not yet started erosion too
		eroded.requested = false;
		ResetErosion();
	}
	if (!eroded.shown) {
		return;
	}
	ImGui::Text("Last erosion: %s, %.1f ms", ErosionMethodName(eroded.method), eroded.ms);
	if (eroded.plantsWanted || erodedPlantsJob.IsRunning()) {
		ImGui::TextUnformatted("Placing the plants of the copy...");
	}
	else if (vegetationLayer.HasExtraInstances()) {
		ImGui::Text("Plants of the copy: %zu (%.0f ms)%s", vegetationLayer.ExtraCount(), eroded.plantsMs, eroded.treesOnly ? ", trees only (large map)" : "");
	}
	else if (!biomesGeneration) {
		ImGui::TextWrapped("Plants grow only with biomes: turn on biome generation.");
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
		noisePreview.Generate(gpu, *edited, world.width, world.height, mapOffset);
	}
}

void TerrainGenerationSys::RequestPreview()
{
	pending.preview = true;
}

void TerrainGenerationSys::RequestTerrain()
{
	pending.terrain = true;
	//The river network flows over the terrain
	RequestRivers();
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
		RequestRivers();
	}
}

void TerrainGenerationSys::ImGuiPresets(Camera& camera)
{
	std::string error;
	switch (presets.ImGuiDraw()) {
	case PresetPanel::Action::SAVE: {
		const std::optional<glm::ivec2> offset = presetPosition ? std::optional<glm::ivec2>(PlaceOffset(camera)) : std::nullopt;
		if (preset::Save(presets.SavePath(), world, offset, error)) {
			presets.ReportSaved();
		}
		else {
			presets.ReportError(error);
		}
		break;
	}
	case PresetPanel::Action::LOAD: {
		WorldConfig loaded;
		std::optional<glm::ivec2> offset;
		if (preset::Load(presets.LoadPath(), loaded, offset, error)) {
			ApplyWorld(loaded);
			if (presetPosition && offset) {
				GoToMap(*offset, camera);
			}
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
	ImGui::Checkbox("Map position", &presetPosition);
	ImGui::SetItemTooltip("Saving: the preset also keeps where the partial map lies (in the infinite world: the map under the camera).\n"
		"Loading: a preset with a position moves the map there and the camera above it. Off = only the parameters");

	ImGui::SeparatorText("Places");
	const auto here = [&](const std::string& name) {
		places::Place place;
		place.name = name;
		place.camera = camera.GetPosition() / renderSettings.modelScale;
		place.yaw = camera.GetYaw();
		place.pitch = camera.GetPitch();
		place.mapOffset = PlaceOffset(camera);
		return place;
	};
	if (const std::optional<places::Place> place = places.ImGuiDraw(here)) {
		MoveMap(place->mapOffset);
		camera.SetView(place->camera * renderSettings.modelScale, place->yaw, place->pitch);
	}

	ImGui::SeparatorText("Export packs");
	if (const std::optional<exportpack::PackSource> source = packs.ImGuiDraw()) {
		LoadPack(*source, camera);
	}
}

void TerrainGenerationSys::GoToMap(glm::ivec2 offset, Camera& camera)
{
	MoveMap(offset);
	TerrainView::LookDownAtMap(camera, renderSettings, offset, glm::ivec2(world.width, world.height));
}

void TerrainGenerationSys::LoadPack(const exportpack::PackSource& source, Camera& camera)
{
	if (source.mode != packMode) {
		packs.ReportError("The pack was made in the " + source.mode + " mode");
		return;
	}
	if (source.generation.empty()) {
		packs.ReportError("The pack has no generation parameters");
		return;
	}
	if (!preset::ValidMapOffset(source.worldOrigin)) {
		packs.ReportError("The world position of the pack is out of range");
		return;
	}
	WorldConfig loaded;
	std::string error;
	if (!preset::FromJson(source.generation, loaded, error)) {
		packs.ReportError("Generation parameters: " + error);
		return;
	}
	ApplyWorld(loaded);
	GoToMap(source.worldOrigin, camera);
	packs.ReportLoaded();
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
	renderSettings.shadows = nullptr;
	mapLabels.Clear();
	if (chunksShown) {
		//Shadow maps first: they need the plants culled for the sun before the camera culling reuses the buffers
		if (shadows.Render(renderer, camera, light, renderSettings, chunks, vegetationLayer, vegetationModels)) {
			renderSettings.shadows = &shadows;
		}
		//Plants first: the terrain behind them then fails the depth test before its (expensive) shader runs; in a forest this
		//halves the cost of the terrain
		vegetationLayer.Draw(camera, light, renderSettings, vegetationModels);
		if (terrainMaterials) {
			grass.Draw(chunks, camera, light, renderSettings, *terrainMaterials);
		}
		chunks.Draw(renderer, camera, light, renderSettings);
		//Last: it reads a copy of everything drawn before
		water.Draw(chunks, camera, light, renderSettings, world.terrain.WaterHeight() * renderSettings.heightScale, RiverWater());
		return;
	}
	//The eroded copy next to the map along X, separated by a 5% gap; the 2D view frames both
	const TextureClass* erodedHeight = eroded.shown ? view.GetLayerTexture(TerrainView::Layer::OVERLAY) : nullptr;
	const float shift = ErodedShift();
	if (erodedHeight) {
		const glm::vec2 half(view.GetWorldWidth() / 2.0f, view.GetWorldHeight() / 2.0f);
		view.SetFlatViewBounds(view.GetWorldCenter() - half, view.GetWorldCenter() + glm::vec2(shift + half.x, half.y));
	}
	else {
		view.ResetFlatViewBounds();
	}

	//The partial map: the same plants, grass and shadows as the infinite world, from its chunks limited to the map (and the plants of the copy)
	if (partialDetails && chunks.GetBounds()) {
		if (shadows.Render(renderer, camera, light, renderSettings, chunks, vegetationLayer, vegetationModels)) {
			renderSettings.shadows = &shadows;
		}
		vegetationLayer.Draw(camera, light, renderSettings, vegetationModels);
		if (terrainMaterials) {
			grass.Draw(chunks, camera, light, renderSettings, *terrainMaterials);
		}
	}
	else if (vegetationLayer.HasExtraInstances()) {
		vegetationLayer.Draw(camera, light, renderSettings, vegetationModels);
	}
	view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::TERRAIN);
	const glm::vec3 copyOffset(shift, 0.0f, 0.0f);
	if (erodedHeight) {
		view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::OVERLAY, copyOffset);
	}
	//Water last: it reads a copy of everything drawn before
	if (const TextureClass* height = view.GetLayerTexture(TerrainView::Layer::TERRAIN)) {
		const glm::vec2 size(view.GetWorldWidth(), view.GetWorldHeight());
		//World XZ of map position (0, 0), where the river network lies: the centre of texel (0, 0) of the map without an offset
		const glm::vec2 worldTexelOrigin = (-glm::vec2(view.GetMapWidth(), view.GetMapHeight()) * 0.5f + 0.5f) * TerrainView::worldUnitsPerTexel;
		const float waterLevel = world.terrain.WaterHeight() * renderSettings.heightScale;
		water.DrawMap(*height, view.GetWorldCorner(), size, worldTexelOrigin, camera, light, renderSettings, waterLevel, view.GetFlatViewMin(),
			view.GetFlatViewMax(), RiverWater());
		//The sea follows the eroded heights; rivers and lakes keep the levels of the network, shifted with the copy
		if (erodedHeight) {
			const glm::vec2 copyShift(shift, 0.0f);
			water.DrawMap(*erodedHeight, view.GetWorldCorner() + copyShift, size, worldTexelOrigin + copyShift, camera, light, renderSettings, waterLevel,
				view.GetFlatViewMin(), view.GetFlatViewMax(), RiverWater());
		}
	}
	if (!erodedHeight) {
		return;
	}
	const glm::mat4 viewProjection = renderSettings.map2d
		? [&] { const FrameUniforms::Block block = TerrainView::FrameBlock(camera, light, renderSettings, view.GetFlatViewMin(), view.GetFlatViewMax());
			return block.projection * block.view; }()
		: camera.GetViewProjection();
	const std::vector<std::pair<float, std::string>> names = { { 0.0f, "Terrain" }, { shift, std::string("Eroded: ") + ErosionMethodName(eroded.method) } };
	//Above the highest terrain (heights stay within +-1.5), so that a mountain does not hide the label in 3D; the 2D view ignores the height
	mapLabels.Place(viewProjection, names, view.GetWorldCenter(), view.GetWorldHeight() / 2.0f, 1.5f * std::abs(renderSettings.heightScale),
		renderSettings.modelScale);
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
		{ tabRivers, [this] { RiversEditor(); } },
		{ tabErosion, [this] { ErosionEditor(); } },
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
	}
	else {
		MapPositionImGui();
	}
	water.ImGuiDraw();
	if (infiniteGeneration || partialDetails) {
		shadows.ImGuiDraw();
	}
}

void TerrainGenerationSys::MapPositionImGui()
{
	ImGui::SeparatorText("Partial map");
	const glm::ivec2 shown = view.GetMapOffset();
	const glm::vec2 center = view.GetWorldCenter() * renderSettings.modelScale;
	ImGui::Text("Texels %d .. %d x %d .. %d of the world", shown.x, shown.x + view.GetMapWidth() - 1, shown.y, shown.y + view.GetMapHeight() - 1);
	ImGui::Text("Middle at camera position (%.0f, %.0f)", center.x, center.y);
	ImGui::SetItemTooltip("World coordinates as the camera has them: leaving Infinite generation centres the map on the camera");

	int offset[2] = { mapOffset.x, mapOffset.y };
	ImGui::InputInt2("Offset (texels)", offset);
	//Applied when the field is left, so typing a number does not regenerate the map at every digit
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		MoveMap(glm::ivec2(offset[0], offset[1]));
	}
	ImGui::SetItemTooltip("Moves the map over the world by whole texels (1 texel = 1 m = 4 world units):\n"
		"its middle lies at camera position 4 x offset (before Model scale); 0, 0 = the map of the world origin");
	if (ImGui::Button("Centre on camera")) {
		centerOnCamera = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("World origin")) {
		MoveMap(glm::ivec2(0));
	}
	ImGui::Checkbox("Follow camera", &followCamera);
	ImGui::SetItemTooltip("Centres the map again when the camera gets farther than a quarter of the map from its middle\n"
		"(the map, and the rivers of the Region method, are generated again); paused while an eroded copy is shown");
	ImGui::Checkbox("Plants, grass and shadows", &partialDetails);
	ImGui::SetItemTooltip("The vegetation, grass and sun shadows of the infinite world on the map, from chunks of the world limited to it\n"
		"(generated as the infinite world generates them, near the camera). Off = only the map, faster while editing");
}

void TerrainGenerationSys::ImGuiSaving()
{
	if (infiniteGeneration) {
		ImGui::TextWrapped("Saving works on the finite map: switch to Partial generation in the Display tab.");
		return;
	}
	ExportPanel::Pack pack;
	pack.mode = packMode;
	pack.range = heightmap::terrainRange;
	pack.metresPerUnit = renderSettings.heightScale / TerrainView::worldUnitsPerTexel;
	pack.worldOrigin = view.GetMapOffset();
	if (world.terrain.method == EvaluationMethod::LAYERED) {
		pack.seaLevel = world.terrain.WaterHeight();
	}
	pack.generation = [this] { return preset::ToJson(world); };
	pack.heightmaps = { { view.GetMapWidth(), view.GetMapHeight(), "", [this] { return view.ReadLayer(TerrainView::Layer::TERRAIN); } } };
	if (eroded.shown && view.GetLayerTexture(TerrainView::Layer::OVERLAY)) {
		pack.heightmaps.push_back({ view.GetMapWidth(), view.GetMapHeight(), eroded.method == erosion::Method::PIPES ? "_eroded_pipes" : "_eroded_droplets",
			[this] { return view.ReadLayer(TerrainView::Layer::OVERLAY); } });
	}

	//Material and biome maps exist only with biomes, made with the same size as the height map
	const TextureClass* splat = view.GetSplatTexture();
	const TextureClass* colors = view.GetLayerTexture(TerrainView::Layer::BIOMES);
	const bool sameSize = splat && colors && !biomeMap.Empty() && splat->GetWidth() == view.GetMapWidth() && splat->GetHeight() == view.GetMapHeight()
		&& biomeMap.GetWidth() == view.GetMapWidth() && biomeMap.GetHeight() == view.GetMapHeight();
	if (biomesGeneration && sameSize) {
		pack.materials = ExportPanel::SplatSource(*splat, materialNames);
		if (terrainMaterials) {
			pack.materials->textures = [materials = terrainMaterials](float metresPerTexel) { return materials->ExportTextures(metresPerTexel); };
		}
		pack.biomes = ExportPanel::BiomeTableSource(world.biomes.table, [this] { return biomeMap; }, *colors);
	}
	else {
		pack.layersMissing = "Material and biome maps: turn on biome generation.";
	}

	//Vegetation: the plants the infinite world places on these texels, with the same settings (seed, density, water, rivers)
	if (biomesGeneration && sameSize && AnyVegetationModel()) {
		ExportPanel::VegetationSource vegetation;
		vegetation.input = [this] { return VegetationExportInput(); };
		vegetation.readSurface = [this] { return view.ReadSurface(TerrainView::Layer::TERRAIN); };
		vegetation.readIds = [this] { return biomeMap; };
		pack.vegetation = std::move(vegetation);
	}
	else {
		pack.vegetationMissing = !biomesGeneration || !sameSize ? "Vegetation grows only with biomes: turn on biome generation."
			: "No vegetation models loaded (python tools/fetch_models.py).";
	}

	//Rivers and lakes of the Region method, the network the terrain was carved with
	if (RiverWater() && riverNetwork) {
		const float metresPerUnit = pack.metresPerUnit;
		const float metresPerTexel = pack.metresPerTexel;
		pack.riverWater = [this, metresPerUnit, metresPerTexel] {
			const float texelsPerHeight = renderSettings.heightScale / TerrainView::worldUnitsPerTexel;
			const std::vector<hydrology::WaterVertex> water = hydrology::BuildWaterMesh(*riverNetwork, world.terrain.WaterHeight(), texelsPerHeight);
			//The network lies in world map positions, the pack starts at the map's texel (0, 0)
			const glm::vec2 offset(view.GetMapOffset());
			std::vector<glm::vec3> triangles;
			triangles.reserve(water.size());
			for (const hydrology::WaterVertex& v : water) {
				triangles.emplace_back((v.x - offset.x) * metresPerTexel, v.level * metresPerUnit, (v.y - offset.y) * metresPerTexel);
			}
			return triangles;
		};
	}
	exporter.ImGuiDraw(pack);
	if (std::optional<mapproject::Project> sent = SendToImportedImGui(pack)) {
		sentMap = std::move(sent);
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
			//Biome map size, not world.width/height - those may already hold an unapplied resize. TerrainView centers the map on its offset
			const int width = biomeMap.GetWidth(), height = biomeMap.GetHeight();
			const glm::ivec2 texel(glm::floor((position - view.GetWorldCenter()) / TerrainView::worldUnitsPerTexel + glm::vec2(width, height) / 2.0f));
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
	if (biomepanel::Blend(world.biomes)) {
		RequestBiomes();
	}

	const bool climateChanged = biomepanel::Climate(world.biomes.climate);
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

	if (biomepanel::Materials(b, false)) {
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

void TerrainGenerationSys::RiversEditor()
{
	rivers::RiverConfig& r = world.rivers;
	bool changed = false;
	int method = static_cast<int>(r.method);
	const char* methods[] = { "None", "Region (hydrology)", "Noise" };
	if (ImGui::Combo("Method##rivers", &method, methods, IM_ARRAYSIZE(methods))) {
		r.method = static_cast<rivers::Method>(method);
		changed = true;
	}
	ImGui::SetItemTooltip("Region: water flows downhill over a region around the map, depressions fill into lakes,\n"
		"rivers start where enough area drains and widen downstream (computed in the background).\n"
		"Noise: channels along the zero lines of a noise, down to the sea level, only in the lowlands; covers the infinite world");
	if (r.method == rivers::Method::NONE) {
		return;
	}

	ImGui::SeparatorText("Channel");
	changed |= ImGui::SliderFloat("Bank width", &r.bankWidth, 4.0f, 256.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Distance from the channel over which the carving fades out (the valley)");
	changed |= ImGui::SliderFloat("Bank slope", &r.bankSlope, 0.0f, 0.01f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Height per texel the banks rise from the water; 0.004 = 45 degrees in TerrainGen");
	changed |= ImGui::SliderFloat("Depth per width", &r.depthPerWidth, 0.0f, 0.005f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Depth of the bed below the water per texel of half width");

	ImGui::SeparatorText("Near water");
	changed |= ImGui::SliderFloat("Humidity", &r.humidity, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Humidity added to the biomes at a river (seen with biomes generation on)");
	changed |= ImGui::SliderFloat("Humidity range", &r.humidityRange, 1.0f, 512.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Distance from the channel over which the added humidity fades out");
	const std::string bedName = r.bedMaterial < 0 ? "None"
		: r.bedMaterial < static_cast<int>(materialNames.size()) ? materialNames[r.bedMaterial] : "Material " + std::to_string(r.bedMaterial);
	if (ImGui::BeginCombo("Bed material", bedName.c_str())) {
		if (ImGui::Selectable("None", r.bedMaterial < 0)) {
			r.bedMaterial = -1;
			changed = true;
		}
		for (int m = 0; m < static_cast<int>(materialNames.size()) && m < biome::maxMaterials; m++) {
			if (ImGui::Selectable(materialNames[m].c_str(), r.bedMaterial == m)) {
				r.bedMaterial = m;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Surface material of the river bed (TEXTURED display mode, with biomes generation on)");

	if (r.method == rivers::Method::REGION) {
		ImGui::SeparatorText("Region");
		const int cellSizes[] = { 4, 8, 16, 32 };
		const char* cellNames[] = { "4", "8", "16", "32" };
		int cellIndex = 1;
		for (int i = 0; i < IM_ARRAYSIZE(cellSizes); i++) {
			cellIndex = cellSizes[i] == r.cellSize ? i : cellIndex;
		}
		if (ImGui::Combo("Cell size", &cellIndex, cellNames, IM_ARRAYSIZE(cellNames))) {
			r.cellSize = cellSizes[cellIndex];
			changed = true;
		}
		ImGui::SetItemTooltip("Map texels between samples of the terrain the water flows over");
		//Region between 16 and 2048 cells (Preset.cpp)
		int regionSize = r.regionSize;
		if (ImGui::SliderInt("Region size", &regionSize, 16 * r.cellSize, 2048 * r.cellSize, "%d texels", ImGuiSliderFlags_Logarithmic)) {
			r.regionSize = regionSize;
		}
		changed |= ImGui::IsItemDeactivatedAfterEdit();
		r.regionSize = std::clamp(r.regionSize, 16 * r.cellSize, 2048 * r.cellSize);
		ImGui::SetItemTooltip("Side of the square that gets rivers: around the middle of the partial map, or following the camera in the infinite world;\n"
			"they fade out at its border. Applied on release: the network is computed again");
		changed |= ImGui::SliderFloat("Border fade", &r.borderFade, 16.0f, 2048.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
		//View state, not generation: toggling it does not compute the network again
		ImGui::Checkbox("Follow camera (infinite world)", &riversFollowCamera);
		ImGui::SetItemTooltip("The region jumps by a quarter of its side once the camera gets farther than a quarter from its middle\n"
			"(onto a fixed grid, so a place always gets the same network). The new network is computed in the background and the chunks\n"
			"are replaced from the nearest; rivers in the part the regions share may change. Off = the region stays where it is");
		if (riverRegion) {
			ImGui::Text("Region centre: %.0f, %.0f (map texels)", riverRegion->center.x, riverRegion->center.y);
		}

		ImGui::SeparatorText("Rivers");
		changed |= ImGui::SliderFloat("Source area", &r.sourceArea, 1.0e4f, 1.0e7f, "%.3g texels^2", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Area that has to drain through a place for a river to start there; less = more rivers");
		changed |= ImGui::SliderFloat("Min half width", &r.minHalfWidth, 0.5f, 32.0f, "%.1f texels", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Max half width", &r.maxHalfWidth, 0.5f, 128.0f, "%.1f texels", ImGuiSliderFlags_AlwaysClamp);
		r.maxHalfWidth = std::max(r.maxHalfWidth, r.minHalfWidth);
		ImGui::SetItemTooltip("Rivers widen with the square root of the drained area, from the min at the source up to this");
		changed |= ImGui::SliderFloat("Meander", &r.meander, 0.0f, 64.0f, "%.1f texels", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Amplitude of the bends added to the course");
		changed |= ImGui::SliderInt("Smoothing", &r.smoothing, 0, 32, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Passes smoothing the staircase of the sampling grid out of the courses");
		changed |= ImGui::SliderFloat("Surface drop", &r.surfaceDrop, 0.0f, 0.02f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("How far below the terrain of its course the water surface lies");
		changed |= ImGui::SliderFloat("Min lake depth", &r.minLakeDepth, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("A filled depression deeper than this is a lake; rivers cross lakes without a channel");

		ImGui::SeparatorText("Network");
		if (riverJob.IsRunning()) {
			ImGui::TextDisabled("Computing...");
		}
		else if (!riverMap.Ready()) {
			ImGui::TextDisabled("Not computed yet");
		}
		if (riverMap.Ready()) {
			ImGui::Text("River cells: %d, lakes: %d (%d cells)", riverCells, lakeCount, lakeCells);
			ImGui::Text("Computed in %.0f ms (%d x %d cells)", riverMs, riverMap.Size().x, riverMap.Size().y);
		}
	}
	else {
		ImGui::SeparatorText("Noise");
		changed |= ImGui::SliderFloat("Scale##rivers", &r.noiseScale, 100.0f, 20000.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Wavelength of the noise: typical distance between rivers");
		changed |= ImGui::SliderFloat("Half width", &r.noiseHalfWidth, 0.5f, 64.0f, "%.1f texels", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Max elevation", &r.maxElevation, 0.005f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Height above the water where rivers end (they fade out from half of it)");
		changed |= ImGui::SliderFloat("Warp", &r.noiseWarp, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("How strongly the channels bend");
		changed |= ImGui::InputInt("Seed##rivers", &r.noiseSeed);
	}
	if (changed) {
		RequestTerrain();
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
