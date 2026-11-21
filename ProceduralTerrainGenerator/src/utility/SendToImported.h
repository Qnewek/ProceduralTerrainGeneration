#pragma once

#include <optional>

#include "ExportPanel.h"
#include "MapProject.h"

//"Imported map" section of the Saving tab of a generation mode: a button for each height map of the pack (the map and the
//eroded copies shown). The pressed map is read (only then, it may come from the GPU) and returned as a new project in metres,
//with the sea level, the generation parameters and where it came from; the caller hands it to the Imported map mode.
std::optional<mapproject::Project> SendToImportedImGui(const ExportPanel::Pack& pack);
