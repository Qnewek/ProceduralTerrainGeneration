#include "ExportManifest.h"

#include <nlohmann/json.hpp>

namespace exportpack {
	using Json = nlohmann::ordered_json;

	namespace {
		std::string FormatName(heightmap::Format format)
		{
			switch (format) {
			case heightmap::Format::PNG16: return "png16";
			case heightmap::Format::R16: return "r16";
			case heightmap::Format::EXR32: return "exr32";
			default: return "unknown";
			}
		}

		//How the values of a height file turn into metres
		Json Encoding(heightmap::Format format, const Manifest& m)
		{
			Json e;
			if (format == heightmap::Format::EXR32) {
				e["type"] = "float32";
				e["channel"] = "Y";
				e["formula"] = "metres = value";
				return e;
			}
			e["type"] = "uint16";
			if (format == heightmap::Format::R16) {
				e["byteOrder"] = "little-endian";
				e["header"] = false;
			}
			e["valueMin"] = m.range.min;
			e["valueMax"] = m.range.max;
			e["metresMin"] = m.range.min * m.metresPerUnit;
			e["metresMax"] = m.range.max * m.metresPerUnit;
			e["formula"] = "metres = metresMin + v / 65535 * (metresMax - metresMin)";
			return e;
		}
	}

	File HeightFile(std::string path, std::string content, heightmap::Format format)
	{
		return File{ std::move(path), std::move(content), FormatName(format), format };
	}

	File Png8File(std::string path, std::string content, int channels)
	{
		const char* layout = channels == 1 ? "grey" : channels == 3 ? "rgb" : "rgba";
		return File{ std::move(path), std::move(content), std::string("png8 ") + layout, std::nullopt };
	}

	UnrealImport Unreal(const Manifest& manifest)
	{
		const float minMetres = manifest.range.min * manifest.metresPerUnit;
		const float maxMetres = manifest.range.max * manifest.metresPerUnit;
		UnrealImport u;
		u.zScale = (maxMetres - minMetres) * 100.0f / 512.0f;
		u.locationZ = (minMetres + maxMetres) * 0.5f * 100.0f;
		return u;
	}

	std::string ToJson(const Manifest& m)
	{
		Json j;
		j["generator"] = "Procedural Terrain Generator";
		j["manifestVersion"] = 1;
		j["name"] = m.name;
		j["mode"] = m.mode;

		Json grid;
		grid["width"] = m.width;
		grid["height"] = m.height;
		grid["metresPerTexel"] = metresPerTexel;
		//Texel centres: the first and the last sample of a row are width - 1 texels apart
		grid["sizeMetres"] = { (m.width - 1) * metresPerTexel, (m.height - 1) * metresPerTexel };
		grid["rowOrder"] = "row 0 first; it is the top edge of the 2D map view (the smallest Z)";
		grid["axes"] = "right-handed, Y up: column = +X (right in the 2D map), row = +Z (down in the 2D map)";
		j["grid"] = grid;

		Json height;
		height["metresPerUnit"] = m.metresPerUnit;
		height["mapMinMetres"] = m.mapValues.min * m.metresPerUnit;
		height["mapMaxMetres"] = m.mapValues.max * m.metresPerUnit;
		j["height"] = height;

		if (m.seaLevel) {
			j["seaLevel"] = { { "value", *m.seaLevel }, { "metres", *m.seaLevel * m.metresPerUnit } };
		}

		Json files = Json::array();
		for (const File& f : m.files) {
			Json file;
			file["path"] = f.path;
			file["content"] = f.content;
			file["format"] = f.format;
			if (f.height) {
				file["encoding"] = Encoding(*f.height, m);
			}
			files.push_back(file);
		}
		j["files"] = files;

		if (!m.materials.empty()) {
			Json materials;
			materials["encoding"] = "share of the material per texel, 0 - 255; the shares of a texel sum to 255";
			Json list = Json::array();
			for (const Material& mat : m.materials) {
				Json entry;
				entry["id"] = mat.id;
				entry["name"] = mat.name;
				entry["splat"] = { { "file", mat.splatFile }, { "channel", std::string(1, "RGBA"[mat.splatChannel & 3]) } };
				entry["weightFile"] = mat.weightFile.empty() ? Json(nullptr) : Json(mat.weightFile);
				entry["coverage"] = mat.coverage;
				list.push_back(entry);
			}
			materials["list"] = list;
			j["materials"] = materials;
		}

		if (!m.meshLevels.empty() || !m.waterMesh.empty()) {
			Json meshes;
			meshes["units"] = "metres, Y up, right-handed (glTF axes): x = map column, z = map row, texel (0, 0) at the origin";
			meshes["uv"] = "u = column / (width - 1), v = row / (height - 1): the maps of the pack as textures (OBJ: v flipped)";
			if (!m.meshLevels.empty()) {
				meshes["tileMetres"] = m.tileMetres > 0.0f ? Json(m.tileMetres) : Json(nullptr);
				meshes["tileNames"] = "terrain_<x>_<z>_LOD<n>, x and z = index of the tile square";
				Json levels = Json::array();
				for (const MeshLevel& level : m.meshLevels) {
					levels.push_back({ { "lod", level.lod }, { "file", level.path }, { "triangles", level.triangles }, { "vertices", level.vertices },
						{ "errorMetres", level.errorMetres }, { "tiles", level.tiles } });
				}
				meshes["levels"] = levels;
			}
			if (!m.waterMesh.empty()) {
				meshes["water"] = { { "file", m.waterMesh }, { "nodes", m.waterNodes } };
			}
			j["meshes"] = meshes;
		}

		if (!m.biomes.empty()) {
			Json biomes;
			biomes["encoding"] = "biome id per texel; colors are the blended biome colors of the app";
			Json list = Json::array();
			for (const Biome& b : m.biomes) {
				list.push_back({ { "id", b.id }, { "name", b.name }, { "color", { b.color[0], b.color[1], b.color[2] } }, { "coverage", b.coverage } });
			}
			biomes["list"] = list;
			j["biomes"] = biomes;
		}

		const UnrealImport unreal = Unreal(m);
		Json import;
		import["unreal"] = {
			{ "files", "png16 or r16" },
			{ "scale", { 100.0f * metresPerTexel, 100.0f * metresPerTexel, unreal.zScale } },
			{ "locationZ", unreal.locationZ },
			{ "note", "Landscape import; Z scale 100 spans 512 m centred on the actor" }
		};
		import["unity"] = {
			{ "files", "r16 (Byte Order: Windows)" },
			{ "terrainSize", { (m.width - 1) * metresPerTexel, (m.range.max - m.range.min) * m.metresPerUnit, (m.height - 1) * metresPerTexel } },
			{ "positionY", m.range.min * m.metresPerUnit },
			{ "note", "Unity needs a square heightmap of 2^n + 1 texels" }
		};
		if (!m.materials.empty()) {
			import["unreal"]["layers"] = "weight files: one 8-bit weightmap per landscape layer (weight-blended)";
			import["unity"]["layers"] = "splat files: alphamaps, 4 terrain layers per RGBA file in material order";
		}
		j["import"] = import;

		if (!m.generation.empty()) {
			Json generation = Json::parse(m.generation, nullptr, false);
			j["generation"] = generation.is_discarded() ? Json(m.generation) : generation;
		}
		return j.dump(2);
	}
}
