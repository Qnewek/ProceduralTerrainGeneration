#include "ExportPanel.h"

#include <algorithm>
#include <cstdio>
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

void ExportPanel::SetError(const std::string& fileName, const std::string& error, const std::string& saved)
{
	status = "Error (" + fileName + "): " + error;
	if (!saved.empty()) {
		status += "\nSaved before the error: " + saved;
	}
	statusIsError = true;
}

bool ExportPanel::WritePng(const std::filesystem::path& packFolder, const exportpack::File& file, const std::vector<uint8_t>& pixels, int width, int height,
	int channels, exportpack::Manifest& manifest, std::string& saved)
{
	std::string error;
	if (!splatmap::WritePng8(packFolder / file.path, pixels, width, height, channels, error)) {
		SetError(file.path, error, saved);
		return false;
	}
	manifest.files.push_back(file);
	saved += (saved.empty() ? "" : ", ") + file.path;
	return true;
}

bool ExportPanel::ExportMaterials(const MaterialSource& source, const std::filesystem::path& packFolder, exportpack::Manifest& manifest, std::string& saved)
{
	splatmap::Shares shares = source.read();
	splatmap::Normalize(shares);
	const std::vector<float> coverage = splatmap::Coverage(shares);
	const int count = shares.channels;

	//Unity alphamaps: 4 materials per RGBA file
	for (int first = 0; first < count; first += 4) {
		const std::string file = std::string(name) + "_splat_" + std::to_string(first / 4) + ".png";
		if (!WritePng(packFolder, exportpack::Png8File(file, "splat", 4), splatmap::Rgba(shares, first), shares.width, shares.height, 4, manifest, saved)) {
			return false;
		}
	}
	//A weight map for every material that is on the map
	for (int m = 0; m < count; m++) {
		exportpack::Material material;
		material.id = m;
		material.name = m < static_cast<int>(source.names.size()) ? source.names[m] : "material " + std::to_string(m);
		material.splatFile = std::string(name) + "_splat_" + std::to_string(m / 4) + ".png";
		material.splatChannel = m % 4;
		material.coverage = coverage[m];
		if (coverage[m] > 0.0f) {
			char id[8];
			std::snprintf(id, sizeof(id), "%02d", m);
			material.weightFile = std::string(name) + "_weight_" + id + "_" + splatmap::SafeName(material.name) + ".png";
			if (!WritePng(packFolder, exportpack::Png8File(material.weightFile, "weight", 1), splatmap::Channel(shares, m), shares.width, shares.height, 1,
				manifest, saved)) {
				return false;
			}
		}
		manifest.materials.push_back(material);
	}
	return true;
}

bool ExportPanel::ExportBiomes(const BiomeSource& source, const std::filesystem::path& packFolder, exportpack::Manifest& manifest, std::string& saved)
{
	const BiomeMap ids = source.readIds();
	const std::vector<float> colors = source.readColors();
	const int width = ids.GetWidth(), height = ids.GetHeight();
	if (!WritePng(packFolder, exportpack::Png8File(std::string(name) + "_biome_ids.png", "biome_ids", 1), splatmap::BiomeIds(ids), width, height, 1,
		manifest, saved)) {
		return false;
	}
	if (colors.size() == ids.Size() * 4 && !WritePng(packFolder, exportpack::Png8File(std::string(name) + "_biome_colors.png", "biome_colors", 3),
		splatmap::Colors(colors.data(), ids.Size()), width, height, 3, manifest, saved)) {
		return false;
	}
	const std::vector<float> coverage = splatmap::BiomeCoverage(ids, static_cast<int>(source.table.size()));
	for (size_t b = 0; b < source.table.size(); b++) {
		exportpack::Biome biome = source.table[b];
		biome.coverage = coverage[b];
		manifest.biomes.push_back(biome);
	}
	return true;
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
	HeightField mainMap;
	for (size_t i = 0; i < pack.heightmaps.size(); i++) {
		const Target& target = pack.heightmaps[i];
		const std::string fileName = HeightFileName(target);
		const HeightField map = target.read();
		if (i == 0) {
			manifest.width = map.GetWidth();
			manifest.height = map.GetHeight();
			manifest.mapValues = heightmap::MinMax(map);
			mainMap = map;
		}
		size_t mapClipped = 0;
		std::string error;
		if (!heightmap::Export(packFolder / fileName, map, format, pack.range, mapClipped, error, pack.metresPerUnit)) {
			SetError(fileName, error, saved);
			return;
		}
		manifest.files.push_back(exportpack::HeightFile(fileName, std::string("height") + target.suffix, format));
		saved += (saved.empty() ? "" : ", ") + fileName;
		clipped += mapClipped;
	}

	if (exportMaterials && pack.materials && !ExportMaterials(*pack.materials, packFolder, manifest, saved)) {
		return;
	}
	if (exportBiomes && pack.biomes && !ExportBiomes(*pack.biomes, packFolder, manifest, saved)) {
		return;
	}

	if (pack.generation) {
		manifest.generation = pack.generation();
	}
	pending = Pending{ packFolder, std::move(manifest), saved, clipped, format };
	if (meshFormat == 0) {
		Finish();
		return;
	}

	//The meshes on a background thread, from copies of everything they need
	meshexport::Settings settings;
	settings.name = name;
	settings.format = static_cast<meshexport::Format>(meshFormat - 1);
	settings.lodLevels = lodLevels;
	settings.tileMetres = tileSizes[tileChoice];
	settings.metresPerUnit = pack.metresPerUnit;
	for (const exportpack::File& file : pending->manifest.files) {
		if (file.content == "biome_colors") {
			settings.terrainTexture = file.path;
		}
	}
	settings.water = exportWater;
	if (exportWater) {
		settings.seaLevel = pack.seaLevel;
		if (pack.riverWater) {
			settings.riverWater = pack.riverWater();
		}
	}
	pending->manifest.tileMetres = std::max(settings.tileMetres, 0.0f);
	meshJob.Start([map = std::move(mainMap), settings = std::move(settings), packFolder]() {
		return meshexport::Write(map, settings, packFolder);
	});
	status = "Saved: " + saved + "\nBuilding the meshes...";
	statusIsError = false;
}

void ExportPanel::PollMeshes()
{
	std::optional<meshexport::Result> result = meshJob.TryTake();
	if (!result || !pending) {
		return;
	}
	if (!result->ok) {
		SetError("meshes", result->error, pending->saved);
		pending.reset();
		return;
	}
	exportpack::Manifest& manifest = pending->manifest;
	for (const exportpack::File& file : result->files) {
		manifest.files.push_back(file);
		pending->saved += ", " + file.path;
	}
	manifest.meshLevels = result->levels;
	manifest.waterMesh = result->waterFile;
	manifest.waterNodes = result->waterNodes;
	Finish();
	char time[64];
	std::snprintf(time, sizeof(time), "\nMeshes built in %.1f s", result->ms / 1000.0);
	status += time;
}

void ExportPanel::Finish()
{
	Pending& p = *pending;
	const std::string manifestName = exportpack::ManifestName(p.manifest.name);
	const std::string json = exportpack::ToJson(p.manifest);
	std::string error;
	if (!WriteFileAtomic(p.folder / manifestName, json.data(), json.size(), error)) {
		SetError(manifestName, error, p.saved);
		pending.reset();
		return;
	}
	p.saved += ", " + manifestName;

	std::error_code ec;
	std::u8string location = std::filesystem::absolute(p.folder, ec).u8string();
	status = "Saved: " + p.saved + "\nFolder: " + std::string(location.begin(), location.end());
	if (p.clipped > 0) {
		status += p.format == heightmap::Format::EXR32 ? "\nInvalid (non-finite) cells replaced: " : "\nCells outside the value range were clipped: ";
		status += std::to_string(p.clipped);
	}
	statusIsError = false;
	pending.reset();
}

void ExportPanel::ImGuiDraw(const Pack& pack)
{
	PollMeshes();
	const bool busy = meshJob.IsRunning();
	ImGui::BeginDisabled(busy);

	int selectedFormat = static_cast<int>(format);
	if (ImGui::Combo("Height format", &selectedFormat, "PNG 16-bit\0RAW 16-bit (R16)\0EXR 32-bit float (metres)\0")) {
		format = static_cast<heightmap::Format>(selectedFormat);
	}
	ImGui::SetItemTooltip("PNG / R16: Unreal and Unity landscapes (16-bit, fixed value range).\n"
		"EXR: exact heights in metres, e.g. for Godot, Blender or Houdini.");

	if (pack.materials || pack.biomes) {
		ImGui::BeginDisabled(!pack.materials);
		ImGui::Checkbox("Material maps", &exportMaterials);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Shares of the surface materials, 8-bit PNG:\n"
			"_splat_N: 4 materials per RGBA file (Unity alphamaps)\n"
			"_weight_ID_name: one greyscale map per material on the map (Unreal layers, Godot)");
		ImGui::BeginDisabled(!pack.biomes);
		ImGui::Checkbox("Biome maps", &exportBiomes);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("_biome_ids: biome id per texel (8-bit greyscale), names in the .json\n_biome_colors: the biome colors of the app (RGB)");
	}
	else if (pack.layersMissing) {
		ImGui::TextDisabled("%s", pack.layersMissing);
	}

	ImGui::Combo("Terrain mesh", &meshFormat, "None\0glTF binary (.glb)\0OBJ\0");
	ImGui::SetItemTooltip("Terrain as triangles in metres (Y up): one file per level of detail, tiles as separate objects.\n"
		"glTF: one compact file, colored with the biome colors when biome maps are exported.\nOBJ: plain text, large files.");
	if (meshFormat != 0) {
		ImGui::SliderInt("Levels of detail", &lodLevels, 1, 5);
		ImGui::SetItemTooltip("LOD 0 = every texel; each next level has about 4 times fewer triangles (simplified where the terrain is flat).\n"
			"The largest height error of every level is in the .json.");
		ImGui::Combo("Tile size", &tileChoice, "128 m\0" "256 m\0" "512 m\0" "1024 m\0" "Whole map\0");
		const bool anyWater = pack.seaLevel.has_value() || static_cast<bool>(pack.riverWater);
		ImGui::BeginDisabled(!anyWater);
		ImGui::Checkbox("Water", &exportWater);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(anyWater ? "_water file: the sea surface over the map, rivers and lakes (Region method)" : "This map has no sea, rivers or lakes");
	}

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
		if (exportMaterials && pack.materials) {
			files += ", material maps";
		}
		if (exportBiomes && pack.biomes) {
			files += ", biome maps";
		}
		if (meshFormat != 0) {
			files += ", " + std::to_string(lodLevels) + " terrain mesh file" + (lodLevels > 1 ? "s" : "");
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
	//The whole section stays disabled while the meshes are built
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
