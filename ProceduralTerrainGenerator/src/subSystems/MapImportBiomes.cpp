//Biomes of the working copy: the Biomes tab of MapImportSys and what it computes
#include "MapImportSys.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <new>
#include <utility>

#include "imgui.h"

#include "BiomeSettingsPanel.h"
#include "FileDialog.h"
#include "Preset.h"
#include "VegetationModels.h"

static std::string BiomeFileNameOf(const std::filesystem::path& path)
{
	const std::u8string text = path.filename().u8string();
	return std::string(text.begin(), text.end());
}

void MapImportSys::SetVegetationModels(VegetationModels* models)
{
	vegetationModels = models;
	if (!models) {
		vegetation.ClearExtraInstances();
		placedLayer.ClearExtraInstances();
	}
	std::vector<BiomeEditor::PlantModel> names;
	if (models) {
		for (const VegetationModels::Model& m : models->GetModels()) {
			names.push_back({ m.name, vegetation::LayerOfKind(m.kind) });
		}
	}
	biomeEditor.SetPlantModels(std::move(names));
}

std::string MapImportSys::UseBiomeSettings(const std::string& json, const std::filesystem::path& folderOfProject)
{
	mapbiomes::Settings previous = std::move(biomeSettings);
	biomeSettings = mapbiomes::Default(seaLevelMetres);
	std::string jsonError;
	if (!json.empty()) {
		//Replaced only on success, so a broken text leaves the defaults
		mapbiomes::FromJson(json, biomeSettings, jsonError, folderOfProject);
	}
	else {
		//A new map of the same region usually wants the same land cover
		biomeSettings.source = previous.source;
		biomeSettings.landCoverFiles = std::move(previous.landCoverFiles);
		biomeSettings.classes = std::move(previous.classes);
	}
	LandCoverInputChanged();
	biomeEditor.Reset();
	//The climate of the previous map must not color this one
	climateTexture.reset();
	biomesStale = false;
	ShowBiomesState();
	return jsonError;
}

void MapImportSys::UsePaint(biomepaint::Layer layer)
{
	biomePaint = std::move(layer);
	const bool fits = !map.Empty() && biomePaint.width == map.GetWidth() && biomePaint.height == map.GetHeight()
		&& biomePaint.texels.size() == map.Size();
	if (!fits) {
		biomePaint = biomepaint::Layer();
	}
	if (biomePaint.texels.empty()) {
		paintTexture.reset();
	}
	else {
		if (!paintTexture || paintTexture->GetWidth() != biomePaint.width || paintTexture->GetHeight() != biomePaint.height) {
			paintTexture = std::make_unique<TextureClass>(biomePaint.width, biomePaint.height, GL_RG8UI);
		}
		paintTexture->UploadRegion(0, 0, biomePaint.width, biomePaint.height, biomePaint.width, biomePaint.texels.data(), GL_RG_INTEGER, GL_UNSIGNED_BYTE);
	}
	biomesStale = true;
}

bool MapImportSys::RunBiomePass(glm::ivec4 region)
{
	const int width = map.GetWidth(), height = map.GetHeight();
	const TextureClass* heights = view.GetLayerTexture(TerrainView::Layer::TERRAIN);
	if (!BiomesOn() || map.Empty() || !climateTexture || !heights || climateTexture->GetWidth() != width || climateTexture->GetHeight() != height) {
		return false;
	}
	auto sameSize = [width, height](const TextureClass* t) { return t && t->GetWidth() == width && t->GetHeight() == height; };
	//A target made now holds nothing outside the rectangle: the whole map then
	if (!sameSize(biomeIds.get()) || !sameSize(view.GetLayerTexture(TerrainView::Layer::BIOMES)) || !sameSize(view.GetSplatTexture())) {
		region = glm::ivec4(0, 0, -1, -1);
	}
	if (!sameSize(biomeIds.get())) {
		biomeIds = std::make_unique<TextureClass>(width, height, GL_R32I);
	}
	MapBiomeInput input{ *heights, *climateTexture, metresPerUnit, glm::vec2(pixelMetres) };
	if (paintTexture) {
		input.paint = paintTexture.get();
		input.paintBiomes = biomepaint::SlotBiomes(biomePaint, biomeSettings.config.table);
	}
	if (LandCoverOn() && sameSize(classTexture.get())) {
		input.classes = classTexture.get();
		input.classMasks = mapbiomes::ClassMasks(biomeSettings);
	}
	input.region = region;
	biomeGenerator.GenerateMapBiomes(biomeSettings, seaLevelMetres, input, view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds,
		&view.SplatTexture(width, height));
	//New biome ids: the plants follow them (after a stroke, which runs this for every dab)
	biomeIdsVersion = mapVersion;
	PlantsChanged();
	return true;
}

void MapImportSys::UpdateBiomePaint(const Camera& camera, bool active, float deltaTime)
{
	const bool pressed = std::exchange(paintPressed, false);
	const bool canPaint = biomePaintOn && BiomesOn() && active && !map.Empty() && !camera.MouseControl();
	if (paintStroke) {
		//Let go, also outside the window or after losing the focus, or the mode or the biomes left
		if (!paintHeld || !canPaint) {
			FinishPaintStroke();
			return;
		}
	}
	else {
		if (!pressed || !canPaint || !hoverTexel || Busy()) {
			return;
		}
		const bool erase = paintInvert;
		if (erase && biomePaint.texels.empty()) {
			return;
		}
		const std::vector<biome::Biome>& table = biomeSettings.config.table;
		if (!erase && std::none_of(table.begin(), table.end(), [&](const biome::Biome& b) { return b.name == paintBiome; })) {
			biomeStatus = "Paint: choose a biome in the Paint section";
			biomeStatusIsError = true;
			return;
		}
		try {
			PaintStroke started;
			started.before = biomePaint;
			if (biomePaint.texels.empty()) {
				biomePaint = biomepaint::Make(map.GetWidth(), map.GetHeight());
			}
			if (!erase) {
				started.slot = biomepaint::SlotOf(biomePaint, paintBiome);
				if (started.slot == 0) {
					biomePaint = std::move(started.before);
					biomeStatus = "Paint: the palette is full (" + std::to_string(biomepaint::maxPalette) + " biomes)";
					biomeStatusIsError = true;
					return;
				}
			}
			paintStroke = std::move(started);
		}
		catch (const std::bad_alloc&) {
			biomeStatus = "Paint: not enough memory for the painted biomes";
			biomeStatusIsError = true;
			return;
		}
		if (!paintTexture) {
			UsePaint(std::move(biomePaint));
		}
	}
	//Cursor over the sky or off the map: this frame paints nothing
	if (!hoverTexel) {
		return;
	}
	biomepaint::Dab dab;
	dab.center = *hoverTexel;
	dab.radius = paintRadius;
	dab.hardness = paintHardness;
	dab.amount = paintRate * std::clamp(deltaTime, 0.0f, 0.1f);
	dab.slot = paintStroke->slot;
	dab.erase = paintInvert;
	dab.seed = ++paintSeed;
	const glm::ivec4 rect = biomepaint::Apply(biomePaint, dab);
	if (rect.z <= 0 || rect.w <= 0) {
		return;
	}
	paintStroke->painted = true;
	const size_t first = static_cast<size_t>(rect.y) * biomePaint.width + rect.x;
	paintTexture->UploadRegion(rect.x, rect.y, rect.z, rect.w, biomePaint.width, biomePaint.texels.data() + first, GL_RG_INTEGER, GL_UNSIGNED_BYTE);
	//Climate not ready (or of another size): the whole pass runs when it is
	if (!RunBiomePass(rect)) {
		biomesStale = true;
	}
}

void MapImportSys::FinishPaintStroke()
{
	PaintStroke finished = std::move(*paintStroke);
	paintStroke.reset();
	if (!finished.painted) {
		return;
	}
	const size_t stepBytes = finished.before.Bytes();
	if (history.Push({ "Paint biomes", std::nullopt, seaLevelMetres, std::move(finished.before) })) {
		historyNote.clear();
	}
	else {
		historyNote = "History off: one step needs " + std::to_string((stepBytes + (1 << 20) - 1) >> 20) + " MB, more than the history memory";
	}
	dirty = true;
	biomeStatus.clear();
}

void MapImportSys::PaintImGui()
{
	ImGui::SeparatorText("Paint");
	const std::vector<biome::Biome>& table = biomeSettings.config.table;
	if (ImGui::Checkbox("Paint biomes with the mouse", &biomePaintOn) && biomePaintOn) {
		sculptOn = false;
		plantBrushOn = false;
	}
	ImGui::SetItemTooltip("Left button paints the chosen biome over the automatic ones, in 3D and on the 2D map");
	if (biomePaintOn) {
		if (std::none_of(table.begin(), table.end(), [&](const biome::Biome& b) { return b.name == paintBiome; }) && !table.empty()) {
			paintBiome = table[0].name;
		}
		if (ImGui::BeginCombo("Biome##paint", paintBiome.c_str())) {
			for (size_t i = 0; i < table.size(); i++) {
				ImGui::PushID(static_cast<int>(i));
				const glm::vec3 c = table[i].color;
				ImGui::ColorButton("##color", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
				ImGui::SameLine();
				if (ImGui::Selectable(table[i].name.c_str(), table[i].name == paintBiome)) {
					paintBiome = table[i].name;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SliderFloat("Radius (px)##paint", &paintRadius, 1.0f, 512.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Hardness##paint", &paintHardness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Full strength up to this part of the radius, then a smooth fall to the edge");
		ImGui::SliderFloat("Speed##paint", &paintRate, 0.1f, 10.0f, "%.1f /s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Strength gained per second at the centre; full strength is 1");
		ImGui::TextDisabled("Left button: paint, Ctrl: erase, Shift + wheel: radius");
	}
	ImGui::BeginDisabled(Busy() || biomePaint.texels.empty());
	if (ImGui::Button("Clear painting")) {
		const size_t stepBytes = biomePaint.Bytes();
		if (history.Push({ "Clear painting", std::nullopt, seaLevelMetres, std::move(biomePaint) })) {
			historyNote.clear();
		}
		else {
			historyNote = "History off: one step needs " + std::to_string((stepBytes + (1 << 20) - 1) >> 20) + " MB, more than the history memory";
		}
		UsePaint(biomepaint::Layer());
		dirty = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("All biomes automatic again (Undo brings the painting back)");
	if (!biomePaint.palette.empty()) {
		const int missing = static_cast<int>(std::count_if(biomePaint.palette.begin(), biomePaint.palette.end(), [&](const std::string& name) {
			return std::none_of(table.begin(), table.end(), [&](const biome::Biome& b) { return b.name == name; });
		}));
		ImGui::TextDisabled("%d painted biome names, %d not in the table (automatic there)", static_cast<int>(biomePaint.palette.size()), missing);
	}
}

void MapImportSys::ShowBiomesState()
{
	utilities::heightMapMode& mode = renderSettings.displayMode;
	if (BiomesOn()) {
		if (mode != utilities::heightMapMode::TEXTURED && mode != utilities::heightMapMode::LAND_COVER) {
			mode = utilities::heightMapMode::BIOMES;
		}
		climateStale = true;
	}
	else {
		view.ReleaseSplat();
		if (mode == utilities::heightMapMode::BIOMES) {
			mode = utilities::heightMapMode::GREYSCALE;
		}
	}
}

void MapImportSys::UpdateBiomes()
{
	UpdateLandCover();
	if (std::optional<ClimateResult> result = climateJob.TryTake()) {
		if (!result->error.empty()) {
			//Not started again by itself: the next change of the map or the settings tries again
			biomeStatus = "Could not compute the climate: " + result->error;
			biomeStatusIsError = true;
		}
		else if (result->version != mapVersion) {
			//Computed for a map that is no longer shown
			climateStale = true;
		}
		else if (BiomesOn()) {
			const mapbiomes::ClimateMaps& maps = result->maps;
			if (!climateTexture || climateTexture->GetWidth() != maps.width || climateTexture->GetHeight() != maps.height) {
				climateTexture = std::make_unique<TextureClass>(maps.width, maps.height, GL_RG16F);
			}
			climateTexture->Upload(maps.packed.data(), GL_RG, GL_HALF_FLOAT);
			climateMs = result->ms;
			biomesStale = true;
		}
	}
	//A stroke changes the map only on the GPU; its end is a map change like any other
	if (!BiomesOn() || map.Empty() || stroke) {
		return;
	}
	if (climateStale && !climateJob.IsRunning()) {
		climateStale = false;
		try {
			climateJob.Start([metres = WorkingMetres(), pixel = pixelMetres, sea = seaLevelMetres, settings = biomeSettings, version = mapVersion]() {
				ClimateResult result;
				result.version = version;
				const auto start = std::chrono::steady_clock::now();
				//An exception would come back in future.get() on the main thread and end the app; a map of 8192² needs about 1.3 GB here
				try {
					result.maps = mapbiomes::ComputeClimate(metres, pixel, sea, settings);
				}
				catch (const std::exception& e) {
					result.maps = mapbiomes::ClimateMaps();
					result.error = e.what();
				}
				result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				return result;
			});
		}
		catch (const std::bad_alloc&) {
			biomeStatus = "Could not compute the climate: not enough memory";
			biomeStatusIsError = true;
		}
		catch (const std::exception& e) {
			biomeStatus = std::string("Could not compute the climate: ") + e.what();
			biomeStatusIsError = true;
		}
	}
	if (biomesStale && RunBiomePass(glm::ivec4(0, 0, -1, -1))) {
		biomesStale = false;
	}
}

bool MapImportSys::ClimateSourceImGui()
{
	mapbiomes::Settings& s = biomeSettings;
	ImGui::SeparatorText("Climate from terrain");
	ImGui::TextDisabled("Continentalness from the distance to the coast, mountainousness from the relief around each pixel");
	//The height unit and base change only the GPU pass (mapbiomes::ClimateHeight); continentalness and mountainousness stay
	bool heightChanged = ImGui::SliderFloat("Height of terrain 1 (m)", &s.heightUnitMetres, 16.0f, 10000.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Metres that make terrain height 1 for the climate and the elevation of the biomes (TerrainGen: 256 m)");
	heightChanged |= ImGui::InputFloat("Height 0 at (m)", &s.baseMetres, 1.0f, 10.0f, "%.1f");
	ImGui::SetItemTooltip("Metres of terrain height 0: the bottom of the deepest water of the biome table");
	ImGui::BeginDisabled(!seaLevelMetres);
	if (ImGui::Button("Fit to sea level")) {
		//The water of TerrainGen at its usual height above 0
		s.baseMetres = mapbiomes::Default(seaLevelMetres).baseMetres;
		heightChanged = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Puts height 0 below the sea level as deep as the water of TerrainGen lies");
	bool changed = false;
	float coastKm = s.coastMetres / 1000.0f;
	if (ImGui::SliderFloat("Coast distance (km)", &coastKm, 1.0f, 200.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
		s.coastMetres = coastKm * 1000.0f;
		changed = true;
	}
	ImGui::SetItemTooltip("Distance from the coast where continentalness reaches 1 inland (and -1 at sea)");
	changed |= ImGui::SliderFloat("Relief radius (m)", &s.reliefRadiusMetres, 10.0f, 10000.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Mountainousness compares the highest and the lowest point within this distance");
	changed |= ImGui::SliderFloat("Relief of mountains (m)", &s.reliefMetres, 10.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Difference of heights within the radius that makes mountainousness 1 (half of it gives 0)");
	if (!seaLevelMetres) {
		ImGui::TextDisabled("No sea level (Edit tab): continentalness is 1 everywhere");
	}
	climateStale = climateStale || changed;
	biomesStale = biomesStale || heightChanged;
	return changed || heightChanged;
}

void MapImportSys::BiomesImGui()
{
	if (map.Empty()) {
		ImGui::TextWrapped("Load a map or open a project first.");
		return;
	}
	mapbiomes::Settings& s = biomeSettings;
	BiomeConfig& config = s.config;
	if (ImGui::Checkbox("Biomes", &s.enabled)) {
		dirty = true;
		ShowBiomesState();
		//The biome brush ends with the biomes (a running stroke ends in the next Update)
		if (!s.enabled) {
			biomePaintOn = false;
		}
	}
	ImGui::SetItemTooltip("Biomes and surface materials of the map from its terrain; shown in the BIOMES and TEXTURED display modes and exported");
	if (!BiomesOn()) {
		return;
	}
	if (climateJob.IsRunning()) {
		ImGui::TextDisabled("Computing climate...");
	}
	else if (climateMs > 0.0) {
		ImGui::TextDisabled("Climate computed in %.0f ms", climateMs);
	}
	if (!biomeStatus.empty()) {
		ImGui::TextColored(biomeStatusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", biomeStatus.c_str());
	}

	bool climateChanged = ClimateSourceImGui();
	bool biomesChanged = false;
	float origin[2] = { s.climateOrigin.x, s.climateOrigin.y };
	if (ImGui::InputFloat2("Climate origin (m)", origin, "%.0f")) {
		s.climateOrigin = glm::vec2(origin[0], origin[1]);
		biomesChanged = true;
	}
	ImGui::SetItemTooltip("Position of the top-left pixel in the temperature and humidity noises");
	LandCoverImGui();

	ImGui::SeparatorText("Biomes");
	biomesChanged |= biomepanel::Blend(config);
	biomesChanged |= biomepanel::Climate(config.climate);
	biomesChanged |= biomepanel::Materials(config, true);

	ImGui::SeparatorText("Temperature and humidity");
	ImGui::RadioButton("Temperature", &biomeNoise, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Humidity", &biomeNoise, 1);
	ImGui::PushID(biomeNoise);
	biomesChanged |= utilities::NoiseImGui(biomeNoise == 0 ? config.temperature : config.humidity);
	ImGui::PopID();

	ImGui::SeparatorText("Biome table");
	if (!biomePresetsListed) {
		biomePresets = preset::List("presets");
		biomePresetsListed = true;
		biomePreset = 0;
	}
	if (biomePresets.empty()) {
		ImGui::TextDisabled("No presets in presets/ (saved in TerrainGen)");
	}
	else {
		biomePreset = std::min(biomePreset, static_cast<int>(biomePresets.size()) - 1);
		if (ImGui::BeginCombo("Preset", BiomeFileNameOf(biomePresets[biomePreset]).c_str())) {
			for (int i = 0; i < static_cast<int>(biomePresets.size()); i++) {
				if (ImGui::Selectable(BiomeFileNameOf(biomePresets[i]).c_str(), i == biomePreset)) {
					biomePreset = i;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Load biomes from preset")) {
			WorldConfig world;
			std::string loadError;
			if (preset::Load(biomePresets[biomePreset], world, loadError)) {
				config = world.biomes;
				biomeEditor.Reset();
				biomesChanged = true;
				biomeStatus = "Biomes of " + BiomeFileNameOf(biomePresets[biomePreset]);
				biomeStatusIsError = false;
			}
			else {
				biomeStatus = "Could not load " + BiomeFileNameOf(biomePresets[biomePreset]) + ": " + loadError;
				biomeStatusIsError = true;
			}
		}
		ImGui::SetItemTooltip("The biome table, climate, noises and materials of a TerrainGen preset");
		ImGui::SameLine();
	}
	if (ImGui::Button("Refresh##biomepresets")) {
		biomePresetsListed = false;
	}
	bool otherTableChanged = false;
	biomesChanged |= biomeEditor.ImGuiDraw(config.table, &otherTableChanged);
	PaintImGui();

	if (climateChanged || biomesChanged || otherTableChanged) {
		dirty = true;
	}
	if (biomesChanged) {
		biomesStale = true;
		dirty = true;
	}
}

void MapImportSys::SetGeoreference(std::optional<geo::GeoReference> reference)
{
	georeference = reference;
	dirty = true;
	LandCoverInputChanged();
}

void MapImportSys::LandCoverImGui()
{
	mapbiomes::Settings& s = biomeSettings;
	ImGui::SeparatorText("Biome source");
	int source = s.source == mapbiomes::Source::LAND_COVER ? 1 : 0;
	bool sourceChanged = ImGui::RadioButton("Climate from terrain", &source, 0);
	ImGui::SetItemTooltip("Every biome of the table can be anywhere; the climate decides");
	ImGui::SameLine();
	sourceChanged |= ImGui::RadioButton("Land cover", &source, 1);
	ImGui::SetItemTooltip("Classes of a land cover file (ESA WorldCover and the like) allow some biomes; the climate picks among them");
	if (sourceChanged) {
		s.source = source == 1 ? mapbiomes::Source::LAND_COVER : mapbiomes::Source::CLIMATE;
		dirty = true;
		biomesStale = true;
		//The class map is not kept with the climate source: read the files again
		if (!classTexture) {
			LandCoverInputChanged();
		}
	}
	if (!LandCoverOn()) {
		return;
	}

	ImGui::SeparatorText("Land cover");
	GeoreferenceImGui();
	ImGui::Spacing();
	//Files: the first one wins where two have a class
	int removed = -1;
	for (int i = 0; i < static_cast<int>(s.landCoverFiles.size()); i++) {
		ImGui::PushID(i);
		const std::string& path = s.landCoverFiles[i];
		const std::filesystem::path filePath(std::u8string(path.begin(), path.end()));
		if (ImGui::SmallButton("Remove")) {
			removed = i;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(BiomeFileNameOf(filePath).c_str());
		ImGui::SetItemTooltip("%s", path.c_str());
		//The status of the last class map, when it was computed for these files
		const landcover::FileStatus* status = i < static_cast<int>(landCoverFiles.size()) && landCoverFiles[i].path == path ? &landCoverFiles[i] : nullptr;
		if (status) {
			ImGui::Indent();
			if (!status->error.empty()) {
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", status->error.c_str());
			}
			else {
				const std::string system = status->system.empty() ? "" : status->system + ", ";
				if (!status->skipped.empty()) {
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%sskipped: %s", system.c_str(), status->skipped.c_str());
				}
				else if (status->stretched) {
					ImGui::TextDisabled("%sstretched over the map", system.c_str());
				}
				else {
					ImGui::TextDisabled("%scovers %.0f%% of the map", system.c_str(), 100.0f * status->coverage);
				}
			}
			ImGui::Unindent();
		}
		ImGui::PopID();
	}
	if (removed >= 0) {
		s.landCoverFiles.erase(s.landCoverFiles.begin() + removed);
		dirty = true;
		LandCoverInputChanged();
	}
	if (ImGui::Button("Add file...")) {
		std::string dialogError;
		const std::filesystem::path startFolder = s.landCoverFiles.empty() ? mapPath.parent_path()
			: std::filesystem::path(std::u8string(s.landCoverFiles.back().begin(), s.landCoverFiles.back().end())).parent_path();
		if (const std::optional<std::filesystem::path> file = OpenFileDialog(L"Land cover GeoTIFF (*.tif, *.tiff)\0*.tif;*.tiff\0", startFolder, dialogError)) {
			const std::u8string text = std::filesystem::absolute(*file).lexically_normal().u8string();
			const std::string added(text.begin(), text.end());
			if (std::find(s.landCoverFiles.begin(), s.landCoverFiles.end(), added) == s.landCoverFiles.end()) {
				s.landCoverFiles.push_back(added);
				dirty = true;
				LandCoverInputChanged();
			}
		}
		else if (!dialogError.empty()) {
			landCoverError = dialogError;
		}
	}
	ImGui::SetItemTooltip("A GeoTIFF of 8-bit class codes, e.g. a tile of ESA WorldCover; several tiles can cover the map together");
	ImGui::SameLine();
	if (ImGui::Button("Refresh##landcover")) {
		LandCoverInputChanged();
	}
	ImGui::SetItemTooltip("Reads the files again");
	if (landCoverJob.IsRunning() || landCoverStale) {
		ImGui::TextDisabled("Reading land cover...");
	}
	else if (landCoverMs > 0.0 && classTexture) {
		ImGui::TextDisabled("Land cover read in %.0f ms", landCoverMs);
	}
	if (!landCoverError.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", landCoverError.c_str());
	}
	uint64_t classified = 0;
	for (int code = 1; code < 256; code++) {
		classified += classCounts[code];
	}
	if (!landCoverJob.IsRunning() && !landCoverStale && (s.landCoverFiles.empty() || classified == 0)) {
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "No land cover on the map: biomes from the climate");
	}

	//Classes: what each allows
	bool classesChanged = false;
	const std::vector<biome::Biome>& table = s.config.table;
	const double mapPixels = std::max<double>(1.0, static_cast<double>(map.Size()));
	if (ImGui::BeginTable("landcoverclasses", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("Code");
		ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Map");
		ImGui::TableSetupColumn("Biomes");
		ImGui::TableHeadersRow();
		for (mapbiomes::LandCoverClass& c : s.classes) {
			ImGui::PushID(c.code);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			float color[3] = { c.color.r, c.color.g, c.color.b };
			if (ImGui::ColorEdit3("##color", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
				c.color = glm::vec3(color[0], color[1], color[2]);
				dirty = true;
			}
			ImGui::TableNextColumn();
			ImGui::Text("%d", c.code);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(c.name.c_str());
			ImGui::TableNextColumn();
			const uint64_t count = c.code >= 0 && c.code < 256 ? classCounts[c.code] : 0;
			if (count > 0) {
				ImGui::Text("%.1f%%", 100.0 * static_cast<double>(count) / mapPixels);
			}
			else {
				ImGui::TextDisabled("-");
			}
			ImGui::TableNextColumn();
			//Biomes of the class that are in the table; none = all
			int inTable = 0;
			for (const std::string& name : c.biomes) {
				inTable += std::any_of(table.begin(), table.end(), [&](const biome::Biome& b) { return b.name == name; }) ? 1 : 0;
			}
			const std::string label = inTable == 0 ? "all###biomes" : std::to_string(inTable) + "###biomes";
			if (ImGui::SmallButton(label.c_str())) {
				ImGui::OpenPopup("classbiomes");
			}
			ImGui::SetItemTooltip(inTable == 0 ? "No biome of the table chosen: all are allowed" : "Biomes the class allows");
			if (ImGui::BeginPopup("classbiomes")) {
				ImGui::TextDisabled("Biomes of %s (none = all)", c.name.c_str());
				for (const biome::Biome& b : table) {
					auto found = std::find(c.biomes.begin(), c.biomes.end(), b.name);
					bool on = found != c.biomes.end();
					if (ImGui::Checkbox(b.name.c_str(), &on)) {
						if (on) {
							c.biomes.push_back(b.name);
						}
						else {
							c.biomes.erase(found);
						}
						classesChanged = true;
					}
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (ImGui::Button("WorldCover defaults")) {
		mapbiomes::Settings defaults = s;
		defaults.classes = mapbiomes::WorldCoverClasses();
		mapbiomes::AddMissingClasses(defaults, classCounts);
		//Classes edited by hand are not thrown away without asking
		if (s.classes.empty() || s.classes == defaults.classes) {
			classesChanged = s.classes != defaults.classes;
			s.classes = std::move(defaults.classes);
		}
		else {
			ImGui::OpenPopup("Replace classes");
		}
	}
	ImGui::SetItemTooltip("Names, colors and biomes of the ESA WorldCover legend for the default biome table");
	if (ImGui::BeginPopup("Replace classes")) {
		ImGui::TextUnformatted("Replace the classes with the WorldCover defaults?");
		if (ImGui::Button("Replace")) {
			s.classes = mapbiomes::WorldCoverClasses();
			mapbiomes::AddMissingClasses(s, classCounts);
			classesChanged = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (classesChanged) {
		//The class map stays; only the GPU pass again
		biomesStale = true;
		dirty = true;
	}
}

void MapImportSys::UpdateLandCover()
{
	if (std::optional<LandCoverResult> result = landCoverJob.TryTake()) {
		if (result->mapId != landCoverMapId || result->filesVersion != landCoverFilesVersion) {
			//For another map, other files or another georeference: landCoverStale is already set
		}
		else if (!result->error.empty()) {
			landCoverError = "Could not read the land cover: " + result->error;
			classTexture.reset();
			biomesStale = true;
		}
		else {
			landcover::ClassMap& classes = result->map;
			landCoverFiles = std::move(classes.files);
			classCounts = classes.counts;
			landCoverMs = result->ms;
			landCoverError.clear();
			if (!classTexture || classTexture->GetWidth() != classes.width || classTexture->GetHeight() != classes.height) {
				classTexture = std::make_unique<TextureClass>(classes.width, classes.height, GL_R8UI);
				//An integer texture with linear filtering is incomplete: texelFetch in the LAND COVER mode would read 0
				glTextureParameteri(classTexture->GetID(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTextureParameteri(classTexture->GetID(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
			//Rows of an odd width are not 4-byte aligned: UploadRegion sets the alignment to 1
			classTexture->UploadRegion(0, 0, classes.width, classes.height, classes.width, classes.classes.data(), GL_RED_INTEGER, GL_UNSIGNED_BYTE);
			//Not a change of the project: the classes come back from the files every time (opening a project asks nothing on close)
			mapbiomes::AddMissingClasses(biomeSettings, classCounts);
			biomesStale = true;
		}
	}
	//Only with the land cover source; the files stay unread until it is chosen. A stroke changes no class
	if (!LandCoverOn() || map.Empty() || !landCoverStale || landCoverJob.IsRunning()) {
		return;
	}
	landCoverStale = false;
	if (biomeSettings.landCoverFiles.empty()) {
		classTexture.reset();
		landCoverFiles.clear();
		classCounts.fill(0);
		landCoverError.clear();
		biomesStale = true;
		return;
	}
	std::vector<std::filesystem::path> paths;
	for (const std::string& file : biomeSettings.landCoverFiles) {
		paths.emplace_back(std::u8string(file.begin(), file.end()));
	}
	landCoverJob.Start([paths = std::move(paths), width = map.GetWidth(), height = map.GetHeight(), reference = georeference,
		mapId = landCoverMapId, filesVersion = landCoverFilesVersion]() {
		LandCoverResult result;
		result.mapId = mapId;
		result.filesVersion = filesVersion;
		const auto start = std::chrono::steady_clock::now();
		//An exception would come back in future.get() on the main thread and end the app
		try {
			result.map = landcover::Compute(paths, width, height, reference);
		}
		catch (const std::exception& e) {
			result.map = landcover::ClassMap();
			result.error = e.what();
		}
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
}

void MapImportSys::GeoreferenceImGui()
{
	if (georeference) {
		const glm::dvec2 corner = georeference->origin;
		ImGui::TextWrapped("Top left corner of the map: %.6f, %.6f", corner.x, corner.y);
		int epsg = georeference->epsg;
		if (utilities::SystemComboImGui("Map coordinate system", epsg)) {
			geo::GeoReference changed = *georeference;
			changed.epsg = epsg;
			SetGeoreference(changed);
		}
		if (!geo::Supported(epsg)) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "System not supported: land cover is stretched over the map");
		}
	}
	else {
		ImGui::TextWrapped("No georeference: land cover is stretched over the map");
	}
	if (ImGui::Button("Take georeference from file...")) {
		std::string dialogError;
		const std::optional<std::filesystem::path> file = OpenFileDialog(
			L"Maps with coordinates (*.tif, *.tiff, *.hgt, *.asc)\0*.tif;*.tiff;*.hgt;*.asc\0", mapPath.parent_path(), dialogError);
		heightmap::FileHeader fileHeader;
		std::string inspectError;
		if (!file) {
			geoStatus = dialogError;
			geoStatusError = !dialogError.empty();
		}
		else if (!heightmap::Inspect(*file, fileHeader, inspectError)) {
			geoStatus = BiomeFileNameOf(*file) + ": " + inspectError;
			geoStatusError = true;
		}
		else if (!fileHeader.info.georeference) {
			geoStatus = BiomeFileNameOf(*file) + " has no georeference";
			geoStatusError = true;
		}
		else {
			geoFile = *file;
			geoFileHeader = fileHeader;
			geoCrop[0] = geoCrop[1] = 0;
			geoDownsample = 1;
			geoStatus.clear();
		}
	}
	if (!geoFile.empty()) {
		ImGui::Text("%s: %d x %d, %s", BiomeFileNameOf(geoFile).c_str(), geoFileHeader.width, geoFileHeader.height,
			geo::SystemName(geoFileHeader.info.georeference->epsg).c_str());
		ImGui::TextDisabled("The area of the file the map was made of (Load: area x, y and downsample)");
		ImGui::InputInt2("Area x, y", geoCrop);
		if (ImGui::InputInt("Downsample", &geoDownsample)) {
			geoDownsample = std::clamp(geoDownsample, 1, 64);
		}
		//The map covers map size x downsample pixels of the file from the corner of the area
		const long long right = geoCrop[0] + static_cast<long long>(map.GetWidth()) * geoDownsample;
		const long long bottom = geoCrop[1] + static_cast<long long>(map.GetHeight()) * geoDownsample;
		if (ImGui::Button("Use")) {
			if (geoCrop[0] < 0 || geoCrop[1] < 0 || right > geoFileHeader.width || bottom > geoFileHeader.height) {
				geoStatus = "The map (" + std::to_string(map.GetWidth()) + " x " + std::to_string(map.GetHeight()) + ", downsample "
					+ std::to_string(geoDownsample) + ") does not fit in the file from this corner";
				geoStatusError = true;
			}
			else {
				geo::GeoReference taken = geo::Cropped(*geoFileHeader.info.georeference, geoCrop[0], geoCrop[1], geoDownsample);
				//Coordinates without a named system keep the system chosen for the map
				if (taken.epsg == 0 && georeference) {
					taken.epsg = georeference->epsg;
				}
				SetGeoreference(taken);
				geoStatus = "Georeference taken from " + BiomeFileNameOf(geoFile);
				geoStatusError = false;
				geoFile.clear();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			geoFile.clear();
		}
	}
	if (!geoStatus.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, geoStatusError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
		ImGui::TextWrapped("%s", geoStatus.c_str());
		ImGui::PopStyleColor();
	}
}
