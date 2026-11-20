#include "PlacesPanel.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

PlacesPanel::PlacesPanel(std::filesystem::path _file) : file(std::move(_file))
{
}

void PlacesPanel::Load()
{
	loaded = true;
	selected = -1;
	std::string error;
	if (!places::Load(file, list, error)) {
		list.clear();
		status = "Error reading " + file.generic_string() + ": " + error;
		statusIsError = true;
	}
}

void PlacesPanel::Save(const std::string& done)
{
	std::string error;
	if (places::Save(file, list, error)) {
		status = done;
		statusIsError = false;
	}
	else {
		status = "Error: " + error;
		statusIsError = true;
	}
}

std::optional<places::Place> PlacesPanel::ImGuiDraw(const std::function<places::Place(const std::string& name)>& here)
{
	if (!loaded) {
		Load();
	}
	std::optional<places::Place> chosen;

	if (list.empty()) {
		ImGui::TextDisabled("No places yet: name the current view and add it.");
	}
	else if (ImGui::BeginListBox("##places", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * std::min<float>(static_cast<float>(list.size()), 6.0f) + 4.0f))) {
		for (int i = 0; i < static_cast<int>(list.size()); i++) {
			ImGui::PushID(i);
			if (ImGui::Selectable(list[i].name.c_str(), i == selected, ImGuiSelectableFlags_AllowDoubleClick)) {
				selected = i;
				//Selecting fills the name, so a place is easy to update with the current view
				std::snprintf(name, sizeof(name), "%s", list[i].name.c_str());
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					chosen = list[i];
				}
			}
			ImGui::SetItemTooltip("Map offset %d, %d\nDouble click: go there", list[i].mapOffset.x, list[i].mapOffset.y);
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}

	const bool hasSelection = selected >= 0 && selected < static_cast<int>(list.size());
	ImGui::BeginDisabled(!hasSelection);
	if (ImGui::Button("Go")) {
		chosen = list[selected];
	}
	ImGui::SetItemTooltip("Moves the camera and the partial map to the place (the map is generated there)");
	ImGui::SameLine();
	if (ImGui::Button("Delete")) {
		const std::string removed = list[selected].name;
		list.erase(list.begin() + selected);
		selected = -1;
		Save("Deleted: " + removed);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		Load();
	}
	ImGui::SetItemTooltip("Reads %s again (e.g. after editing it by hand)", file.generic_string().c_str());

	ImGui::InputText("Name##place", name, sizeof(name));
	const std::string newName(name);
	auto existing = std::find_if(list.begin(), list.end(), [&](const places::Place& p) { return p.name == newName; });
	ImGui::BeginDisabled(newName.empty() || (existing == list.end() && list.size() >= places::maxPlaces));
	if (ImGui::Button(existing != list.end() ? "Replace with current view" : "Add current view")) {
		places::Place place = here(newName);
		if (existing != list.end()) {
			*existing = place;
			selected = static_cast<int>(existing - list.begin());
		}
		else {
			list.push_back(place);
			selected = static_cast<int>(list.size()) - 1;
		}
		Save("Saved place: " + newName);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Remembers where the camera is and looks, and where the partial map lies.\n"
		"A place does not hold the generation parameters: it shows the same terrain with the same preset");

	if (!status.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", status.c_str());
		ImGui::PopStyleColor();
	}
	return chosen;
}
