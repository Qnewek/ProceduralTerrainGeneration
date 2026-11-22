//Biomes of the working copy: the Biomes tab of MapImportSys and what it computes
#include "MapImportSys.h"

#include <chrono>
#include <cstdio>
#include <exception>

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
	const int width = map.GetWidth(), height = map.GetHeight();
	const TextureClass* heights = view.GetLayerTexture(TerrainView::Layer::TERRAIN);
	if (!biomesStale || !climateTexture || !heights || climateTexture->GetWidth() != width || climateTexture->GetHeight() != height) {
		return;
	}
	biomesStale = false;
	if (!biomeIds || biomeIds->GetWidth() != width || biomeIds->GetHeight() != height) {
		biomeIds = std::make_unique<TextureClass>(width, height, GL_R32I);
	}
	const MapBiomeInput input{ *heights, *climateTexture, metresPerUnit, glm::vec2(pixelMetres) };
	biomeGenerator.GenerateMapBiomes(biomeSettings, seaLevelMetres, input, view.LayerTexture(TerrainView::Layer::BIOMES, width, height), *biomeIds,
		&view.SplatTexture(width, height));
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

	if (climateChanged || biomesChanged) {
		dirty = true;
	}
	if (biomesChanged) {
		biomesStale = true;
		dirty = true;
	}
}
