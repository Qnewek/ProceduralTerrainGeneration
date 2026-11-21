#include "SendToImported.h"

#include <string>

#include "imgui.h"

std::optional<mapproject::Project> SendToImportedImGui(const ExportPanel::Pack& pack)
{
	ImGui::SeparatorText("Imported map");
	std::optional<size_t> pressed;
	for (size_t i = 0; i < pack.heightmaps.size(); i++) {
		const std::string suffix = pack.heightmaps[i].suffix;
		//"_eroded_pipes" -> "pipes"
		const std::string label = i == 0 ? std::string("Send map") : "Send eroded copy (" + suffix.substr(suffix.find_last_of('_') + 1) + ")";
		if (ImGui::Button((label + "##send" + std::to_string(i)).c_str())) {
			pressed = i;
		}
		ImGui::SetItemTooltip("Opens this map in the Imported map mode as a new project, heights in metres");
	}
	if (!pressed) {
		return std::nullopt;
	}

	const ExportPanel::Target& target = pack.heightmaps[*pressed];
	mapproject::Project project;
	project.working = target.read();
	for (size_t i = 0; i < project.working.Size(); i++) {
		project.working[i] *= pack.metresPerUnit;
	}
	project.pixelMetres = glm::dvec2(pack.metresPerTexel);
	if (pack.seaLevel) {
		project.seaLevel = *pack.seaLevel * pack.metresPerUnit;
	}
	project.source = std::string(pack.mode) + " map at (" + std::to_string(pack.worldOrigin.x) + ", " + std::to_string(pack.worldOrigin.y) + ")";
	if (*pressed > 0) {
		const std::string suffix = target.suffix;
		project.source += ", eroded (" + suffix.substr(suffix.find_last_of('_') + 1) + ")";
	}
	if (pack.generation) {
		project.generation = pack.generation();
	}
	return project;
}
