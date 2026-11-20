#include "ExportManifest.h"

#include <nlohmann/json.hpp>

namespace exportpack {
	using Json = nlohmann::ordered_json;

	namespace {
		const char* FormatName(heightmap::Format format)
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
			file["format"] = FormatName(f.format);
			file["encoding"] = Encoding(f.format, m);
			files.push_back(file);
		}
		j["files"] = files;

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
		j["import"] = import;

		if (!m.generation.empty()) {
			Json generation = Json::parse(m.generation, nullptr, false);
			j["generation"] = generation.is_discarded() ? Json(m.generation) : generation;
		}
		return j.dump(2);
	}
}
