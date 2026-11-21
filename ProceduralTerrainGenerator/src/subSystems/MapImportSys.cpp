#include "MapImportSys.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>

#include "imgui.h"

#include "DemTools.h"
#include "FileDialog.h"
#include "Preset.h"

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
	if (!erosion.Initialize()) {
		std::cout << "[ERROR] MapImportSys: erosion shaders could not be built\n";
		return false;
	}
	std::cout << "[LOG] MapImportSys initialized\n";
	return true;
}

void MapImportSys::Update(Camera& camera, bool active)
{
	if (std::optional<LoadResult> result = job.TryTake()) {
		InstallResult(*result);
	}
	if (std::optional<ProjectLoad> result = projectJob.TryTake()) {
		InstallProject(*result);
	}
	if (std::optional<SaveResult> result = saveJob.TryTake()) {
		FinishSaving(*result);
	}
	if (std::optional<EditResult> result = editJob.TryTake()) {
		FinishEdit(*result);
	}
	if (erodeRequested) {
		erodeRequested = false;
		Erode();
	}
	UpdateRivers();
	//Meshes of an export are written in the background; the Saving tab may be closed
	exporter.Update();
	if (lookDown && active && !map.Empty()) {
		lookDown = false;
		TerrainView::LookDownAtMap(camera, renderSettings, glm::ivec2(0), glm::ivec2(map.GetWidth(), map.GetHeight()));
	}
}

float MapImportSys::ToUnits(HeightField& working, HeightField& original)
{
	float largest = 1.0f;
	for (const HeightField* m : { &working, &original }) {
		for (size_t i = 0; i < m->Size(); i++) {
			largest = std::max(largest, std::abs((*m)[i]));
		}
	}
	for (HeightField* m : { &working, &original }) {
		for (size_t i = 0; i < m->Size(); i++) {
			(*m)[i] /= largest;
		}
	}
	return largest;
}

void MapImportSys::InstallMap(HeightField units, float _metresPerUnit, glm::dvec2 _pixelMetres)
{
	//The network of the previous map would be drawn over the new one until its own is computed
	riverNetwork.reset();
	riverMap.Clear();
	erodeRequested = false;
	ShowUnits(std::move(units), _metresPerUnit, _pixelMetres);
	history.Clear();
	historyNote.clear();
	editStatus.clear();
	OnMapChanged();
}

void MapImportSys::ShowUnits(HeightField units, float _metresPerUnit, glm::dvec2 _pixelMetres)
{
	map = std::move(units);
	metresPerUnit = _metresPerUnit;
	pixelMetres = _pixelMetres;
	auto [lowest, highest] = std::minmax_element(map.Data(), map.Data() + map.Size());
	mapMin = *lowest;
	mapMax = *highest;
	//Height 1 = metresPerUnit metres; a texel = worldUnitsPerTexel world units = pixelMetres metres
	trueHeightScale = static_cast<float>(metresPerUnit * TerrainView::worldUnitsPerTexel / pixelMetres.x);
	renderSettings.heightScale = trueHeightScale;

	view.Resize(map.GetWidth(), map.GetHeight());
	view.SetLayer(TerrainView::Layer::TERRAIN, map);
	mapVersion++;
	//The water of the network shown until the new one comes keeps its metres in the new map units
	if (riverNetwork) {
		UploadRiverWater();
	}
}

std::string MapImportSys::ProjectName() const
{
	return projectPath.empty() ? std::string("Untitled") : FileNameOf(projectPath);
}

void MapImportSys::SetProjectStatus(const std::string& text, bool isError)
{
	projectStatus = text;
	projectStatusIsError = isError;
}

void MapImportSys::SaveProject()
{
	if (projectPath.empty()) {
		SaveProjectAs();
	}
	else {
		StartSaving(projectPath);
	}
}

void MapImportSys::SaveProjectAs()
{
	if (map.Empty()) {
		return;
	}
	if (Busy()) {
		SetProjectStatus("Wait for the current save or load to finish", true);
		return;
	}
	if (projectPath.empty()) {
		//The dialog ignores a folder that does not exist yet and opens anywhere; the project list reads this folder
		std::error_code ec;
		std::filesystem::create_directories(ProjectFolder(), ec);
	}
	std::string dialogError;
	const std::u8string stem = projectPath.stem().u8string();
	const std::optional<std::filesystem::path> file = SaveFileDialog(L"Terrain project (*.ptgmap)\0*.ptgmap\0", L"ptgmap",
		projectPath.empty() ? ProjectFolder() : projectPath.parent_path(), projectPath.empty() ? suggestedName : std::string(stem.begin(), stem.end()), dialogError);
	if (!file) {
		if (!dialogError.empty()) {
			SetProjectStatus(dialogError, true);
		}
		return;
	}
	StartSaving(mapproject::WithExtension(*file));
}

void MapImportSys::StartSaving(const std::filesystem::path& path)
{
	if (map.Empty()) {
		return;
	}
	if (Busy()) {
		SetProjectStatus("Wait for the current save or load to finish", true);
		return;
	}
	mapproject::Project project;
	project.working = map;
	project.original = original;
	project.pixelMetres = pixelMetres;
	project.seaLevel = seaLevelMetres;
	project.source = projectSource;
	project.generation = projectGeneration;
	project.verticalExaggeration = renderSettings.heightScale / trueHeightScale;
	project.rivers = preset::RiversJson(riverConfig);
	const float unit = metresPerUnit;
	const uint64_t version = mapVersion;
	saveJob.Start([project = std::move(project), path, unit, version]() mutable {
		SaveResult result;
		result.path = path;
		result.version = version;
		auto start = std::chrono::steady_clock::now();
		//An exception would come back in future.get() on the main thread and end the app
		try {
			for (HeightField* m : { &project.working, &project.original }) {
				for (size_t i = 0; i < m->Size(); i++) {
					(*m)[i] *= unit;
				}
			}
			result.ok = mapproject::Save(path, project, result.error);
		}
		catch (const std::bad_alloc&) {
			result.ok = false;
			result.error = "Not enough memory to save the project";
		}
		catch (const std::exception& e) {
			result.ok = false;
			result.error = e.what();
		}
		if (result.ok) {
			std::error_code ec;
			result.bytes = std::filesystem::file_size(path, ec);
		}
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
	SetProjectStatus("Saving " + FileNameOf(path) + "...", false);
}

void MapImportSys::FinishSaving(SaveResult& result)
{
	if (!result.ok) {
		//The project keeps its changes and its previous file
		SetProjectStatus("Could not save " + FileNameOf(result.path) + ": " + result.error, true);
		//What the save was for does not happen (it would lose the changes): the question comes back with the reason, in any mode
		if (afterSave) {
			pending = std::move(afterSave);
			afterSave.reset();
			questionError = projectStatus;
			openQuestion = true;
		}
		return;
	}
	char text[128];
	std::snprintf(text, sizeof(text), " (%.1f MB in %.0f ms)", static_cast<double>(result.bytes) / (1024.0 * 1024.0), result.ms);
	SetProjectStatus("Saved " + FileNameOf(result.path) + text, false);
	std::cout << "[LOG] Project saved: " << FileNameOf(result.path) << text << "\n";
	projectsListed = false;
	//Another map shown during the save is a different project, still not saved
	if (result.version == mapVersion) {
		projectPath = result.path;
		const std::u8string stem = projectPath.stem().u8string();
		suggestedName = std::string(stem.begin(), stem.end());
		dirty = false;
	}
	if (afterSave) {
		PendingAction action = std::move(*afterSave);
		afterSave.reset();
		Request(std::move(action));
	}
}

void MapImportSys::Request(PendingAction action)
{
	if (!HasUnsavedChanges()) {
		Run(action);
		return;
	}
	//A newer request replaces one still waiting for the answer
	pending = std::move(action);
	openQuestion = true;
}

void MapImportSys::Run(PendingAction& action)
{
	switch (action.kind) {
	case PendingAction::Kind::OPEN:
		OpenProject(action.path);
		break;
	case PendingAction::Kind::LOAD:
		StartLoading(action.path);
		break;
	case PendingAction::Kind::RECEIVE:
		if (action.received) {
			InstallReceived(*action.received);
		}
		break;
	case PendingAction::Kind::QUIT:
		quitApproved = true;
		break;
	}
}

void MapImportSys::Receive(mapproject::Project project)
{
	PendingAction action;
	action.kind = PendingAction::Kind::RECEIVE;
	action.received = std::move(project);
	Request(std::move(action));
}

void MapImportSys::InstallReceived(mapproject::Project& project)
{
	if (project.working.Empty()) {
		return;
	}
	//Shown at once, also while a file is loading or a project saving: their results see the new map version
	HeightField noOriginal;
	const float unit = ToUnits(project.working, noOriginal);
	InstallMap(std::move(project.working), unit, project.pixelMetres);
	original = HeightField();
	projectPath.clear();
	projectSource = project.source;
	projectGeneration = project.generation;
	seaLevelMetres = project.seaLevel;
	//"TerrainGen map at (12, -40)" -> "terraingen_map_at_12_-40"
	suggestedName.clear();
	for (char c : project.source) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || c == '-') {
			suggestedName += static_cast<char>(std::tolower(u));
		}
		else if (!suggestedName.empty() && suggestedName.back() != '_') {
			suggestedName += '_';
		}
	}
	while (!suggestedName.empty() && suggestedName.back() == '_') {
		suggestedName.pop_back();
	}
	if (suggestedName.empty()) {
		suggestedName = "map";
	}
	dirty = true;
	mapPath.clear();
	mapInfo = heightmap::ImportInfo();
	mapInfo.bitsPerSample = 32;
	mapInfo.floatingPoint = true;
	mapInfo.metres = true;
	lastLoadMs = 0.0;
	lookDown = true;
	SetProjectStatus("Received: " + project.source, false);
}

void MapImportSys::RequestQuit()
{
	//Closing again while the save asked for by the question is running: it closes the window when it is done
	if (afterSave && afterSave->kind == PendingAction::Kind::QUIT) {
		return;
	}
	PendingAction action;
	action.kind = PendingAction::Kind::QUIT;
	Request(std::move(action));
}

bool MapImportSys::TakeQuit()
{
	const bool approved = quitApproved;
	quitApproved = false;
	return approved;
}

void MapImportSys::ImGuiModals()
{
	const char* title = "Unsaved changes";
	//A save asked for by the question may take seconds and the Project tab may not be shown: say so over the scene
	if (afterSave && saveJob.IsRunning()) {
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("##savingProject", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
		ImGui::TextUnformatted(projectStatus.c_str());
		ImGui::End();
	}
	if (openQuestion) {
		ImGui::OpenPopup(title);
		openQuestion = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}
	const char* what = "";
	if (pending) {
		switch (pending->kind) {
		case PendingAction::Kind::OPEN: what = "opening another project"; break;
		case PendingAction::Kind::LOAD: what = "loading another map"; break;
		case PendingAction::Kind::RECEIVE: what = "taking the map sent from another mode"; break;
		case PendingAction::Kind::QUIT: what = "closing the application"; break;
		}
	}
	ImGui::Text("The project %s has changes that are not saved.", ProjectName().c_str());
	ImGui::Text("Save them before %s?", what);
	if (!questionError.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		ImGui::TextWrapped("%s", questionError.c_str());
		ImGui::PopStyleColor();
	}
	if (ImGui::Button("Save")) {
		afterSave = std::move(pending);
		pending.reset();
		questionError.clear();
		ImGui::CloseCurrentPopup();
		//A project without a file asks where to save it: cancelling that cancels the action too
		SetProjectStatus("", false);
		SaveProject();
		if (!saveJob.IsRunning()) {
			//A save that could not start (busy, dialog failed) asks again with the reason
			if (projectStatusIsError && afterSave) {
				pending = std::move(afterSave);
				questionError = projectStatus;
				openQuestion = true;
			}
			afterSave.reset();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard")) {
		questionError.clear();
		ImGui::CloseCurrentPopup();
		if (pending) {
			PendingAction action = std::move(*pending);
			pending.reset();
			Run(action);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		pending.reset();
		questionError.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void MapImportSys::OpenProject(const std::filesystem::path& path)
{
	if (Busy()) {
		SetProjectStatus("Wait for the current save or load to finish", true);
		return;
	}
	const uint64_t version = mapVersion;
	projectJob.Start([path, version]() {
		ProjectLoad result;
		result.path = path;
		result.version = version;
		auto start = std::chrono::steady_clock::now();
		try {
			result.ok = mapproject::Load(path, result.project, result.error);
			if (result.ok) {
				result.metresPerUnit = ToUnits(result.project.working, result.project.original);
			}
		}
		catch (const std::bad_alloc&) {
			result.ok = false;
			result.error = "Not enough memory to open the project";
		}
		catch (const std::exception& e) {
			result.ok = false;
			result.error = e.what();
		}
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
	SetProjectStatus("Opening " + FileNameOf(path) + "...", false);
}

void MapImportSys::InstallProject(ProjectLoad& result)
{
	if (!result.ok) {
		//The current map and project stay
		SetProjectStatus("Could not open " + FileNameOf(result.path) + ": " + result.error, true);
		return;
	}
	if (result.version != mapVersion) {
		SetProjectStatus("Dropped " + FileNameOf(result.path) + ": another map was shown while it was opening", true);
		return;
	}
	mapproject::Project& project = result.project;
	InstallMap(std::move(project.working), result.metresPerUnit, project.pixelMetres);
	original = std::move(project.original);
	renderSettings.heightScale = trueHeightScale * project.verticalExaggeration;
	projectPath = result.path;
	projectSource = project.source;
	projectGeneration = project.generation;
	seaLevelMetres = project.seaLevel;
	const std::u8string stem = projectPath.stem().u8string();
	suggestedName = std::string(stem.begin(), stem.end());
	dirty = false;
	mapPath = result.path;
	mapInfo = heightmap::ImportInfo();
	mapInfo.bitsPerSample = 32;
	mapInfo.floatingPoint = true;
	mapInfo.metres = true;
	lastLoadMs = result.ms;
	lookDown = true;
	SetProjectStatus("Opened " + FileNameOf(result.path), false);
	//River settings of the project (none in version 1: rivers off)
	riverConfig = rivers::RiverConfig();
	if (!project.rivers.empty()) {
		std::string riverError;
		if (!preset::RiversFromJson(project.rivers, riverConfig, riverError)) {
			SetProjectStatus("Opened " + FileNameOf(result.path) + "; river settings ignored: " + riverError, true);
		}
	}
}

std::filesystem::path MapImportSys::ProjectFolder() const
{
	const std::string text = projectFolder;
	return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

void MapImportSys::RefreshProjects()
{
	projects = mapproject::ListProjects(ProjectFolder());
	projectsListed = true;
}

void MapImportSys::BrowseForProject()
{
	std::string dialogError;
	const std::optional<std::filesystem::path> file = OpenFileDialog(L"Terrain project (*.ptgmap)\0*.ptgmap\0", ProjectFolder(), dialogError);
	if (!file) {
		if (!dialogError.empty()) {
			SetProjectStatus(dialogError, true);
		}
		return;
	}
	Request({ PendingAction::Kind::OPEN, *file, std::nullopt });
}

void MapImportSys::ProjectImGui()
{
	if (!projectsListed) {
		RefreshProjects();
	}
	const bool busy = Busy();
	ImGui::Text("Project: %s%s", ProjectName().c_str(), HasUnsavedChanges() ? " *" : "");
	if (!projectSource.empty()) {
		ImGui::TextWrapped("Source: %s", projectSource.c_str());
	}

	ImGui::BeginDisabled(busy);
	if (ImGui::Button("Open...")) {
		BrowseForProject();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(map.Empty());
	if (ImGui::Button("Save")) {
		SaveProject();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save as...")) {
		SaveProjectAs();
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Ctrl+S saves the project");

	if (!projectStatus.empty()) {
		if (projectStatusIsError) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		}
		ImGui::TextWrapped("%s", projectStatus.c_str());
		if (projectStatusIsError) {
			ImGui::PopStyleColor();
		}
	}

	ImGui::SeparatorText("Projects in folder");
	if (ImGui::InputText("Folder##projects", projectFolder, sizeof(projectFolder))) {
		RefreshProjects();
	}
	if (ImGui::Button("Refresh list##projects")) {
		RefreshProjects();
	}
	if (projects.empty()) {
		ImGui::TextWrapped("No projects (.ptgmap) in this folder.");
	}
	else if (ImGui::BeginListBox("##projects", ImVec2(-FLT_MIN, 0.0f))) {
		for (const std::filesystem::path& file : projects) {
			const bool current = file == projectPath;
			ImGui::Selectable(FileNameOf(file).c_str(), current, ImGuiSelectableFlags_AllowDoubleClick);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !busy) {
				Request({ PendingAction::Kind::OPEN, file, std::nullopt });
			}
		}
		ImGui::EndListBox();
	}
	ImGui::TextDisabled("Double click opens a project.");
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

void MapImportSys::StartLoading(const std::filesystem::path& path, bool reload)
{
	if (Busy()) {
		error = "Wait for the current save or load to finish";
		return;
	}
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
	const uint64_t version = mapVersion;
	job.Start([path, options, custom, rangeUnit, pixel, reload, version]() {
		LoadResult result;
		result.path = path;
		result.reload = reload;
		result.version = version;
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
	if (result.version != mapVersion) {
		error = "Dropped " + FileNameOf(result.path) + ": another map was shown while it was loading";
		return;
	}
	mapInfo = result.info;
	mapPath = result.path;
	lastLoadMs = result.ms;
	InstallMap(std::move(result.map), result.metresPerUnit, result.pixelMetres);
	original = HeightField();
	dirty = true;
	if (!result.reload) {
		//A new project, not saved yet
		projectPath.clear();
		projectSource = FileNameOf(result.path);
		projectGeneration.clear();
		seaLevelMetres.reset();
		const std::u8string stem = result.path.stem().u8string();
		suggestedName = std::string(stem.begin(), stem.end());
	}
}

void MapImportSys::Draw(Renderer& renderer, Camera& camera, LightSource& light)
{
	view.Draw(renderer, camera, light, renderSettings);
	const bool rivers = RiversOn() && riverMap.Ready();
	const TextureClass* height = view.GetLayerTexture(TerrainView::Layer::TERRAIN);
	if (map.Empty() || !height || (!seaLevelMetres && !rivers)) {
		return;
	}
	//Water last: it reads a copy of everything drawn before
	const glm::vec2 size(view.GetWorldWidth(), view.GetWorldHeight());
	//World XZ of map position (0, 0), where the river network lies: the centre of texel (0, 0)
	const glm::vec2 worldTexelOrigin = (-glm::vec2(view.GetMapWidth(), view.GetMapHeight()) * 0.5f + 0.5f) * TerrainView::worldUnitsPerTexel;
	//Without a sea its surface lies under the whole map, hidden by the terrain
	const float sea = seaLevelMetres ? *seaLevelMetres / metresPerUnit : mapMin - 1.0f;
	water.DrawMap(*height, view.GetWorldCorner(), size, worldTexelOrigin, camera, light, renderSettings, sea * renderSettings.heightScale,
		view.GetFlatViewMin(), view.GetFlatViewMax(), rivers ? &riverMap : nullptr);
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
	//Ctrl+S anywhere in this mode, unless a text field takes the keys
	if (!ImGui::GetIO().WantTextInput && !map.Empty()) {
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
			SaveProject();
		}
		//Undo and redo; not while an operation, a load or a save runs
		if (!Busy()) {
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
				StepHistory(false);
			}
			else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
				StepHistory(true);
			}
		}
	}
	const PanelTabs::Tab tabs[] = {
		{ "Project", [this] { ProjectImGui(); } },
		{ "Edit", [this] { EditImGui(); } },
		{ "Load", [this] { LoadImGui(); } },
	};
	importTabs.Draw("import", tabs);
}

void MapImportSys::LoadImGui()
{
	if (!listed) {
		RefreshFiles();
	}
	const bool loading = Busy();

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
		Request({ PendingAction::Kind::LOAD, selected, std::nullopt });
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
		//Changing the range of the loaded map reads its file again; the new range applies to it right away. Not once the map is edited:
		//the reload would replace the edits without asking, so the range waits for the next Load (which asks about unsaved changes)
		const bool edited = !original.Empty() || history.CanUndo();
		if (ImGui::Combo("Value range", &selectedRange, valueRangeNames) && mapPath == selected && selectedRange != customRange && !edited) {
			StartLoading(selected, true);
		}
		if (edited && mapPath == selected) {
			ImGui::TextDisabled("The map is edited: a new range applies when the file is loaded again.");
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
	water.ImGuiDraw();
}

void MapImportSys::ImGuiSaving()
{
	if (map.Empty()) {
		ImGui::TextWrapped("Load a map or open a project first.");
		return;
	}
	ExportPanel::Pack pack;
	pack.mode = "Imported map";
	//Map units are metres / metresPerUnit with metresPerUnit = the largest |height|, so they never leave [-1, 1]
	pack.range = heightmap::noiseRange;
	pack.metresPerUnit = metresPerUnit;
	pack.metresPerTexel = static_cast<float>(pixelMetres.x);
	if (seaLevelMetres) {
		pack.seaLevel = *seaLevelMetres / metresPerUnit;
	}
	const std::string generation = projectGeneration;
	pack.generation = [generation] { return generation; };
	pack.heightmaps = { { map.GetWidth(), map.GetHeight(), "", [this] { return map; } } };
	pack.layersMissing = "Material and biome maps: not available for imported maps.";
	pack.vegetationMissing = "Vegetation: not available for imported maps.";
	//Rivers and lakes of the network shown: positions in map texels, levels in metres / mapMetresPerHeight
	if (RiversOn() && riverNetwork && !riverNetwork->water.empty()) {
		const float pixel = static_cast<float>(pixelMetres.x);
		pack.riverWater = [this, pixel] {
			std::vector<glm::vec3> triangles;
			triangles.reserve(riverNetwork->water.size());
			for (const hydrology::WaterVertex& v : riverNetwork->water) {
				triangles.emplace_back(v.x * pixel, v.level * hydrology::mapMetresPerHeight, v.y * pixel);
			}
			return triangles;
		};
	}
	exporter.ImGuiDraw(pack);
}

void MapImportSys::ImGuiOutput()
{
	if (map.Empty()) {
		ImGui::Text("No map loaded");
		return;
	}
	ImGui::Text("Project: %s%s - %s", ProjectName().c_str(), HasUnsavedChanges() ? " *" : "", projectSource.c_str());
	ImGui::Text("Map: %s (%dx%d, %d-bit%s%s), loaded in %.1f ms", mapPath.empty() ? "-" : FileNameOf(mapPath).c_str(), map.GetWidth(), map.GetHeight(),
		mapInfo.bitsPerSample, mapInfo.floatingPoint ? " float" : "", mapInfo.convertedFromColor ? ", color converted to grey" : "", lastLoadMs);
	ImGui::Text("Pixel: %.2f m%s", pixelMetres.x, mapInfo.geographic ? " (from degrees)" : "");
	ImGui::Text("Heights: %.1f to %.1f m", mapMin * metresPerUnit, mapMax * metresPerUnit);
	if (mapInfo.noDataFilled > 0) {
		ImGui::Text("NoData filled: %d", mapInfo.noDataFilled);
	}
}
