#include "MapImportSys.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

#include "imgui.h"

#include "DemTools.h"
#include "FileDialog.h"

//What the lowest and the highest sample of a file without units (PNG, R16) mean, in the order shown in the UI.
//Files do not store this; a map exported by this app needs the range of the mode it came from. The last entry, "Custom (metres)", is not in the table
static const heightmap::ValueRange valueRanges[] = { heightmap::terrainRange, heightmap::noiseRange };
//Metres of height 1 in the mode each range comes from (TerrainGen shows height 1 as 1024 world units = 256 m, Noise heightmap as 256 = 64 m)
static const float rangeMetresPerUnit[] = { 256.0f, 64.0f };
static const char* valueRangeNames = "[0, 1] (TerrainGen export)\0[-1, 1] (Noise heightmap export)\0Custom (metres)\0";
static constexpr int customRange = 2;
static constexpr int downsampleChoices = 4;		//1, 2, 4, 8

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

int MapImportSys::Downsample() const
{
	return 1 << downsampleIndex;
}

void MapImportSys::Select(const std::filesystem::path& path)
{
	selected = path;
	std::string inspectError;
	inspected = heightmap::Inspect(path, header, inspectError);
	if (!inspected) {
		error = FileNameOf(path) + ": " + inspectError;
		return;
	}
	error.clear();
	//The whole file, reduced as little as needed to fit
	area[0] = area[1] = 0;
	area[2] = header.width;
	area[3] = header.height;
	const int fit = dem::FitDownsample(header.width, header.height);
	downsampleIndex = 0;
	while ((1 << downsampleIndex) < fit && downsampleIndex + 1 < downsampleChoices) {
		downsampleIndex++;
	}
	const heightmap::ImportInfo& info = header.info;
	filePixelMetres = info.pixelSize ? dem::PixelSizeMetres(*info.pixelSize, info.geographic, info.latitude) : glm::dvec2(1.0);
	pixelSize = static_cast<float>((filePixelMetres.x + filePixelMetres.y) / 2.0);
}

void MapImportSys::StartLoading(const std::filesystem::path& path)
{
	error.clear();
	heightmap::ImportOptions options;
	const bool custom = selectedRange == customRange;
	options.range = custom ? heightmap::ValueRange{ customLowest, customHighest } : valueRanges[selectedRange];
	if (inspected && path == selected) {
		options.crop = heightmap::PixelRect{ area[0], area[1], area[2], area[3] };
		options.downsample = Downsample();
	}
	const float rangeUnit = custom ? 0.0f : rangeMetresPerUnit[selectedRange];
	const double pixel = static_cast<double>(pixelSize) * options.downsample;
	job.Start([path, options, custom, rangeUnit, pixel]() {
		LoadResult result;
		result.path = path;
		auto start = std::chrono::steady_clock::now();
		//An exception would come back in future.get() on the main thread and end the app
		try {
			result.ok = heightmap::Import(path, options, result.map, result.info, result.error);
		}
		catch (const std::bad_alloc&) {
			result.ok = false;
			result.error = "Not enough memory to read the file: choose a smaller area";
		}
		catch (const std::exception& e) {
			result.ok = false;
			result.error = e.what();
		}
		if (result.ok) {
			if (result.info.metres || custom) {
				//Metres become about [-1, 1], the range the display modes and the culling of the view expect
				auto [lowest, highest] = std::minmax_element(result.map.Data(), result.map.Data() + result.map.Size());
				result.metresPerUnit = std::max({ std::abs(*lowest), std::abs(*highest), 1.0f });
				for (size_t i = 0; i < result.map.Size(); i++) {
					result.map[i] /= result.metresPerUnit;
				}
			}
			else {
				result.metresPerUnit = rangeUnit;
			}
			result.pixelMetres = glm::dvec2(pixel);
		}
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
	metresPerUnit = result.metresPerUnit;
	pixelMetres = result.pixelMetres;
	auto [lowest, highest] = std::minmax_element(map.Data(), map.Data() + map.Size());
	mapMin = *lowest;
	mapMax = *highest;
	//Height 1 = metresPerUnit metres; a texel = worldUnitsPerTexel world units = pixelMetres metres
	trueHeightScale = static_cast<float>(metresPerUnit * TerrainView::worldUnitsPerTexel / pixelMetres.x);
	renderSettings.heightScale = trueHeightScale;

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
		inspected = false;
	}
	listed = true;
}

void MapImportSys::BrowseForFile()
{
	std::string dialogError;
	const std::optional<std::filesystem::path> file = OpenFileDialog(
		L"Height maps (*.png, *.r16, *.raw, *.tif, *.tiff, *.hgt, *.asc)\0*.png;*.r16;*.raw;*.tif;*.tiff;*.hgt;*.asc\0", Folder(), dialogError);
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
	//Not loaded right away: a large file may need an area or a downsample first
	Select(*file);
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
		ImGui::TextWrapped("No height maps (PNG, R16, RAW, TIFF, HGT, ASC) in this folder.");
	}

	if (ImGui::BeginCombo("File", selected.empty() ? "[none]" : FileNameOf(selected).c_str())) {
		for (const std::filesystem::path& file : files) {
			bool isSelected = file == selected;
			if (ImGui::Selectable(FileNameOf(file).c_str(), isSelected) && !isSelected) {
				Select(file);
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::BeginDisabled(loading);
	const bool valid = OptionsImGui();
	ImGui::EndDisabled();

	ImGui::BeginDisabled(loading || selected.empty() || !valid);
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

bool MapImportSys::OptionsImGui()
{
	if (!inspected) {
		return false;
	}
	const heightmap::ImportInfo& info = header.info;
	ImGui::Text("%d x %d, %d-bit%s", header.width, header.height, info.bitsPerSample, info.floatingPoint ? " float" : "");
	if (info.geographic) {
		ImGui::TextWrapped("Geographic coordinates: no reprojection, pixels in metres at latitude %.2f", info.latitude);
	}
	const double longer = std::max(filePixelMetres.x, filePixelMetres.y);
	if (std::abs(filePixelMetres.x - filePixelMetres.y) > 0.01 * longer) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
		ImGui::TextWrapped("Pixels are not square: %.2f x %.2f m - the map is shown with their mean", filePixelMetres.x, filePixelMetres.y);
		ImGui::PopStyleColor();
	}

	bool valid = true;
	if (NeedsRange()) {
		//Changing the range of the loaded map reads its file again; the new range applies to it right away
		if (ImGui::Combo("Value range", &selectedRange, valueRangeNames) && mapPath == selected && selectedRange != customRange) {
			StartLoading(selected);
		}
		if (selectedRange == customRange) {
			ImGui::InputFloat("Lowest (m)", &customLowest, 0.0f, 0.0f, "%.1f");
			ImGui::InputFloat("Highest (m)", &customHighest, 0.0f, 0.0f, "%.1f");
			if (!(customHighest > customLowest)) {
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Highest must be above lowest");
				valid = false;
			}
		}
	}
	if (ImGui::InputFloat("Pixel size (m)", &pixelSize, 0.0f, 0.0f, "%.3f")) {
		pixelSize = std::max(pixelSize, 0.001f);
	}

	ImGui::InputInt4("Area x, y, w, h", area);
	ImGui::SameLine();
	if (ImGui::SmallButton("Whole file")) {
		area[0] = area[1] = 0;
		area[2] = header.width;
		area[3] = header.height;
	}
	ImGui::Combo("Downsample", &downsampleIndex, "1\0" "2\0" "4\0" "8\0");

	const bool areaInside = area[0] >= 0 && area[1] >= 0 && area[2] >= 1 && area[3] >= 1
		&& static_cast<long long>(area[0]) + area[2] <= header.width && static_cast<long long>(area[1]) + area[3] <= header.height;
	const int factor = Downsample();
	const int resultWidth = areaInside ? (area[2] + factor - 1) / factor : 0;
	const int resultHeight = areaInside ? (area[3] + factor - 1) / factor : 0;
	if (!areaInside) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "The area lies outside the file (%d x %d)", header.width, header.height);
		return false;
	}
	ImGui::Text("Result: %d x %d, pixel %.2f m", resultWidth, resultHeight, pixelSize * factor);
	if (resultWidth < 2 || resultHeight < 2 || resultWidth > dem::maxMapSize || resultHeight > dem::maxMapSize) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "The result must be 2 to %d on each side: choose a smaller area or a larger downsample", dem::maxMapSize);
		valid = false;
	}
	return valid;
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
	if (!map.Empty()) {
		ImGui::Text("Vertical exaggeration: x%.2f", s.heightScale / trueHeightScale);
		ImGui::SameLine();
		if (ImGui::Button("True proportions")) {
			s.heightScale = trueHeightScale;
		}
	}
	utilities::TerrainDetailImGui(s.tessPixels);
}

void MapImportSys::ImGuiOutput()
{
	if (map.Empty()) {
		ImGui::Text("No map loaded");
		return;
	}
	ImGui::Text("Map: %s (%dx%d, %d-bit%s%s), loaded in %.1f ms", FileNameOf(mapPath).c_str(), map.GetWidth(), map.GetHeight(),
		mapInfo.bitsPerSample, mapInfo.floatingPoint ? " float" : "", mapInfo.convertedFromColor ? ", color converted to grey" : "", lastLoadMs);
	ImGui::Text("Pixel: %.2f m%s", pixelMetres.x, mapInfo.geographic ? " (from degrees)" : "");
	ImGui::Text("Heights: %.1f to %.1f m", mapMin * metresPerUnit, mapMax * metresPerUnit);
	if (mapInfo.noDataFilled > 0) {
		ImGui::Text("NoData filled: %d", mapInfo.noDataFilled);
	}
}
