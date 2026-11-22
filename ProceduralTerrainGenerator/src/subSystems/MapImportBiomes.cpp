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
#include "Preset.h"
#include "VegetationModels.h"

static std::string BiomeFileNameOf(const std::filesystem::path& path)
{
	const std::u8string text = path.filename().u8string();
	return std::string(text.begin(), text.end());
}

void MapImportSys::SetVegetationModels(const VegetationModels* models)
{
	std::vector<BiomeEditor::PlantModel> names;
	if (models) {
		for (const VegetationModels::Model& m : models->GetModels()) {
			names.push_back({ m.name, vegetation::LayerOfKind(m.kind) });
		}
	}
	biomeEditor.SetPlantModels(std::move(names));
}

std::string MapImportSys::UseBiomeSettings(const std::string& json)
{
	biomeSettings = mapbiomes::Default(seaLevelMetres);
	std::string jsonError;
	if (!json.empty()) {
		//Replaced only on success, so a broken text leaves the defaults
		mapbiomes::FromJson(json, biomeSettings, jsonError);
	}
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
	input.region = region;
	biomeGenerator.GenerateMapBiomes(biomeSettings, seaLevelMetres, input, view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds,
		&view.SplatTexture(width, height));
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
		if (mode != utilities::heightMapMode::TEXTURED) {
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
	biomesChanged |= biomeEditor.ImGuiDraw(config.table);
	PaintImGui();

	if (climateChanged || biomesChanged) {
		dirty = true;
	}
	if (biomesChanged) {
		biomesStale = true;
		dirty = true;
	}
}
