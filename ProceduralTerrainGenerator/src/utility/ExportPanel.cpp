#include "ExportPanel.h"

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
	const std::string manifestName = exportpack::ManifestName(name);
	const std::string json = exportpack::ToJson(manifest);
	std::string error;
	if (!WriteFileAtomic(packFolder / manifestName, json.data(), json.size(), error)) {
		SetError(manifestName, error, saved);
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
