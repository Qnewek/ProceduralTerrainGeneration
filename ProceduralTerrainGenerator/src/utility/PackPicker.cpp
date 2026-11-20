#include "PackPicker.h"

#include <algorithm>

#include "imgui.h"

//Pack name shown in the UI: the name of its folder
static std::string PackName(const std::filesystem::path& manifest)
{
	std::u8string text = manifest.stem().u8string();
	return std::string(text.begin(), text.end());
}

PackPicker::PackPicker(std::filesystem::path _folder) : folder(std::move(_folder))
{
}

void PackPicker::Refresh()
{
	packs = exportpack::ListPacks(folder);
	if (std::find(packs.begin(), packs.end(), selected) == packs.end()) {
		selected.clear();
	}
	listed = true;
}

void PackPicker::ReportLoaded()
{
	status = "Loaded: " + PackName(selected);
	statusIsError = false;
}

void PackPicker::ReportError(const std::string& message)
{
	status = "Error: " + message;
	statusIsError = true;
}

std::optional<exportpack::PackSource> PackPicker::ImGuiDraw()
{
	if (!listed) {
		Refresh();
	}
	std::optional<exportpack::PackSource> chosen;

	if (ImGui::BeginCombo("Pack", selected.empty() ? "[none]" : PackName(selected).c_str())) {
		for (const std::filesystem::path& manifest : packs) {
			const bool isSelected = manifest == selected;
			if (ImGui::Selectable(PackName(manifest).c_str(), isSelected)) {
				selected = manifest;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::BeginDisabled(selected.empty());
	if (ImGui::Button("Load from pack")) {
		exportpack::PackSource source;
		std::string error;
		if (exportpack::LoadSource(selected, source, error)) {
			chosen = std::move(source);
		}
		else {
			ReportError(error);
		}
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Takes the generation parameters of the pack (as a preset) and moves the partial map where the pack lay in the world");
	ImGui::SameLine();
	if (ImGui::Button("Refresh list##packs")) {
		Refresh();
	}

	if (!status.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", status.c_str());
		ImGui::PopStyleColor();
	}
	return chosen;
}
