//Edit tab of the "Imported map" mode: operations on the working copy, the history and the sea level (part of MapImportSys)
#include "MapImportSys.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>
#include <string>

#include "imgui.h"

#include "ErosionPanel.h"
#include "TextureClass.h"

HeightField MapImportSys::WorkingMetres() const
{
	HeightField metres = map;
	for (size_t i = 0; i < metres.Size(); i++) {
		metres[i] *= metresPerUnit;
	}
	return metres;
}

void MapImportSys::ShowMetres(HeightField metres)
{
	const float exaggeration = renderSettings.heightScale / trueHeightScale;
	//The original back in metres, so that both get the same new unit
	for (size_t i = 0; i < original.Size(); i++) {
		original[i] *= metresPerUnit;
	}
	const float unit = ToUnits(metres, original);
	ShowUnits(std::move(metres), unit, pixelMetres);
	renderSettings.heightScale = trueHeightScale * exaggeration;
	dirty = true;
	OnMapChanged();
}

void MapImportSys::ApplyEdit(HeightField metres, std::optional<float> seaLevel, mapedit::Step before)
{
	//The first change keeps the map it started from as the original
	if (original.Empty()) {
		original = map;
	}
	const size_t stepBytes = before.map ? before.map->Size() * sizeof(float) : 0;
	const bool pushed = history.Push(std::move(before));
	if (pushed) {
		historyNote.clear();
	}
	else {
		historyNote = "History off: one step needs " + std::to_string((stepBytes + (1 << 20) - 1) >> 20) + " MB, more than the history memory";
	}
	const std::optional<float> seaKept = seaLevelMetres;
	seaLevelMetres = seaLevel;
	try {
		ShowMetres(std::move(metres));
	}
	catch (const std::bad_alloc&) {
		//ShowMetres allocates only before it replaces the map (an edit keeps the map size, so no new textures; the river water upload
		//catches its own failure), so the map did not change: a step undoing it would undo nothing. Callers restore the GPU layer and report
		seaLevelMetres = seaKept;
		if (pushed) {
			history.DropLast();
		}
		throw;
	}
}

void MapImportSys::SetSeaLevel(std::optional<float> metres, std::optional<float> before)
{
	history.Push({ "Sea level", std::nullopt, before });
	seaLevelMetres = metres;
	dirty = true;
	OnMapChanged();
}

void MapImportSys::StepHistory(bool redo)
{
	if (Busy() || map.Empty() || !(redo ? history.CanRedo() : history.CanUndo())) {
		return;
	}
	const std::string label = redo ? history.RedoLabel() : history.UndoLabel();
	//A copy of the map in metres only for a step that swaps it (a sea or paint step of an 8192² map would copy 256 MB for nothing)
	HeightField metres = (redo ? history.RedoHoldsMap() : history.UndoHoldsMap()) ? WorkingMetres() : HeightField();
	std::optional<float> sea = seaLevelMetres;
	//Moved, not copied (2 bytes a texel); back in place unless the step swapped it
	biomepaint::Layer paint = std::move(biomePaint);
	const std::optional<mapedit::Restored> restored = redo ? history.Redo(metres, sea, paint, placedPlants) : history.Undo(metres, sea, paint, placedPlants);
	if (!restored || !restored->paint) {
		biomePaint = std::move(paint);
	}
	if (restored && restored->plants) {
		PlacedPlantsChanged();
	}
	if (!restored) {
		return;
	}
	seaLevelMetres = sea;
	//A step of painted biomes leaves the map and its climate alone
	if (restored->map) {
		ShowMetres(std::move(metres));
	}
	else if (restored->seaLevel) {
		//As SetSeaLevel: the map and its units stay, the climate and the rivers follow the sea
		dirty = true;
		OnMapChanged();
	}
	if (restored->paint) {
		UsePaint(std::move(paint));
		dirty = true;
	}
	SetEditStatus((redo ? "Redone: " : "Undone: ") + label, false);
}

void MapImportSys::StartEdit(const std::string& label, std::function<void(HeightField&)> operation)
{
	if (Busy() || map.Empty()) {
		return;
	}
	const uint64_t version = mapVersion;
	editJob.Start([metres = WorkingMetres(), label, operation = std::move(operation), version]() mutable {
		EditResult result;
		result.label = label;
		result.version = version;
		auto start = std::chrono::steady_clock::now();
		//An exception would come back in future.get() on the main thread and end the app
		try {
			operation(metres);
			result.metres = std::move(metres);
		}
		catch (const std::bad_alloc&) {
			result.error = "not enough memory";
		}
		catch (const std::exception& e) {
			result.error = e.what();
		}
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
	SetEditStatus(label + "...", false);
}

void MapImportSys::FinishEdit(EditResult& result)
{
	if (!result.error.empty()) {
		SetEditStatus(result.label + " failed: " + result.error, true);
		return;
	}
	if (result.version != mapVersion) {
		SetEditStatus("Dropped " + result.label + ": the map changed while it ran", true);
		return;
	}
	try {
		mapedit::Step before{ result.label, WorkingMetres(), seaLevelMetres };
		ApplyEdit(std::move(result.metres), seaLevelMetres, std::move(before));
	}
	catch (const std::bad_alloc&) {
		//ApplyEdit leaves the map, the sea level and the history as they were
		SetEditStatus(result.label + " failed: not enough memory", true);
		return;
	}
	char text[64];
	std::snprintf(text, sizeof(text), " done in %.0f ms", result.ms);
	SetEditStatus(result.label + text, false);
}

void MapImportSys::Erode()
{
	if (map.Empty()) {
		return;
	}
	if (Busy()) {
		//Replaces "Eroding...": the button was pressed in the frame the other operation started
		SetEditStatus("Erosion: wait until the current operation finishes", true);
		return;
	}
	const char* method = ErosionMethodName(erosionConfig.method);
	const std::string label = std::string("Erosion (") + method + ")";
	auto start = std::chrono::steady_clock::now();
	try {
		const float scale = mapedit::ErosionScale(pixelMetres.x);
		HeightField heights = WorkingMetres();
		float largest = 0.0f;
		for (size_t i = 0; i < heights.Size(); i++) {
			heights[i] *= scale;
			largest = std::max(largest, std::abs(heights[i]));
		}
		//Droplets keep the map in a fixed point buffer (GpuErosion, 2^-24 steps)
		if (erosionConfig.method == erosion::Method::DROPLETS && largest > 127.0f) {
			SetEditStatus("Droplets need heights within 127 erosion units (this map reaches " + std::to_string(static_cast<int>(largest))
				+ "); use pipes", true);
			return;
		}
		const int width = heights.GetWidth(), height = heights.GetHeight();
		TextureClass source(width, height, GL_R32F), target(width, height, GL_R32F);
		source.Upload(heights.Data(), GL_RED, GL_FLOAT);
		erosion.Erode(erosionConfig, source, target, erosionRuns++);
		target.Read(GL_RED, GL_FLOAT, static_cast<int>(heights.Size() * sizeof(float)), heights.Data());
		for (size_t i = 0; i < heights.Size(); i++) {
			heights[i] /= scale;
		}
		ApplyEdit(std::move(heights), seaLevelMetres, { label, WorkingMetres(), seaLevelMetres });
	}
	catch (const std::bad_alloc&) {
		SetEditStatus(label + " failed: not enough memory", true);
		return;
	}
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	char text[64];
	std::snprintf(text, sizeof(text), " done in %.0f ms", ms);
	SetEditStatus(label + text, false);
}

void MapImportSys::SetEditStatus(const std::string& text, bool isError)
{
	editStatus = text;
	editStatusIsError = isError;
}

void MapImportSys::HistoryImGui()
{
	const bool busy = Busy();
	ImGui::BeginDisabled(busy || !history.CanUndo());
	if (ImGui::Button(history.CanUndo() ? ("Undo " + history.UndoLabel() + "###undo").c_str() : "Undo###undo")) {
		StepHistory(false);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Ctrl+Z");
	ImGui::SameLine();
	ImGui::BeginDisabled(busy || !history.CanRedo());
	if (ImGui::Button(history.CanRedo() ? ("Redo " + history.RedoLabel() + "###redo").c_str() : "Redo###redo")) {
		StepHistory(true);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Ctrl+Y or Ctrl+Shift+Z");

	ImGui::Text("History: %.0f of %d MB", static_cast<double>(history.Bytes()) / (1 << 20), historyMegabytes);
	ImGui::SliderInt("History memory (MB)", &historyMegabytes, 256, 8192);
	//On release: dragging down and back up would drop steps on the way
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		history.SetLimit(static_cast<size_t>(historyMegabytes) << 20);
	}
	ImGui::SetItemTooltip("The oldest steps are dropped to fit; the original is always kept");
	if (!historyNote.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", historyNote.c_str());
	}
}

void MapImportSys::EditImGui()
{
	if (map.Empty()) {
		ImGui::TextWrapped("Load a map or open a project first.");
		return;
	}
	HistoryImGui();
	if (!editStatus.empty()) {
		if (editStatusIsError) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		}
		ImGui::TextWrapped("%s", editStatus.c_str());
		if (editStatusIsError) {
			ImGui::PopStyleColor();
		}
	}
	SculptImGui();

	ImGui::BeginDisabled(Busy() || erodeRequested);
	ImGui::SeparatorText("Erosion");
	ImGui::TextDisabled("Heights in map pixels: erosion unit 1 = %.1f m", pixelMetres.x * mapedit::erosionTexelsPerUnit);
	ErosionParametersImGui(erosionConfig);
	if (ImGui::Button("Erode")) {
		erodeRequested = true;
		SetEditStatus("Eroding...", false);
	}

	ImGui::SeparatorText("Smooth");
	ImGui::SliderInt("Radius (px)##smooth", &smoothRadius, 1, 64);
	ImGui::SliderFloat("Strength##smooth", &smoothStrength, 0.0f, 1.0f);
	if (ImGui::Button("Smooth map")) {
		const int radius = smoothRadius;
		const float strength = smoothStrength;
		StartEdit("Smooth", [radius, strength](HeightField& m) { mapedit::Smooth(m, radius, strength); });
	}

	ImGui::SeparatorText("Scale heights");
	ImGui::DragFloat("Factor##scale", &scaleFactor, 0.01f, 0.1f, 10.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat("Offset (m)##scale", &scaleOffset, 1.0f, -10000.0f, 10000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Height = height * factor + offset");
	if (ImGui::Button("Scale heights")) {
		const float factor = scaleFactor, offset = scaleOffset;
		StartEdit("Scale heights", [factor, offset](HeightField& m) { mapedit::Rescale(m, factor, offset); });
	}

	ImGui::SeparatorText("Original");
	ImGui::BeginDisabled(original.Empty());
	if (ImGui::Button("Revert to original")) {
		try {
			HeightField metres = original;
			for (size_t i = 0; i < metres.Size(); i++) {
				metres[i] *= metresPerUnit;
			}
			ApplyEdit(std::move(metres), seaLevelMetres, { "Revert to original", WorkingMetres(), seaLevelMetres });
			SetEditStatus("Reverted to the original", false);
		}
		catch (const std::bad_alloc&) {
			SetEditStatus("Revert to original failed: not enough memory", true);
		}
	}
	ImGui::EndDisabled();
	if (original.Empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(the map is the original)");
	}
	ImGui::EndDisabled();

	//Sea level and rivers wait for a running save too: it marks the project saved when it finishes, and a change made meanwhile would not be in it
	ImGui::BeginDisabled(Busy());
	ImGui::SeparatorText("Sea level");
	const float lowest = mapMin * metresPerUnit, highest = mapMax * metresPerUnit;
	bool sea = seaLevelMetres.has_value();
	if (ImGui::Checkbox("Sea", &sea)) {
		SetSeaLevel(sea ? std::optional<float>(std::clamp(0.0f, lowest, highest)) : std::nullopt, seaLevelMetres);
	}
	if (seaLevelMetres) {
		const float span = std::max(highest - lowest, 1.0f);
		const float previous = *seaLevelMetres;
		float level = previous;
		//Water follows the slider at once; the history step and the rivers wait until it is let go
		if (ImGui::DragFloat("Level (m)##sea", &level, span / 500.0f, lowest - 0.1f * span, highest + 0.1f * span, "%.1f m")) {
			seaLevelMetres = level;
		}
		if (ImGui::IsItemActivated()) {
			seaBefore = previous;
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			history.Push({ "Sea level", std::nullopt, seaBefore });
			dirty = true;
			OnMapChanged();
		}
	}
	RiversImGui();
	ImGui::EndDisabled();
}

void MapImportSys::UpdateRivers()
{
	if (std::optional<RiverResult> result = riverJob.TryTake()) {
		//A network of an older map is dropped; mapChanged is set then, so a new one starts below
		if (result->version == mapVersion && RiversOn()) {
			riverNetwork = std::make_shared<const hydrology::MapRivers>(std::move(result->rivers));
			riverMs = result->ms;
			UploadRiverWater();
			//Nothing stands in the new water
			PlantsChanged();
		}
	}
	if (!mapChanged || riverJob.IsRunning()) {
		return;
	}
	mapChanged = false;
	if (!RiversOn() || map.Empty()) {
		riverMap.Clear();
		if (riverNetwork) {
			PlantsChanged();
		}
		riverNetwork.reset();
		return;
	}
	const uint64_t version = mapVersion;
	riverJob.Start([metres = WorkingMetres(), pixel = pixelMetres.x, sea = seaLevelMetres, config = riverConfig, version]() {
		RiverResult result;
		result.version = version;
		auto start = std::chrono::steady_clock::now();
		//An exception would come back in future.get() on the main thread and end the app
		try {
			result.rivers = hydrology::ComputeOnMap(metres, pixel, sea, config);
		}
		catch (const std::bad_alloc&) {
			result.rivers = hydrology::MapRivers();
			result.rivers.error = "Not enough memory for the river network";
		}
		catch (const std::exception& e) {
			result.rivers = hydrology::MapRivers();
			result.rivers.error = std::string("River network failed: ") + e.what();
		}
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
}

void MapImportSys::UploadRiverWater()
{
	if (!riverNetwork || !riverNetwork->error.empty()) {
		riverMap.Clear();
		return;
	}
	std::vector<hydrology::WaterVertex> vertices = riverNetwork->water;
	const float toUnits = hydrology::mapMetresPerHeight / metresPerUnit;
	//Heights of the map units the view draws, so the shader gets the slope of what it shows
	for (hydrology::WaterVertex& v : vertices) {
		v.level *= toUnits;
		v.slope *= toUnits;
		v.foam *= toUnits;
	}
	riverMap.Upload(riverNetwork->network, vertices);
}

void MapImportSys::RiversImGui()
{
	ImGui::SeparatorText("Rivers and lakes");
	bool on = RiversOn();
	if (ImGui::Checkbox("Rivers and lakes", &on)) {
		riverConfig.method = on ? rivers::Method::REGION : rivers::Method::NONE;
		dirty = true;
		OnMapChanged();
	}
	ImGui::SetItemTooltip("Water flowing over the working copy (not carved into it); computed again in the background after every change");
	if (!on) {
		return;
	}
	rivers::RiverConfig& r = riverConfig;
	bool changed = false;
	const int cellSizes[] = { 4, 8, 16, 32 };
	const char* cellNames[] = { "4", "8", "16", "32" };
	int cellIndex = 1;
	for (int i = 0; i < IM_ARRAYSIZE(cellSizes); i++) {
		cellIndex = cellSizes[i] == r.cellSize ? i : cellIndex;
	}
	if (ImGui::Combo("Cell size", &cellIndex, cellNames, IM_ARRAYSIZE(cellNames))) {
		r.cellSize = cellSizes[cellIndex];
		//Not used here, but the project file checks it against the cell size (Preset.cpp)
		r.regionSize = std::clamp(r.regionSize, 16 * r.cellSize, 2048 * r.cellSize);
		changed = true;
	}
	ImGui::SetItemTooltip("Map pixels between samples of the terrain the water flows over");
	ImGui::SliderFloat("Source area", &r.sourceArea, 1.0e3f, 1.0e7f, "%.3g px^2", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SetItemTooltip("Area that has to drain through a place for a river to start there; less = more rivers");
	ImGui::SliderFloat("Min half width", &r.minHalfWidth, 0.5f, 32.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SliderFloat("Max half width", &r.maxHalfWidth, 0.5f, 128.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	r.maxHalfWidth = std::max(r.maxHalfWidth, r.minHalfWidth);
	ImGui::SliderFloat("Meander", &r.meander, 0.0f, 64.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SliderInt("Smoothing", &r.smoothing, 0, 32, "%d", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	//Heights of the network are metres / mapMetresPerHeight
	float drop = r.surfaceDrop * hydrology::mapMetresPerHeight;
	if (ImGui::SliderFloat("Surface drop", &drop, 0.0f, 5.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
		r.surfaceDrop = drop / hydrology::mapMetresPerHeight;
	}
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SetItemTooltip("How far below the terrain of its course the water surface of a river lies");
	float lakeDepth = r.minLakeDepth * hydrology::mapMetresPerHeight;
	if (ImGui::SliderFloat("Min lake depth", &lakeDepth, 0.0f, 20.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
		r.minLakeDepth = lakeDepth / hydrology::mapMetresPerHeight;
	}
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SetItemTooltip("A filled depression deeper than this is a lake");
	ImGui::SliderFloat("Min lake area", &r.minLakeArea, 0.0f, 1.0e6f, "%.3g px^2", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
	changed |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SetItemTooltip("Smaller lakes are dropped (their place is land); 0 = keep every lake");
	if (changed) {
		dirty = true;
		OnMapChanged();
	}

	if (riverJob.IsRunning()) {
		ImGui::TextDisabled("Computing...");
	}
	else if (riverNetwork && !riverNetwork->error.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", riverNetwork->error.c_str());
	}
	if (riverNetwork && riverNetwork->error.empty()) {
		const hydrology::Network& n = riverNetwork->network;
		ImGui::Text("River cells: %d, lakes: %d (%d cells)", n.riverCells, n.lakes, n.lakeCells);
		ImGui::Text("Computed in %.0f ms (%d x %d cells)", riverMs, n.rivers.GetWidth(), n.rivers.GetHeight());
	}
}
