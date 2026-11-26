//Plants of the working copy (stage D4): the Vegetation section of the Biomes tab of MapImportSys and their placement
#include "MapImportSys.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <exception>
#include <utility>

#include "imgui.h"

#include "VegetationModels.h"

MapImportSys::~MapImportSys()
{
	//The members (plantJob among them) are destroyed after this; its future waits only until the placement sees the flag
	if (plantCancel) {
		*plantCancel = true;
	}
}

int MapImportSys::ReadyModels() const
{
	if (!vegetationModels) {
		return 0;
	}
	int ready = 0;
	for (const VegetationModels::Model& m : vegetationModels->GetModels()) {
		ready += m.state == VegetationModels::State::READY && !m.variants.empty() ? 1 : 0;
	}
	return ready;
}

std::vector<vegexport::Model> MapImportSys::PlantModels() const
{
	std::vector<vegexport::Model> models;
	if (vegetationModels) {
		for (const VegetationModels::Model& m : vegetationModels->GetModels()) {
			const bool ready = m.state == VegetationModels::State::READY && !m.variants.empty();
			models.push_back({ m.name, m.kind, m.asset, ready ? static_cast<int>(m.variants.size()) : 0, vegetationModels->SourceFile(m), m.lodTargets,
				vegetationModels->CacheFile(m) });
		}
	}
	return models;
}

glm::vec2 MapImportSys::PlantTexelZero() const
{
	//As the water of the rivers
	return (-glm::vec2(view.GetMapWidth(), view.GetMapHeight()) * 0.5f + 0.5f) * TerrainView::worldUnitsPerTexel;
}

vegexport::Input MapImportSys::PlantInput() const
{
	vegexport::Input input;
	const mapbiomes::Settings& s = biomeSettings;
	for (const biome::Biome& b : s.config.table) {
		input.plants.push_back(b.plants);
	}
	input.models = PlantModels();
	const float pixel = static_cast<float>(pixelMetres.x);
	input.metresPerUnit = metresPerUnit;
	input.metresPerTexel = pixel;
	//The cells lie where the climate noises put the map, so a map sent from TerrainGen gets the trees TerrainGen has there
	input.mapTexel = glm::ivec2(glm::round(s.climateOrigin));
	input.limit = vegexport::maxMapInstances;
	//Nothing stands in the sea; without one nothing is water
	input.settings.waterHeight = seaLevelMetres ? *seaLevelMetres / metresPerUnit : -FLT_MAX;
	//Elevation of the plants as the biomes of the map see it: (metres - base) / height unit over the water height of the biomes
	input.settings.elevationScale = metresPerUnit / s.heightUnitMetres;
	input.settings.elevationOffset = -s.baseMetres / s.heightUnitMetres;
	input.settings.elevationWater = mapbiomes::WaterHeight(s, seaLevelMetres);
	input.settings.density = plantSettings.density;
	input.settings.seed = static_cast<uint32_t>(s.config.detailSeed);
	input.lodRanges = vegetation.LodRanges();
	//Rivers and lakes: network positions are map texels, levels metres / mapMetresPerHeight
	if (RiversOn() && riverNetwork && riverNetwork->error.empty()) {
		//Shares the network (aliasing pointer): an 8192² map has a network of gigabytes
		input.rivers = std::shared_ptr<const hydrology::Network>(riverNetwork, &riverNetwork->network);
		input.settings.riverOrigin = glm::vec2(input.mapTexel);
		input.settings.riverPositionScale = 1.0f / pixel;
		input.settings.riverHeightScale = hydrology::mapMetresPerHeight / metresPerUnit;
	}
	return input;
}

void MapImportSys::UpdatePlants()
{
	if (std::optional<PlantResult> result = plantJob.TryTake()) {
		plantCancel.reset();
		if (result->version != plantVersion || !PlantsOn()) {
			//Placed (or failed) for an older state: dropped; plantsStale is set then, a new one starts below
		}
		else if (!result->error.empty()) {
			vegetation.ClearExtraInstances();
			plantCount = 0;
			plantStatus = "Could not place the plants: " + result->error;
			plantStatusIsError = true;
		}
		else {
			vegetation.SetExtraInstances(result->instances);
			plantKept = result->kept;
			plantCount = result->count;
			plantMs = result->ms;
			plantStatus.clear();
			plantStatusIsError = false;
		}
	}
	const int ready = ReadyModels();
	//The plants put by hand, whatever the automatic ones do
	if (placedUploadStale || ready != placedUploadModels) {
		UploadPlacedPlants();
	}
	//Pixels below 1 cm get no plants (vegexport::minPlantPixelMetres): the Vegetation tab says so
	if (!PlantsOn() || map.Empty() || ready == 0 || pixelMetres.x < vegexport::minPlantPixelMetres) {
		if (vegetation.HasExtraInstances()) {
			vegetation.ClearExtraInstances();
		}
		plantCount = 0;
		plantKept = vegexport::Kept::ALL;
		//Placed again when they come back
		plantsStale = true;
		return;
	}
	//Plants of the table (the biome editor does not report them as a change of the biomes) or models that finished loading
	const std::vector<biome::Biome>& table = biomeSettings.config.table;
	bool samePlants = placedTablePlants.size() == table.size() && ready == placedReadyModels;
	for (size_t i = 0; samePlants && i < table.size(); i++) {
		samePlants = placedTablePlants[i] == table[i].plants;
	}
	if (!samePlants && !plantsStale) {
		PlantsChanged();
	}

	//Only from a finished state: no stroke, biomes, land cover and rivers of this map computed
	if (!plantsStale || plantJob.IsRunning() || Busy() || climateStale || biomesStale || climateJob.IsRunning()) {
		return;
	}
	if (LandCoverOn() && (landCoverStale || landCoverJob.IsRunning())) {
		return;
	}
	if (RiversOn() && (mapChanged || riverJob.IsRunning())) {
		return;
	}
	const int width = map.GetWidth(), height = map.GetHeight();
	if (!biomeIds || biomeIds->GetWidth() != width || biomeIds->GetHeight() != height || biomeIdsVersion != mapVersion) {
		//The pass of the biomes for these heights has not run yet (or the climate failed); it marks the plants out of date when it does
		return;
	}
	plantsStale = false;
	placedTablePlants.clear();
	for (const biome::Biome& b : table) {
		placedTablePlants.push_back(b.plants);
	}
	placedReadyModels = ready;

	BiomeMap ids(width, height);
	biomeIds->Read(GL_RED_INTEGER, GL_INT, static_cast<int>(ids.Size() * sizeof(int)), ids.Data());
	const glm::vec2 texelZero = PlantTexelZero();
	vegexport::Input input = PlantInput();
	plantCancel = std::make_shared<std::atomic<bool>>(false);
	input.cancel = plantCancel;
	plantJob.Start([units = map, ids = std::move(ids), input = std::move(input), texelZero, version = plantVersion]() {
		PlantResult result;
		result.version = version;
		const auto start = std::chrono::steady_clock::now();
		//An exception would come back in future.get() on the main thread and end the app
		try {
			vegexport::Limited limited = vegexport::PlaceMapLimited(units, {}, ids, input);
			result.kept = limited.kept;
			result.count = limited.placed.size();
			result.instances = vegexport::ToAppInstances(limited.placed, texelZero, input.metresPerUnit, input.metresPerTexel);
			//In the order SetExtraInstances culls them by, so the frame taking the result skips sorting 2 million plants
			vegetation::SortIntoSquares(result.instances, VegetationLayer::ExtraSquare());
		}
		catch (const std::bad_alloc&) {
			result.instances.clear();
			result.error = "not enough memory";
		}
		catch (const std::exception& e) {
			result.instances.clear();
			result.error = e.what();
		}
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
}

void MapImportSys::UploadPlacedPlants()
{
	placedUploadStale = false;
	placedUploadModels = ReadyModels();
	if (placedPlants.plants.empty() || map.Empty() || placedUploadModels == 0) {
		placedLayer.ClearExtraInstances();
		return;
	}
	const float pixel = static_cast<float>(pixelMetres.x);
	const std::vector<vegexport::Placed> placed = placedplants::ToPlaced(placedPlants, map, metresPerUnit, pixel, PlantModels());
	placedLayer.SetExtraInstances(vegexport::ToAppInstances(placed, PlantTexelZero(), metresPerUnit, pixel));
}

void MapImportSys::UpdatePlantBrush(const Camera& camera, bool active)
{
	const bool pressed = std::exchange(paintPressed, false);
	const bool canPlace = plantBrushOn && active && !map.Empty() && !camera.MouseControl();
	if (plantStroke) {
		//Let go, also outside the window or after losing the focus, or the brush turned off
		if (!paintHeld || !canPlace) {
			FinishPlantStroke();
			return;
		}
	}
	else {
		if (!pressed || !canPlace || !hoverTexel || Busy()) {
			return;
		}
		PlantStroke started;
		started.erase = paintInvert;
		if (started.erase && placedPlants.plants.empty()) {
			return;
		}
		if (!started.erase) {
			//Ticked models that are loaded now
			for (const vegexport::Model& m : PlantModels()) {
				if (m.variants > 0 && std::find(brushModels.begin(), brushModels.end(), m.name) != brushModels.end()) {
					started.brush.models.push_back(m.name);
					started.brush.variants.push_back(m.variants);
				}
			}
			if (started.brush.models.empty()) {
				plantBrushStatus = "Tick at least one loaded model of the brush";
				return;
			}
			started.brush.count = plantBrushCount;
			started.brush.radius = plantRadius;
			started.brush.spacing = plantSpacing;
			started.brush.scaleMin = std::min(plantScaleMin, plantScaleMax);
			started.brush.scaleMax = std::max(plantScaleMin, plantScaleMax);
		}
		plantBrushStatus.clear();
		started.before = placedPlants;
		plantStroke = std::move(started);
	}
	//Cursor over the sky or off the map: this frame places nothing
	if (!hoverTexel) {
		return;
	}
	PlantStroke& s = *plantStroke;
	const float pixel = static_cast<float>(pixelMetres.x);
	const glm::vec2 at = *hoverTexel * pixel;
	const float step = std::max({ plantSpacing, plantRadius * 0.5f, pixel });
	if (s.dabbed && glm::distance(at, s.lastDab) < step) {
		return;
	}
	s.dabbed = true;
	s.lastDab = at;
	size_t changed = 0;
	if (s.erase) {
		changed = placedplants::Erase(placedPlants, at, plantRadius);
	}
	else {
		s.brush.centre = at;
		s.brush.seed = ++plantSeed;
		changed = placedplants::Scatter(placedPlants, s.brush, glm::vec2(map.GetWidth() - 1, map.GetHeight() - 1) * pixel);
	}
	if (changed > 0) {
		s.changed = true;
		PlacedPlantsChanged();
	}
}

void MapImportSys::FinishPlantStroke()
{
	PlantStroke finished = std::move(*plantStroke);
	plantStroke.reset();
	if (!finished.changed) {
		return;
	}
	const size_t stepBytes = finished.before.Bytes();
	if (history.Push({ finished.erase ? "Erase plants" : "Place plants", std::nullopt, seaLevelMetres, std::nullopt, std::move(finished.before) })) {
		historyNote.clear();
	}
	else {
		historyNote = "History off: one step needs " + std::to_string((stepBytes + (1 << 20) - 1) >> 20) + " MB, more than the history memory";
	}
}

void MapImportSys::VegetationImGui()
{
	if (map.Empty()) {
		ImGui::TextDisabled("Load a map first (Load tab).");
		return;
	}
	PlantsImGui();

	ImGui::SeparatorText("Plant brush");
	if (ImGui::Checkbox("Place plants with the mouse", &plantBrushOn) && plantBrushOn) {
		sculptOn = false;
		biomePaintOn = false;
	}
	ImGui::SetItemTooltip("Left button puts the ticked models on the terrain: one at the cursor, or several spread over the circle");
	if (plantBrushOn) {
		if (ReadyModels() == 0) {
			ImGui::TextDisabled("No vegetation models loaded (python tools/fetch_models.py).");
		}
		else {
			ImGui::TextUnformatted("Models of the brush");
			const float rows = std::min(8.0f, static_cast<float>(ReadyModels()));
			if (ImGui::BeginChild("brushModels", ImVec2(0.0f, rows * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f),
				ImGuiChildFlags_Borders)) {
				for (const vegexport::Model& m : PlantModels()) {
					if (m.variants == 0) {
						continue;
					}
					const auto found = std::find(brushModels.begin(), brushModels.end(), m.name);
					bool on = found != brushModels.end();
					if (ImGui::Checkbox(m.name.c_str(), &on)) {
						if (on) {
							brushModels.push_back(m.name);
						}
						else {
							brushModels.erase(found);
						}
					}
					ImGui::SameLine();
					ImGui::TextDisabled("(%s)", m.kind.c_str());
				}
			}
			ImGui::EndChild();
		}
		ImGui::SliderInt("Count##plantBrush", &plantBrushCount, 1, 50, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("1 = one plant exactly at the cursor; more = that many tries spread over the circle, each dab");
		ImGui::SliderFloat("Radius (m)##plantBrush", &plantRadius, 0.5f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Spacing (m)##plantBrush", &plantSpacing, 0.0f, 50.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("No plant closer than this to another placed plant");
		ImGui::DragFloatRange2("Scale##plantBrush", &plantScaleMin, &plantScaleMax, 0.01f, 0.25f, 4.0f, "%.2f", "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Size of each plant, chosen between these, times the size of the model");
		ImGui::TextDisabled("Left button: place, Ctrl: erase, Shift + wheel: radius");
		if (!plantBrushStatus.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", plantBrushStatus.c_str());
		}
	}
	ImGui::BeginDisabled(Busy() || placedPlants.plants.empty());
	if (ImGui::Button("Clear placed plants")) {
		const size_t stepBytes = placedPlants.Bytes();
		if (history.Push({ "Clear placed plants", std::nullopt, seaLevelMetres, std::nullopt, std::move(placedPlants) })) {
			historyNote.clear();
		}
		else {
			historyNote = "History off: one step needs " + std::to_string((stepBytes + (1 << 20) - 1) >> 20) + " MB, more than the history memory";
		}
		placedPlants = placedplants::Layer();
		PlacedPlantsChanged();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Removes every plant put by hand (Undo brings them back)");
	ImGui::SameLine();
	ImGui::TextDisabled("Placed by hand: %zu", placedPlants.plants.size());

	//Both layers draw with these (placedLayer copies them every frame); the statistics are of the automatic plants
	if (ImGui::TreeNode("Drawing##plants")) {
		vegetation.ImGuiViewSettings();
		ImGui::TreePop();
	}

	ImGui::SeparatorText("Models");
	if (vegetationModels && lastLight) {
		vegetationModels->ImGuiDraw(*lastLight);
	}
	else {
		ImGui::TextDisabled("No vegetation models.");
	}
}

void MapImportSys::PlantsImGui()
{
	ImGui::SeparatorText("Automatic plants");
	if (ImGui::Checkbox("Plants", &plantSettings.enabled)) {
		dirty = true;
		PlantsChanged();
	}
	ImGui::SetItemTooltip("Trees, shrubs and rocks of the biomes (their plants in the biome editor), in the true size of the map pixel");
	if (!plantSettings.enabled) {
		return;
	}
	if (!BiomesOn()) {
		ImGui::TextDisabled("They grow from the biomes: turn on Biomes in the Biomes tab.");
		return;
	}
	if (ImGui::SliderFloat("Density##plants", &plantSettings.density, 0.0f, mapproject::maxVegetationDensity, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
		dirty = true;
	}
	//Placed again when the slider is let go, not for every value on the way
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		PlantsChanged();
	}
	ImGui::SetItemTooltip("Multiplies the chance of every species; at most %.0f million plants (above that only trees, then none)",
		static_cast<double>(vegexport::maxMapInstances) / 1e6);
	if (ReadyModels() == 0) {
		ImGui::TextDisabled("No vegetation models loaded (python tools/fetch_models.py).");
		return;
	}
	if (!map.Empty() && pixelMetres.x < vegexport::minPlantPixelMetres) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No automatic plants: pixels below 1 cm");
		return;
	}
	if (!plantStatus.empty()) {
		ImGui::TextColored(plantStatusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", plantStatus.c_str());
	}
	if (plantJob.IsRunning() || plantsStale) {
		ImGui::TextDisabled("Placing plants...");
	}
	else if (plantKept == vegexport::Kept::NONE) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Over 2 million trees: no plants. Crop the map or lower the density.");
	}
	else {
		ImGui::TextDisabled("Plants: %zu (%.1f s)", plantCount, plantMs / 1000.0);
		if (plantKept == vegexport::Kept::TREES_ONLY) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Over 2 million plants: trees only");
		}
	}
}
