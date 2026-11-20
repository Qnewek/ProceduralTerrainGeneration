#include "ExportPanel.h"

#include <system_error>

#include "imgui.h"

#include "FileIO.h"
#include "Preset.h"

ExportPanel::ExportPanel(std::filesystem::path _folder) : folder(std::move(_folder))
{
}

std::string ExportPanel::HeightFileName(const Target& target) const
{
	return heightmap::FileName(std::string(name) + "_height" + target.suffix, format, target.width, target.height);
}

void ExportPanel::ExportAll(const Pack& pack)
{
	const std::filesystem::path packFolder = PackFolder();
	exportpack::Manifest manifest;
	manifest.name = name;
	manifest.mode = pack.mode;
	manifest.range = pack.range;
	manifest.metresPerUnit = pack.metresPerUnit;
	manifest.seaLevel = pack.seaLevel;

	std::string saved;
	size_t clipped = 0;
	for (size_t i = 0; i < pack.heightmaps.size(); i++) {
		const Target& target = pack.heightmaps[i];
		const std::string fileName = HeightFileName(target);
		const HeightField map = target.read();
		if (i == 0) {
			manifest.width = map.GetWidth();
			manifest.height = map.GetHeight();
			manifest.mapValues = heightmap::MinMax(map);
		}
		size_t mapClipped = 0;
		std::string error;
		if (!heightmap::Export(packFolder / fileName, map, format, pack.range, mapClipped, error, pack.metresPerUnit)) {
			status = "Error (" + fileName + "): " + error;
			if (!saved.empty()) {
				status += "\nSaved before the error: " + saved;
			}
			statusIsError = true;
			return;
		}
		manifest.files.push_back({ fileName, std::string("height") + target.suffix, format });
		saved += (saved.empty() ? "" : ", ") + fileName;
		clipped += mapClipped;
	}

	if (pack.generation) {
		manifest.generation = pack.generation();
	}
	const std::string manifestName = exportpack::ManifestName(name);
	const std::string json = exportpack::ToJson(manifest);
	std::string error;
	if (!WriteFileAtomic(packFolder / manifestName, json.data(), json.size(), error)) {
		status = "Error (" + manifestName + "): " + error + "\nSaved before the error: " + saved;
		statusIsError = true;
		return;
	}
	saved += ", " + manifestName;

	std::error_code ec;
	std::u8string location = std::filesystem::absolute(packFolder, ec).u8string();
	status = "Saved: " + saved + "\nFolder: " + std::string(location.begin(), location.end());
	if (clipped > 0) {
		status += format == heightmap::Format::EXR32 ? "\nInvalid (non-finite) cells replaced: " : "\nCells outside the value range were clipped: ";
		status += std::to_string(clipped);
	}
	statusIsError = false;
}

void ExportPanel::ImGuiDraw(const Pack& pack)
{
	int selectedFormat = static_cast<int>(format);
	if (ImGui::Combo("Height format", &selectedFormat, "PNG 16-bit\0RAW 16-bit (R16)\0EXR 32-bit float (metres)\0")) {
		format = static_cast<heightmap::Format>(selectedFormat);
	}
	ImGui::SetItemTooltip("PNG / R16: Unreal and Unity landscapes (16-bit, fixed value range).\n"
		"EXR: exact heights in metres, e.g. for Godot, Blender or Houdini.");
	ImGui::InputText("Pack name", name, sizeof(name));

	const bool validName = preset::IsValidName(name);
	bool exists = false;
	std::string files;
	if (validName) {
		std::error_code ec;
		const std::filesystem::path packFolder = PackFolder();
		for (const Target& target : pack.heightmaps) {
			const std::string fileName = HeightFileName(target);
			exists |= std::filesystem::exists(packFolder / fileName, ec);
			files += (files.empty() ? "" : ", ") + fileName;
		}
		const std::string manifestName = exportpack::ManifestName(name);
		exists |= std::filesystem::exists(packFolder / manifestName, ec);
		files += ", " + manifestName;
	}

	ImGui::BeginDisabled(!validName || pack.heightmaps.empty());
	if (ImGui::Button(exists ? "Overwrite export" : "Export")) {
		ExportAll(pack);
	}
	ImGui::EndDisabled();

	if (name[0] != '\0' && !validName) {
		ImGui::TextWrapped("Use letters, digits, spaces, '-' and '_' only.");
	}
	else if (validName) {
		const std::string location = (std::filesystem::path(folder) / name).generic_string();
		ImGui::TextWrapped(exists ? "Replaces in %s/: %s" : "Writes to %s/: %s", location.c_str(), files.c_str());
	}

	if (!status.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
		ImGui::TextWrapped("%s", status.c_str());
		ImGui::PopStyleColor();
	}
}
