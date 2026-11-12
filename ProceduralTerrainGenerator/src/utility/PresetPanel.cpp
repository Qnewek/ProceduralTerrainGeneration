#include "PresetPanel.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

#include "Preset.h"

//Preset name shown in the UI; file names can hold characters outside ASCII if a file was added by hand
static std::string DisplayName(const std::filesystem::path& file)
{
	std::u8string text = file.stem().u8string();
	return std::string(text.begin(), text.end());
}

PresetPanel::PresetPanel(std::filesystem::path _folder) : folder(std::move(_folder))
{
}

void PresetPanel::Refresh()
{
	files = preset::List(folder);
	if (std::find(files.begin(), files.end(), selected) == files.end()) {
		selected.clear();
	}
	listed = true;
}

std::filesystem::path PresetPanel::SavePath() const
{
	return folder / (std::string(name) + ".json");
}

void PresetPanel::ReportSaved()
{
	Refresh();
	selected = SavePath();
	status = "Saved: " + DisplayName(selected);
	statusIsError = false;
}

void PresetPanel::ReportLoaded()
{
	status = "Loaded: " + DisplayName(selected);
	statusIsError = false;
}

void PresetPanel::ReportError(const std::string& message)
{
	status = "Error: " + message;
	statusIsError = true;
}

PresetPanel::Action PresetPanel::ImGuiDraw()
{
	if (!listed) {
		Refresh();
	}

	Action action = Action::NONE;

	if (ImGui::BeginCombo("Preset", selected.empty() ? "[none]" : DisplayName(selected).c_str())) {
		for (const std::filesystem::path& file : files) {
			bool isSelected = file == selected;
			if (ImGui::Selectable(DisplayName(file).c_str(), isSelected)) {
				selected = file;
				//Selecting fills the name so that a loaded preset is easy to update
				std::snprintf(name, sizeof(name), "%s", DisplayName(file).c_str());
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::BeginDisabled(selected.empty());
	if (ImGui::Button("Load")) {
		action = Action::LOAD;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Refresh list")) {
		Refresh();
	}

	ImGui::InputText("Name", name, sizeof(name));
	bool validName = preset::IsValidName(name);
	std::error_code ec;
	bool exists = validName && std::filesystem::exists(SavePath(), ec);
	ImGui::BeginDisabled(!validName);
	if (ImGui::Button(exists ? "Overwrite" : "Save")) {
		action = Action::SAVE;
	}
	ImGui::EndDisabled();
	if (name[0] != '\0' && !validName) {
		ImGui::TextWrapped("Use letters, digits, spaces, '-' and '_' only.");
	}
	else if (exists) {
		ImGui::TextWrapped("A preset with this name already exists.");
	}

	if (!status.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", status.c_str());
		ImGui::PopStyleColor();
	}
	return action;
}
