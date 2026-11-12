#include "MapImportSys.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "imgui.h"

#include "FileDialog.h"

//What the lowest and the highest sample of a file mean, in the order shown in the UI.
//Files do not store this; a map exported by this app needs the range of the mode it came from
static const heightmap::ValueRange valueRanges[] = { heightmap::terrainRange, heightmap::noiseRange };
static const char* valueRangeNames = "[0, 1] (TerrainGen export)\0[-1, 1] (Noise heightmap export)\0";

static std::string FileNameOf(const std::filesystem::path& path)
{
	const std::u8string text = path.filename().u8string();
	return std::string(text.begin(), text.end());
}

bool MapImportSys::Initialize(int _width, int _height, float _heightScale)
{
	renderSettings.heightScale = _heightScale;
	//Nothing is drawn until a map is loaded, the view only needs a valid starting size
	if (!view.Initialize(_width, _height)) {
		return false;
	}
	std::cout << "[LOG] MapImportSys initialized\n";
	return true;
}

void MapImportSys::Update()
{
	if (std::optional<LoadResult> result = job.TryTake()) {
		InstallResult(*result);
	}
}

void MapImportSys::StartLoading(const std::filesystem::path& path)
{
	error.clear();
	const heightmap::ValueRange range = valueRanges[selectedRange];
	job.Start([path, range]() {
		LoadResult result;
		result.path = path;
		auto start = std::chrono::steady_clock::now();
		result.ok = heightmap::Import(path, range, result.map, result.info, result.error);
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
}

void MapImportSys::InstallResult(LoadResult& result)
{
	if (!result.ok) {
		//The previously loaded map stays on screen
		error = FileNameOf(result.path) + ": " + result.error;
		return;
	}
	map = std::move(result.map);
	mapInfo = result.info;
	mapPath = result.path;
	lastLoadMs = result.ms;
	auto [lowest, highest] = std::minmax_element(map.Data(), map.Data() + map.Size());
	mapMin = *lowest;
	mapMax = *highest;

	view.Resize(map.GetWidth(), map.GetHeight());
	view.SetLayer(TerrainView::Layer::TERRAIN, map);
}

void MapImportSys::Draw(Renderer& renderer, Camera& camera, LightSource& light)
{
	view.Draw(renderer, camera, light, renderSettings);
}

std::filesystem::path MapImportSys::Folder() const
{
	//The text field holds UTF-8, a plain std::string would be read in the system code page
	const std::string text = folder;
	return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

void MapImportSys::RefreshFiles()
{
	files = heightmap::ListImportable(Folder());
	if (std::find(files.begin(), files.end(), selected) == files.end()) {
		selected.clear();
	}
	listed = true;
}

void MapImportSys::BrowseForFile()
{
	std::string dialogError;
	const std::optional<std::filesystem::path> file = OpenFileDialog(L"Height maps (*.png, *.r16, *.raw)\0*.png;*.r16;*.raw\0", Folder(), dialogError);
	if (!file) {
		error = dialogError;
		return;
	}

	//The list follows the chosen folder, unless its path does not fit the text field
	const std::u8string parent = file->parent_path().u8string();
	if (parent.size() < sizeof(folder)) {
		std::copy(parent.begin(), parent.end(), folder);
		folder[parent.size()] = '\0';
		RefreshFiles();
	}
	selected = *file;
	StartLoading(*file);
}

void MapImportSys::ImGuiRightPanel()
{
	const PanelTabs::Tab tabs[] = {
		{ "Load", [this] { LoadImGui(); } },
	};
	importTabs.Draw("import", tabs);
}

void MapImportSys::LoadImGui()
{
	if (!listed) {
		RefreshFiles();
	}
	const bool loading = job.IsRunning();

	ImGui::BeginDisabled(loading);
	if (ImGui::Button("Browse...")) {
		BrowseForFile();
	}
	ImGui::EndDisabled();

	if (ImGui::InputText("Folder", folder, sizeof(folder))) {
		RefreshFiles();
	}
	if (ImGui::Button("Refresh list")) {
		RefreshFiles();
	}
	if (files.empty()) {
		ImGui::TextWrapped("No PNG, R16 or RAW files in this folder.");
	}

	if (ImGui::BeginCombo("File", selected.empty() ? "[none]" : FileNameOf(selected).c_str())) {
		for (const std::filesystem::path& file : files) {
			bool isSelected = file == selected;
			if (ImGui::Selectable(FileNameOf(file).c_str(), isSelected)) {
				selected = file;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::BeginDisabled(loading);
	//Changing the range of a loaded map reads its file again; the new range applies to it right away
	if (ImGui::Combo("Value range", &selectedRange, valueRangeNames) && !mapPath.empty()) {
		StartLoading(mapPath);
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(loading || selected.empty());
	if (ImGui::Button("Load")) {
		StartLoading(selected);
	}
	ImGui::EndDisabled();
	if (loading) {
		ImGui::SameLine();
		ImGui::Text("Loading...");
	}

	if (!error.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		ImGui::TextWrapped("Error: %s", error.c_str());
		ImGui::PopStyleColor();
	}
}

void MapImportSys::ImGuiDisplay()
{
	TerrainRenderSettings& s = renderSettings;
	//No biomes to show and a file has no infinite generation
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, nullptr)) {
		if (s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::GREYSCALE;
		}
	}
}

void MapImportSys::ImGuiOutput()
{
	if (map.Empty()) {
		ImGui::Text("No map loaded");
		return;
	}
	ImGui::Text("Map: %s (%dx%d, %d-bit%s), loaded in %.1f ms", FileNameOf(mapPath).c_str(), map.GetWidth(), map.GetHeight(),
		mapInfo.bitsPerSample, mapInfo.convertedFromColor ? ", color converted to grey" : "", lastLoadMs);
	ImGui::Text("Values: %.4f to %.4f", mapMin, mapMax);
}
