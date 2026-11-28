#include "MapProject.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

#include <nlohmann/json.hpp>
#include <zlib.h>

#include "DemTools.h"
#include "FileIO.h"

namespace mapproject {
	namespace {
		using json = nlohmann::json;

		constexpr char signature[8] = { 'P', 'T', 'G', 'M', 'A', 'P', '\x1A', '\n' };
		constexpr size_t lengthBytes = 4;
		constexpr int compressionLevel = 3;
		constexpr const char* workingBlock = "working";
		constexpr const char* originalBlock = "original";
		constexpr const char* paintBlock = "biomePaint";
		constexpr const char* plantsBlock = "placedPlants";
		constexpr const char* plantsDamaged = "The placed plants of the project are damaged";
		//zlib packs at most about 1032 to 1: a block too small for what it should hold is damage, found before allocating for it
		constexpr size_t zlibMaxRatio = 1032;
		constexpr size_t maxPlacedPlants = 10'000'000;
		const size_t maxZlibBytes = std::min<size_t>(static_cast<size_t>(std::numeric_limits<uLong>::max()) / 2, size_t(1) << 30);

		bool CanHold(size_t blockBytes, size_t unpackedBytes)
		{
			if (unpackedBytes <= 64) {
				return true;
			}
			const size_t minBlock = (unpackedBytes - 64 + zlibMaxRatio - 1) / zlibMaxRatio;
			return blockBytes >= minBlock;
		}

		bool CompressBytes(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& out, std::string& error)
		{
			if (bytes.size() > maxZlibBytes) {
				error = "Data is too large to compress";
				return false;
			}
			uLongf size = compressBound(static_cast<uLong>(bytes.size()));
			out.resize(size);
			if (compress2(out.data(), &size, bytes.data(), static_cast<uLong>(bytes.size()), compressionLevel) != Z_OK) {
				error = "Compression failed";
				return false;
			}
			out.resize(size);
			return true;
		}

		//Plants of placedplants::Pack; layer.palette and the plant count come from the header
		bool DecompressPlants(const uint8_t* data, size_t size, size_t count, placedplants::Layer& layer, std::string& error)
		{
			if (count > maxPlacedPlants || size > maxZlibBytes) {
				error = plantsDamaged;
				return false;
			}
			const size_t expected = count * placedplants::plantBytes;
			if (expected > maxZlibBytes || !CanHold(size, expected)) {
				error = plantsDamaged;
				return false;
			}
			std::vector<uint8_t> bytes(expected);
			uLongf unpacked = static_cast<uLongf>(bytes.size());
			if (uncompress(bytes.data(), &unpacked, data, static_cast<uLong>(size)) != Z_OK || unpacked != bytes.size()) {
				error = plantsDamaged;
				return false;
			}
			std::string why;
			if (!placedplants::Unpack(bytes.data(), bytes.size(), layer, why)) {
				error = std::string(plantsDamaged) + ": " + why;
				return false;
			}
			return true;
		}

		bool AllFinite(const HeightField& map)
		{
			return std::all_of(map.Data(), map.Data() + map.Size(), [](float v) { return std::isfinite(v); });
		}

		//Byte k of every float goes to plane k: bytes of the sign and exponent repeat a lot over a smooth map
		std::vector<uint8_t> SplitPlanes(const HeightField& map)
		{
			const size_t count = map.Size();
			const uint8_t* bytes = reinterpret_cast<const uint8_t*>(map.Data());
			std::vector<uint8_t> planes(count * sizeof(float));
			for (size_t i = 0; i < count; i++) {
				for (size_t k = 0; k < sizeof(float); k++) {
					planes[k * count + i] = bytes[i * sizeof(float) + k];
				}
			}
			return planes;
		}

		void JoinPlanes(const std::vector<uint8_t>& planes, HeightField& map)
		{
			const size_t count = map.Size();
			uint8_t* bytes = reinterpret_cast<uint8_t*>(map.Data());
			for (size_t i = 0; i < count; i++) {
				for (size_t k = 0; k < sizeof(float); k++) {
					bytes[i * sizeof(float) + k] = planes[k * count + i];
				}
			}
		}

		bool Compress(const HeightField& map, std::vector<uint8_t>& out, std::string& error)
		{
			if (map.Size() > maxZlibBytes / sizeof(float)) {
				error = "The map is too large to compress";
				return false;
			}
			const std::vector<uint8_t> planes = SplitPlanes(map);
			uLongf size = compressBound(static_cast<uLong>(planes.size()));
			out.resize(size);
			if (compress2(out.data(), &size, planes.data(), static_cast<uLong>(planes.size()), compressionLevel) != Z_OK) {
				error = "Compression failed";
				return false;
			}
			out.resize(size);
			return true;
		}

		bool Decompress(const uint8_t* data, size_t size, HeightField& map, std::string& error)
		{
			if (size > maxZlibBytes || map.Size() > maxZlibBytes / sizeof(float)) {
				error = "The map data is damaged";
				return false;
			}
			std::vector<uint8_t> planes(map.Size() * sizeof(float));
			uLongf unpacked = static_cast<uLongf>(planes.size());
			if (uncompress(planes.data(), &unpacked, data, static_cast<uLong>(size)) != Z_OK || unpacked != planes.size()) {
				error = "The map data is damaged";
				return false;
			}
			JoinPlanes(planes, map);
			if (!AllFinite(map)) {
				error = "The map data is damaged (not finite heights)";
				return false;
			}
			return true;
		}

		//Texels of painted biomes as little-endian bytes
		bool CompressPaint(const biomepaint::Layer& paint, std::vector<uint8_t>& out, std::string& error)
		{
			if (paint.texels.size() > maxZlibBytes / 2) {
				error = "The biome paint data is too large to compress";
				return false;
			}
			std::vector<uint8_t> bytes(paint.texels.size() * 2);
			for (size_t i = 0; i < paint.texels.size(); i++) {
				bytes[2 * i] = static_cast<uint8_t>(paint.texels[i] & 0xFF);
				bytes[2 * i + 1] = static_cast<uint8_t>(paint.texels[i] >> 8);
			}
			uLongf size = compressBound(static_cast<uLong>(bytes.size()));
			out.resize(size);
			if (compress2(out.data(), &size, bytes.data(), static_cast<uLong>(bytes.size()), compressionLevel) != Z_OK) {
				error = "Compression failed";
				return false;
			}
			out.resize(size);
			return true;
		}

		bool DecompressPaint(const uint8_t* data, size_t size, biomepaint::Layer& paint, std::string& error)
		{
			if (size > maxZlibBytes || paint.texels.size() > maxZlibBytes / 2) {
				error = "The biome paint data is damaged";
				return false;
			}
			std::vector<uint8_t> bytes(paint.texels.size() * 2);
			uLongf unpacked = static_cast<uLongf>(bytes.size());
			if (uncompress(bytes.data(), &unpacked, data, static_cast<uLong>(size)) != Z_OK || unpacked != bytes.size()) {
				error = "The biome paint data is damaged";
				return false;
			}
			const size_t slots = paint.palette.size();
			for (size_t i = 0; i < paint.texels.size(); i++) {
				paint.texels[i] = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
				if ((paint.texels[i] & 0xFF) > slots) {
					error = "The biome paint data is damaged (a biome without a name)";
					return false;
				}
			}
			return true;
		}

		std::string LowercaseExtension(const std::filesystem::path& path)
		{
			const std::u8string text = path.extension().u8string();
			std::string lowercase(text.begin(), text.end());
			std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return lowercase;
		}
	}

	bool Serialize(const Project& project, std::vector<uint8_t>& out, std::string& error)
	{
		const HeightField& working = project.working;
		if (working.Empty()) {
			error = "The project has no map";
			return false;
		}
		if (working.GetWidth() > dem::maxMapSize || working.GetHeight() > dem::maxMapSize) {
			error = "The map is larger than " + std::to_string(dem::maxMapSize) + " pixels on a side";
			return false;
		}
		const bool hasOriginal = !project.original.Empty();
		if (hasOriginal && (project.original.GetWidth() != working.GetWidth() || project.original.GetHeight() != working.GetHeight())) {
			error = "The original map has another size than the working copy";
			return false;
		}
		if (!AllFinite(working) || (hasOriginal && !AllFinite(project.original))) {
			error = "The map has heights that are not finite";
			return false;
		}

		const bool hasPaint = !project.paint.Empty();
		if (hasPaint && (project.paint.width != working.GetWidth() || project.paint.height != working.GetHeight()
			|| project.paint.texels.size() != working.Size())) {
			error = "The painted biomes have another size than the map";
			return false;
		}
		if (hasPaint && static_cast<int>(project.paint.palette.size()) > biomepaint::maxPalette) {
			error = "Too many painted biomes";
			return false;
		}

		const placedplants::Layer& plants = project.placedPlants;
		const bool hasPlants = !plants.plants.empty();
		if (hasPlants && (plants.plants.size() > maxPlacedPlants || static_cast<int>(plants.palette.size()) > placedplants::maxPalette
			|| std::any_of(plants.plants.begin(), plants.plants.end(), [&](const placedplants::Plant& p) { return p.model >= plants.palette.size(); }))) {
			error = plants.plants.size() > maxPlacedPlants ? "Too many placed plants in the project" : "A placed plant names a model the project does not list";
			return false;
		}

		std::vector<uint8_t> workingData, originalData, paintData, plantsData;
		if (!Compress(working, workingData, error) || (hasOriginal && !Compress(project.original, originalData, error))
			|| (hasPaint && !CompressPaint(project.paint, paintData, error)) || (hasPlants && !CompressBytes(placedplants::Pack(plants), plantsData, error))) {
			return false;
		}

		json header;
		header["version"] = currentVersion;
		header["width"] = working.GetWidth();
		header["height"] = working.GetHeight();
		header["pixelMetres"] = { project.pixelMetres.x, project.pixelMetres.y };
		if (project.seaLevel) {
			header["seaLevel"] = *project.seaLevel;
		}
		header["source"] = project.source;
		header["generation"] = project.generation;
		header["verticalExaggeration"] = project.verticalExaggeration;
		if (!project.rivers.empty()) {
			header["rivers"] = project.rivers;
		}
		if (!project.biomes.empty()) {
			header["biomes"] = project.biomes;
		}
		if (hasPaint) {
			header["biomePaint"] = { { "palette", project.paint.palette } };
		}
		if (const std::optional<geo::GeoReference>& g = project.georeference) {
			header["georeference"] = { { "epsg", g->epsg }, { "origin", { g->origin.x, g->origin.y } }, { "pixel", { g->pixel.x, g->pixel.y } } };
		}
		header["vegetation"] = { { "enabled", project.vegetation.enabled }, { "density", project.vegetation.density } };
		if (hasPlants) {
			header["placedPlants"] = { { "palette", plants.palette }, { "count", plants.plants.size() } };
		}
		header["blocks"] = json::array();
		header["blocks"].push_back({ { "name", workingBlock }, { "bytes", workingData.size() } });
		if (hasOriginal) {
			header["blocks"].push_back({ { "name", originalBlock }, { "bytes", originalData.size() } });
		}
		if (hasPaint) {
			header["blocks"].push_back({ { "name", paintBlock }, { "bytes", paintData.size() } });
		}
		if (hasPlants) {
			header["blocks"].push_back({ { "name", plantsBlock }, { "bytes", plantsData.size() } });
		}
		//Text that is not valid UTF-8 (a file name in the system code page) is kept with replaced characters
		const std::string text = header.dump(-1, ' ', false, json::error_handler_t::replace);
		if (text.size() > std::numeric_limits<uint32_t>::max()) {
			error = "The project header is too large";
			return false;
		}

		out.clear();
		out.reserve(sizeof(signature) + lengthBytes + text.size() + workingData.size() + originalData.size() + paintData.size() + plantsData.size());
		out.insert(out.end(), std::begin(signature), std::end(signature));
		const uint32_t length = static_cast<uint32_t>(text.size());
		for (size_t i = 0; i < lengthBytes; i++) {
			out.push_back(static_cast<uint8_t>(length >> (8 * i)));
		}
		out.insert(out.end(), text.begin(), text.end());
		out.insert(out.end(), workingData.begin(), workingData.end());
		out.insert(out.end(), originalData.begin(), originalData.end());
		out.insert(out.end(), paintData.begin(), paintData.end());
		out.insert(out.end(), plantsData.begin(), plantsData.end());
		return true;
	}

	bool Deserialize(const uint8_t* data, size_t size, Project& out, std::string& error)
	{
		const size_t prefix = sizeof(signature) + lengthBytes;
		if (size < prefix || std::memcmp(data, signature, sizeof(signature)) != 0) {
			error = "Not a project file";
			return false;
		}
		uint32_t length = 0;
		for (size_t i = 0; i < lengthBytes; i++) {
			length |= static_cast<uint32_t>(data[sizeof(signature) + i]) << (8 * i);
		}
		if (length > size - prefix) {
			error = "The project file is cut short";
			return false;
		}

		Project project;
		//map = nullptr: the block of painted biomes, or of the placed plants when plants is set
		struct Block { HeightField* map; size_t bytes; bool plants = false; };
		std::vector<Block> blocks;
		size_t plantCount = 0;
		bool hasPlantsBlock = false;
		try {
			const json header = json::parse(data + prefix, data + prefix + length);
			const int version = header.at("version").get<int>();
			if (version > currentVersion) {
				error = "The project was made by a newer version of the app (version " + std::to_string(version) + ")";
				return false;
			}
			if (version < 1) {
				error = "Unknown project version " + std::to_string(version);
				return false;
			}
			const int width = header.at("width").get<int>();
			const int height = header.at("height").get<int>();
			if (width < 1 || height < 1 || width > dem::maxMapSize || height > dem::maxMapSize) {
				error = "Wrong map size " + std::to_string(width) + "x" + std::to_string(height);
				return false;
			}
			const json& pixel = header.at("pixelMetres");
			project.pixelMetres = glm::dvec2(pixel.at(0).get<double>(), pixel.at(1).get<double>());
			if (!(project.pixelMetres.x > 0.0) || !(project.pixelMetres.y > 0.0) || !std::isfinite(project.pixelMetres.x) || !std::isfinite(project.pixelMetres.y)) {
				error = "Wrong pixel size";
				return false;
			}
			if (header.contains("seaLevel")) {
				project.seaLevel = header.at("seaLevel").get<float>();
				if (!std::isfinite(*project.seaLevel)) {
					error = "The sea level of the project is damaged";
					return false;
				}
			}
			project.source = header.value("source", std::string());
			project.generation = header.value("generation", std::string());
			//Version 2: river settings; version 1 has none
			project.rivers = header.value("rivers", std::string());
			//Version 3: biome settings; older versions have none
			project.biomes = header.value("biomes", std::string());
			//Version 4: painted biomes; the palette here, the texels in their block
			std::vector<std::string> palette;
			if (version >= 4 && header.contains("biomePaint")) {
				palette = header.at("biomePaint").at("palette").get<std::vector<std::string>>();
				if (static_cast<int>(palette.size()) > biomepaint::maxPalette) {
					error = "The project has too many painted biomes";
					return false;
				}
			}
			project.verticalExaggeration = header.value("verticalExaggeration", 1.0f);
			if (!(project.verticalExaggeration > 0.0f) || !std::isfinite(project.verticalExaggeration)) {
				project.verticalExaggeration = 1.0f;
			}
			//Version 5: georeference
			if (version >= 5 && header.contains("georeference")) {
				const json& g = header.at("georeference");
				geo::GeoReference reference;
				reference.epsg = g.at("epsg").get<int>();
				reference.origin = glm::dvec2(g.at("origin").at(0).get<double>(), g.at("origin").at(1).get<double>());
				reference.pixel = glm::dvec2(g.at("pixel").at(0).get<double>(), g.at("pixel").at(1).get<double>());
				if (!std::isfinite(reference.origin.x) || !std::isfinite(reference.origin.y) || !std::isfinite(reference.pixel.x)
					|| !std::isfinite(reference.pixel.y) || reference.pixel.x == 0.0 || reference.pixel.y == 0.0) {
					error = "The georeference of the project is damaged";
					return false;
				}
				project.georeference = reference;
			}
			//Version 6: vegetation settings; older versions get the defaults (plants on, density 1)
			if (version >= 6 && header.contains("vegetation")) {
				const json& v = header.at("vegetation");
				project.vegetation.enabled = v.at("enabled").get<bool>();
				project.vegetation.density = v.at("density").get<float>();
				if (!std::isfinite(project.vegetation.density) || project.vegetation.density < 0.0f || project.vegetation.density > maxVegetationDensity) {
					error = "The vegetation settings of the project are damaged";
					return false;
				}
			}

			//Version 7: plants placed by hand; the palette and the count here, the plants in their block
			if (version >= 7 && header.contains("placedPlants")) {
				const json& p = header.at("placedPlants");
				project.placedPlants.palette = p.at("palette").get<std::vector<std::string>>();
				plantCount = p.at("count").get<size_t>();
				if (static_cast<int>(project.placedPlants.palette.size()) > placedplants::maxPalette || plantCount > maxPlacedPlants) {
					error = plantsDamaged;
					return false;
				}
			}

			//Every block in the file and able to hold its map, checked before anything of the map size is allocated
			size_t blockTotal = 0;
			const size_t texels = static_cast<size_t>(width) * height;
			for (const json& block : header.at("blocks")) {
				const std::string name = block.at("name").get<std::string>();
				const size_t bytes = block.at("bytes").get<size_t>();
				if (bytes > size - prefix - length - blockTotal) {
					error = "The project file is cut short";
					return false;
				}
				blockTotal += bytes;
				const size_t needed = name == paintBlock ? texels * 2
					: name == plantsBlock ? plantCount * placedplants::plantBytes
					: (name == workingBlock || name == originalBlock ? texels * sizeof(float) : 0);
				if (!CanHold(bytes, needed)) {
					error = "The project block \"" + name + "\" is too small for a " + std::to_string(width) + " x " + std::to_string(height) + " map";
					return false;
				}
			}
			for (const json& block : header.at("blocks")) {
				const std::string name = block.at("name").get<std::string>();
				const size_t bytes = block.at("bytes").get<size_t>();
				if (name == plantsBlock && version >= 7) {
					if (hasPlantsBlock) {
						error = "Repeated block \"" + name + "\" in the project";
						return false;
					}
					hasPlantsBlock = true;
					blocks.push_back({ nullptr, bytes, true });
					continue;
				}
				if (name == paintBlock && version >= 4) {
					if (!project.paint.texels.empty()) {
						error = "Repeated block \"" + name + "\" in the project";
						return false;
					}
					project.paint = biomepaint::Make(width, height);
					project.paint.palette = palette;
					blocks.push_back({ nullptr, bytes });
					continue;
				}
				HeightField* map = name == workingBlock ? &project.working : name == originalBlock ? &project.original : nullptr;
				if (!map || !map->Empty()) {
					error = "Unknown or repeated block \"" + name + "\" in the project";
					return false;
				}
				map->Resize(width, height);
				blocks.push_back({ map, bytes });
			}
		}
		catch (const json::exception& e) {
			error = std::string("The project header is damaged: ") + e.what();
			return false;
		}
		catch (const std::bad_alloc&) {
			error = "Not enough memory for the project";
			return false;
		}
		if (project.working.Empty()) {
			error = "The project has no map";
			return false;
		}
		if (plantCount > 0 && !hasPlantsBlock) {
			error = plantsDamaged;
			return false;
		}

		size_t offset = prefix + length;
		for (const Block& block : blocks) {
			if (block.bytes > size - offset) {
				error = "The project file is cut short";
				return false;
			}
			bool read = false;
			try {
				read = block.map ? Decompress(data + offset, block.bytes, *block.map, error)
					: block.plants ? DecompressPlants(data + offset, block.bytes, plantCount, project.placedPlants, error)
					: DecompressPaint(data + offset, block.bytes, project.paint, error);
			}
			catch (const std::bad_alloc&) {
				error = "Not enough memory for the project";
			}
			if (!read) {
				return false;
			}
			offset += block.bytes;
		}
		out = std::move(project);
		return true;
	}

	bool Save(const std::filesystem::path& path, const Project& project, std::string& error)
	{
		std::vector<uint8_t> bytes;
		return Serialize(project, bytes, error) && WriteFileAtomic(path, bytes.data(), bytes.size(), error);
	}

	bool Load(const std::filesystem::path& path, Project& out, std::string& error)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			error = "Cannot open the file";
			return false;
		}
		const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (file.bad()) {
			error = "Cannot read the file";
			return false;
		}
		return Deserialize(bytes.data(), bytes.size(), out, error);
	}

	std::filesystem::path WithExtension(const std::filesystem::path& path)
	{
		if (LowercaseExtension(path) == extension) {
			return path;
		}
		std::filesystem::path result = path;
		result += extension;
		return result;
	}

	std::vector<std::filesystem::path> ListProjects(const std::filesystem::path& folder)
	{
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
			if (it->is_regular_file(ec) && LowercaseExtension(it->path()) == extension) {
				files.push_back(it->path());
			}
		}
		std::sort(files.begin(), files.end());
		return files;
	}
}
