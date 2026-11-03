#include "ExportPanel.h"

#include <iostream>
#include <system_error>

#include "imgui.h"

#include "Preset.h"

ExportPanel::ExportPanel(std::filesystem::path _folder) : folder(std::move(_folder))
{
}

std::filesystem::path ExportPanel::PathOf(const Target& target) const
{
	return folder / heightmap::FileName(std::string(name) + target.suffix, format, target.map.GetWidth(), target.map.GetHeight());
}

void ExportPanel::ExportAll(const std::vector<Target>& targets, heightmap::ValueRange range)
{
	std::string saved;
	size_t clipped = 0;
	for (const Target& target : targets) {
		const std::filesystem::path path = PathOf(target);
		const std::string fileName = path.filename().string();
		size_t mapClipped = 0;
		std::string error;
		if (!heightmap::Export(path, target.map, format, range, mapClipped, error)) {
			status = "Error (" + fileName + "): " + error;
			if (!saved.empty()) {
				status += "\nSaved before the error: " + saved;
			}
			statusIsError = true;
			return;
		}
		saved += (saved.empty() ? "" : ", ") + fileName;
		clipped += mapClipped;
	}

	std::error_code ec;
	std::u8string location = std::filesystem::absolute(folder, ec).u8string();
	status = "Saved: " + saved + "\nFolder: " + std::string(location.begin(), location.end());
	if (clipped > 0) {
		status += "\nCells outside the value range were clipped: " + std::to_string(clipped);
	}
	statusIsError = false;
}

void ExportPanel::ImGuiDraw(const std::vector<Target>& targets, heightmap::ValueRange range)
{
	if (!ImGui::CollapsingHeader("Saving")) {
		return;
	}

	int selectedFormat = static_cast<int>(format);
	if (ImGui::Combo("Format", &selectedFormat, "PNG 16-bit\0RAW 16-bit (R16)\0")) {
		format = static_cast<heightmap::Format>(selectedFormat);
	}
	ImGui::InputText("File name", name, sizeof(name));

	bool validName = preset::IsValidName(name);
	bool exists = false;
	std::string files;
	if (validName) {
		std::error_code ec;
		for (const Target& target : targets) {
			const std::filesystem::path path = PathOf(target);
			exists |= std::filesystem::exists(path, ec);
			files += (files.empty() ? "" : ", ") + path.filename().string();
		}
	}

	ImGui::BeginDisabled(!validName);
	if (ImGui::Button(exists ? "Overwrite heightmap" : "Save heightmap")) {
		ExportAll(targets, range);
	}
	ImGui::EndDisabled();

	if (name[0] != '\0' && !validName) {
		ImGui::TextWrapped("Use letters, digits, spaces, '-' and '_' only.");
	}
	else if (validName) {
		ImGui::TextWrapped(exists ? "Replaces: %s" : "Writes: %s", files.c_str());
	}

	if (!status.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", status.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::Button("Save heightMap as an 3d object")) {
		std::cout << "[LOG] Well it will work one day i promise!" << std::endl;
	}
}
