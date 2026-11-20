#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ExportManifest.h"

//ImGui section listing the export packs of a folder for "Load from pack". It reads the chosen manifest; the subsystem applies
//its generation parameters and world position and reports the outcome back.
class PackPicker
{
public:
	//@param _folder - folder holding the packs (exports/<name>/<name>.json)
	explicit PackPicker(std::filesystem::path _folder);

	//@return what the chosen pack says, when the user asked to load it in this frame (a pack that cannot be read is reported here)
	std::optional<exportpack::PackSource> ImGuiDraw();

	void ReportLoaded();
	void ReportError(const std::string& message);

private:
	void Refresh();

	std::filesystem::path folder;
	std::vector<std::filesystem::path> packs;
	std::filesystem::path selected;
	bool listed = false;
	std::string status;
	bool statusIsError = false;
};
