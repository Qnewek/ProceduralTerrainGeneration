#include "Preset.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "FileIO.h"

namespace preset {
	namespace {
		//Keeps fields in the order they are written, so files stay easy to read and edit
		using json = nlohmann::ordered_json;

		//Version 2 (2026-11-08): biomes as a table of value ranges instead of discrete levels; older presets are not read
		constexpr int currentVersion = 2;
		constexpr int minVersion = 2;
		constexpr const char* worldKind = "world";
		constexpr const char* noiseKind = "noise";

		constexpr int minMapSize = 2;
		constexpr int maxMapSize = 8192;
		constexpr int maxOctaves = 32;
		constexpr int maxTerraceSteps = 64;
		constexpr size_t maxBiomes = 64;

		//Keys of the biome ranges, in the order of biome::Parameter
		constexpr const char* biomeParameterNames[biome::PARAMETER_COUNT] = { "continentalness", "mountainousness", "weirdness", "temperature", "humidity" };

		template<typename E>
		struct EnumName {
			E value;
			const char* name;
		};

		constexpr EnumName<noise::Options> optionNames[] = {
			{ noise::Options::REFIT_ALL, "REFIT_ALL" },
			{ noise::Options::FLATTEN_NEGATIVES, "FLATTEN_NEGATIVES" },
			{ noise::Options::REVERT_NEGATIVES, "REVERT_NEGATIVES" },
			{ noise::Options::NOTHING, "NOTHING" },
		};

		constexpr EnumName<noise::IslandType> islandTypeNames[] = {
			{ noise::IslandType::CONE, "CONE" },
			{ noise::IslandType::DIAGONAL, "DIAGONAL" },
			{ noise::IslandType::EUCLIDEAN_SQUARED, "EUCLIDEAN_SQUARED" },
			{ noise::IslandType::SQUARE_BUMP, "SQUARE_BUMP" },
			{ noise::IslandType::HYPERBOLOID, "HYPERBOLOID" },
			{ noise::IslandType::SQUIRCLE, "SQUIRCLE" },
			{ noise::IslandType::TRIG, "TRIG" },
		};

		constexpr EnumName<EvaluationMethod> methodNames[] = {
			{ EvaluationMethod::LINEAR_COMBINE, "LINEAR_COMBINE" },
			{ EvaluationMethod::SPLINE_COMBINE, "SPLINE_COMBINE" },
			{ EvaluationMethod::LAYERED, "LAYERED" },
		};

		template<typename E, size_t N>
		const char* NameOf(const EnumName<E>(&names)[N], E value)
		{
			for (const EnumName<E>& entry : names) {
				if (entry.value == value) {
					return entry.name;
				}
			}
			return names[0].name;
		}

		//The shortest decimal that identifies a float: 0.3f is written as 0.3 instead of 0.30000001192092896
		double ReadableFloat(float value)
		{
			char buffer[32];
			std::to_chars_result end = std::to_chars(buffer, buffer + sizeof(buffer), value);
			double result = value;
			std::from_chars(buffer, end.ptr, result);
			return result;
		}

		json FloatArray(const std::vector<float>& values)
		{
			json result = json::array();
			for (float value : values) {
				result.push_back(ReadableFloat(value));
			}
			return result;
		}

		json NoiseToJson(const noise::NoiseConfigParameters& n)
		{
			json j;
			j["seed"] = n.seed;
			j["resolution"] = n.resolution;
			j["xOffset"] = ReadableFloat(n.xoffset);
			j["yOffset"] = ReadableFloat(n.yoffset);
			j["octaves"] = n.octaves;
			j["scale"] = ReadableFloat(n.scale);
			j["contrast"] = ReadableFloat(n.constrast);
			j["redistribution"] = ReadableFloat(n.redistribution);
			j["lacunarity"] = ReadableFloat(n.lacunarity);
			j["persistence"] = ReadableFloat(n.persistance);
			j["negatives"] = NameOf(optionNames, n.option);
			j["revertGain"] = ReadableFloat(n.revertGain);
			j["ridge"] = n.Ridge;
			j["ridgeGain"] = ReadableFloat(n.RidgeGain);
			j["ridgeOffset"] = ReadableFloat(n.RidgeOffset);
			j["island"] = n.island;
			j["islandMixPower"] = ReadableFloat(n.mixPower);
			j["islandType"] = NameOf(islandTypeNames, n.islandType);
			j["symmetrical"] = n.symmetrical;
			j["warpStrength"] = ReadableFloat(n.warpStrength);
			j["warpScale"] = ReadableFloat(n.warpScale);
			j["slopeErosion"] = ReadableFloat(n.slopeErosion);
			j["terraceSteps"] = n.terraceSteps;
			j["terraceSharpness"] = ReadableFloat(n.terraceSharpness);
			return j;
		}

		json SplineToJson(const SplinePoints& s)
		{
			json j;
			j["x"] = s.x;
			j["y"] = s.y;
			j["cubic"] = s.cubic;
			return j;
		}

		json BiomeToJson(const biome::Biome& b)
		{
			json j;
			j["name"] = b.name;
			j["color"] = FloatArray({ b.color.r, b.color.g, b.color.b });
			for (int i = 0; i < biome::PARAMETER_COUNT; i++) {
				j[biomeParameterNames[i]] = FloatArray({ b.ranges[i].min, b.ranges[i].max });
			}
			return j;
		}

		json ErosionToJson(const erosion::ErosionConfig& e)
		{
			json j;
			j["erosionRate"] = ReadableFloat(e.erosionRate);
			j["depositionRate"] = ReadableFloat(e.depositionRate);
			j["evaporationRate"] = ReadableFloat(e.evaporationRate);
			j["gravity"] = ReadableFloat(e.gravity);
			j["inertia"] = ReadableFloat(e.inertia);
			j["minSlope"] = ReadableFloat(e.minSlope);
			j["erosionRadius"] = e.erosionRadius;
			j["blur"] = ReadableFloat(e.blur);
			j["dropletLifetime"] = e.dropletLifetime;
			j["dropletCount"] = e.dropletCount;
			j["initialWater"] = ReadableFloat(e.initialWater);
			j["initialVelocity"] = ReadableFloat(e.initialVelocity);
			j["initialCapacity"] = ReadableFloat(e.initialCapacity);
			return j;
		}

		//Reads fields of one JSON object. A missing field leaves the value untouched, a field of a wrong type throws
		class Reader {
		public:
			Reader(const json& _object, std::string _path) : object(_object), path(std::move(_path))
			{
				if (!object.is_object()) {
					throw std::runtime_error(path + " must be an object");
				}
			}

			const json* Find(const char* key) const
			{
				auto it = object.find(key);
				return it == object.end() ? nullptr : &*it;
			}

			std::string PathOf(const char* key) const { return path + "." + key; }

			void Read(const char* key, int& value) const
			{
				const json* field = Find(key);
				if (!field) {
					return;
				}
				if (!field->is_number_integer() || field->get<double>() < INT_MIN || field->get<double>() > INT_MAX) {
					throw std::runtime_error(PathOf(key) + " must be an integer");
				}
				value = static_cast<int>(field->get<long long>());
			}

			void Read(const char* key, float& value) const
			{
				const json* field = Find(key);
				if (field) {
					value = ToFloat(*field, key);
				}
			}

			void Read(const char* key, bool& value) const
			{
				const json* field = Find(key);
				if (!field) {
					return;
				}
				if (!field->is_boolean()) {
					throw std::runtime_error(PathOf(key) + " must be true or false");
				}
				value = field->get<bool>();
			}

			void Read(const char* key, std::string& value) const
			{
				const json* field = Find(key);
				if (!field) {
					return;
				}
				if (!field->is_string()) {
					throw std::runtime_error(PathOf(key) + " must be a string");
				}
				value = field->get<std::string>();
			}

			//An array of exactly count numbers
			void ReadFixed(const char* key, float* values, size_t count) const
			{
				std::vector<float> read;
				Read(key, read);
				if (!Find(key)) {
					return;
				}
				if (read.size() != count) {
					throw std::runtime_error(PathOf(key) + " must be an array of " + std::to_string(count) + " numbers");
				}
				std::copy(read.begin(), read.end(), values);
			}

			void Read(const char* key, std::vector<float>& values) const
			{
				const json* field = Find(key);
				if (!field) {
					return;
				}
				if (!field->is_array()) {
					throw std::runtime_error(PathOf(key) + " must be an array of numbers");
				}
				values.clear();
				for (const json& element : *field) {
					values.push_back(ToFloat(element, key));
				}
			}

			void Read(const char* key, std::vector<double>& values) const
			{
				const json* field = Find(key);
				if (!field) {
					return;
				}
				if (!field->is_array()) {
					throw std::runtime_error(PathOf(key) + " must be an array of numbers");
				}
				values.clear();
				for (const json& element : *field) {
					if (!element.is_number()) {
						throw std::runtime_error(PathOf(key) + " must be an array of numbers");
					}
					values.push_back(element.get<double>());
				}
			}

			template<typename E, size_t N>
			void ReadEnum(const char* key, const EnumName<E>(&names)[N], E& value) const
			{
				const json* field = Find(key);
				if (!field) {
					return;
				}
				if (field->is_string()) {
					const std::string& text = field->get_ref<const std::string&>();
					for (const EnumName<E>& entry : names) {
						if (text == entry.name) {
							value = entry.value;
							return;
						}
					}
				}
				std::string allowed;
				for (const EnumName<E>& entry : names) {
					allowed += (allowed.empty() ? "" : ", ") + std::string(entry.name);
				}
				throw std::runtime_error(PathOf(key) + " must be one of: " + allowed);
			}

		private:
			float ToFloat(const json& field, const char* key) const
			{
				if (!field.is_number() || std::abs(field.get<double>()) > std::numeric_limits<float>::max()) {
					throw std::runtime_error(PathOf(key) + " must be a number in the float range");
				}
				return static_cast<float>(field.get<double>());
			}

			const json& object;
			std::string path;
		};

		void ReadNoise(const Reader& parent, const char* key, noise::NoiseConfigParameters& n)
		{
			const json* field = parent.Find(key);
			if (!field) {
				return;
			}
			Reader r(*field, parent.PathOf(key));
			r.Read("seed", n.seed);
			r.Read("resolution", n.resolution);
			r.Read("xOffset", n.xoffset);
			r.Read("yOffset", n.yoffset);
			r.Read("octaves", n.octaves);
			r.Read("scale", n.scale);
			r.Read("contrast", n.constrast);
			r.Read("redistribution", n.redistribution);
			r.Read("lacunarity", n.lacunarity);
			r.Read("persistence", n.persistance);
			r.ReadEnum("negatives", optionNames, n.option);
			r.Read("revertGain", n.revertGain);
			r.Read("ridge", n.Ridge);
			r.Read("ridgeGain", n.RidgeGain);
			r.Read("ridgeOffset", n.RidgeOffset);
			r.Read("island", n.island);
			r.Read("islandMixPower", n.mixPower);
			r.ReadEnum("islandType", islandTypeNames, n.islandType);
			r.Read("symmetrical", n.symmetrical);
			//Missing in presets saved before these fields existed; the defaults turn them off
			r.Read("warpStrength", n.warpStrength);
			r.Read("warpScale", n.warpScale);
			r.Read("slopeErosion", n.slopeErosion);
			r.Read("terraceSteps", n.terraceSteps);
			r.Read("terraceSharpness", n.terraceSharpness);
		}

		void ReadSpline(const Reader& parent, const char* key, SplinePoints& s)
		{
			const json* field = parent.Find(key);
			if (!field) {
				return;
			}
			Reader r(*field, parent.PathOf(key));
			r.Read("x", s.x);
			r.Read("y", s.y);
			r.Read("cubic", s.cubic);
		}

		void ReadErosion(const Reader& parent, const char* key, erosion::ErosionConfig& e)
		{
			const json* field = parent.Find(key);
			if (!field) {
				return;
			}
			Reader r(*field, parent.PathOf(key));
			r.Read("erosionRate", e.erosionRate);
			r.Read("depositionRate", e.depositionRate);
			r.Read("evaporationRate", e.evaporationRate);
			r.Read("gravity", e.gravity);
			r.Read("inertia", e.inertia);
			r.Read("minSlope", e.minSlope);
			r.Read("erosionRadius", e.erosionRadius);
			r.Read("blur", e.blur);
			r.Read("dropletLifetime", e.dropletLifetime);
			r.Read("dropletCount", e.dropletCount);
			r.Read("initialWater", e.initialWater);
			r.Read("initialVelocity", e.initialVelocity);
			r.Read("initialCapacity", e.initialCapacity);
		}

		void ReadTerrain(const Reader& parent, const char* key, TerrainConfig& t)
		{
			const json* field = parent.Find(key);
			if (!field) {
				return;
			}
			Reader r(*field, parent.PathOf(key));
			r.ReadEnum("method", methodNames, t.method);
			ReadNoise(r, "continentalness", t.continentalness);
			ReadNoise(r, "mountainousness", t.mountainousness);
			ReadNoise(r, "weirdness", t.weirdness);
			ReadSpline(r, "continentalnessSpline", t.continentalnessSpline);
			ReadSpline(r, "mountainousnessSpline", t.mountainousnessSpline);
			ReadSpline(r, "weirdnessSpline", t.weirdnessSpline);
			//Missing in presets saved before the layered method existed; the defaults are kept
			r.Read("seaLevel", t.seaLevel);
			r.Read("coastWidth", t.coastWidth);
			r.Read("mountainShare", t.mountainShare);
			ReadSpline(r, "peaksSpline", t.peaksSpline);
		}

		void ReadBiomes(const Reader& parent, const char* key, BiomeConfig& b)
		{
			const json* field = parent.Find(key);
			if (!field) {
				return;
			}
			Reader r(*field, parent.PathOf(key));
			ReadNoise(r, "temperature", b.temperature);
			ReadNoise(r, "humidity", b.humidity);
			r.Read("blendWidth", b.blendWidth);

			//A table in the file replaces the default one; fields missing in an entry keep the values of a new Biome
			const json* table = r.Find("table");
			if (!table) {
				return;
			}
			if (!table->is_array()) {
				throw std::runtime_error(r.PathOf("table") + " must be an array of biomes");
			}
			b.table.clear();
			for (size_t index = 0; index < table->size(); index++) {
				const Reader entry((*table)[index], r.PathOf("table") + "[" + std::to_string(index) + "]");
				biome::Biome& biome = b.table.emplace_back();
				entry.Read("name", biome.name);
				float color[3] = { biome.color.r, biome.color.g, biome.color.b };
				entry.ReadFixed("color", color, 3);
				biome.color = glm::vec3(color[0], color[1], color[2]);
				for (int i = 0; i < biome::PARAMETER_COUNT; i++) {
					float range[2] = { biome.ranges[i].min, biome.ranges[i].max };
					entry.ReadFixed(biomeParameterNames[i], range, 2);
					biome.ranges[i] = { range[0], range[1] };
				}
			}
		}

		//The Check functions throw on values generation cannot handle: division by zero in the noise, undefined
		//spline sampling (x not ascending) or smoothstep, an empty biome table or reversed biome ranges
		void CheckSize(int width, int height)
		{
			if (width < minMapSize || width > maxMapSize || height < minMapSize || height > maxMapSize) {
				throw std::runtime_error("Map size must be between " + std::to_string(minMapSize) + " and " + std::to_string(maxMapSize)
					+ " (is " + std::to_string(width) + "x" + std::to_string(height) + ")");
			}
		}

		void CheckNoise(const noise::NoiseConfigParameters& n, const std::string& name)
		{
			if (n.octaves < 1 || n.octaves > maxOctaves) {
				throw std::runtime_error(name + ": octaves must be between 1 and " + std::to_string(maxOctaves));
			}
			if (n.resolution <= 0) {
				throw std::runtime_error(name + ": resolution must be greater than 0");
			}
			if (n.slopeErosion < 0.0f) {
				throw std::runtime_error(name + ": slopeErosion must not be negative");
			}
			if (n.terraceSteps < 0 || n.terraceSteps > maxTerraceSteps) {
				throw std::runtime_error(name + ": terraceSteps must be between 0 and " + std::to_string(maxTerraceSteps));
			}
		}

		void CheckSpline(const SplinePoints& s, const std::string& name)
		{
			if (s.x.size() < 3 || s.x.size() != s.y.size()) {
				throw std::runtime_error(name + " spline: x and y must have the same size, at least 3 points");
			}
			for (size_t i = 0; i < s.x.size(); i++) {
				if (s.x[i] < -1.0 || s.x[i] > 1.0 || s.y[i] < 0.0 || s.y[i] > 1.0) {
					throw std::runtime_error(name + " spline: x must be in [-1, 1] and y in [0, 1]");
				}
				if (i > 0 && s.x[i] <= s.x[i - 1]) {
					throw std::runtime_error(name + " spline: x must be strictly ascending");
				}
			}
		}

		void CheckBiomes(const BiomeConfig& b)
		{
			if (!std::isfinite(b.blendWidth) || b.blendWidth < 0.0f) {
				throw std::runtime_error("Biomes: blendWidth must not be negative");
			}
			if (b.table.empty() || b.table.size() > maxBiomes) {
				throw std::runtime_error("Biomes: the table must have between 1 and " + std::to_string(maxBiomes) + " biomes");
			}
			for (const biome::Biome& biome : b.table) {
				const std::string name = "Biome \"" + biome.name + "\"";
				for (int i = 0; i < 3; i++) {
					if (!(biome.color[i] >= 0.0f && biome.color[i] <= 1.0f)) {
						throw std::runtime_error(name + ": color components must be between 0 and 1");
					}
				}
				for (int i = 0; i < biome::PARAMETER_COUNT; i++) {
					const biome::Range& r = biome.ranges[i];
					if (!std::isfinite(r.min) || !std::isfinite(r.max) || r.min > r.max) {
						throw std::runtime_error(name + ": " + biomeParameterNames[i] + " must be a range [min, max] with min <= max");
					}
				}
			}
		}

		void Check(const WorldConfig& c)
		{
			CheckSize(c.width, c.height);
			CheckNoise(c.terrain.continentalness, "Continentalness");
			CheckNoise(c.terrain.mountainousness, "Mountainousness");
			CheckNoise(c.terrain.weirdness, "Weirdness");
			CheckSpline(c.terrain.continentalnessSpline, "Continentalness");
			CheckSpline(c.terrain.mountainousnessSpline, "Mountainousness");
			CheckSpline(c.terrain.weirdnessSpline, "Weirdness");
			CheckSpline(c.terrain.peaksSpline, "Peaks");
			if (!(c.terrain.seaLevel >= 0.0f && c.terrain.seaLevel <= 1.0f)) {
				throw std::runtime_error("seaLevel must be between 0 and 1");
			}
			//smoothstep in the shader is undefined for edges in the wrong order or equal
			if (!(c.terrain.coastWidth > 0.0f && c.terrain.coastWidth <= 1.0f)) {
				throw std::runtime_error("coastWidth must be greater than 0 and at most 1");
			}
			if (!(c.terrain.mountainShare >= 0.0f && c.terrain.mountainShare <= 1.0f)) {
				throw std::runtime_error("mountainShare must be between 0 and 1");
			}
			CheckNoise(c.biomes.temperature, "Temperature");
			CheckNoise(c.biomes.humidity, "Humidity");
			CheckBiomes(c.biomes);
		}

		void Check(const NoiseMapConfig& c)
		{
			CheckSize(c.width, c.height);
			CheckNoise(c.noise, "Noise");
			if (c.erosion.dropletCount < 0 || c.erosion.dropletLifetime < 0 || c.erosion.erosionRadius < 0) {
				throw std::runtime_error("Erosion: droplet count, droplet lifetime and radius cannot be negative");
			}
		}

		void ReadFields(const Reader& r, WorldConfig& c)
		{
			r.Read("width", c.width);
			r.Read("height", c.height);
			ReadTerrain(r, "terrain", c.terrain);
			ReadBiomes(r, "biomes", c.biomes);
		}

		void ReadFields(const Reader& r, NoiseMapConfig& c)
		{
			r.Read("width", c.width);
			r.Read("height", c.height);
			ReadNoise(r, "noise", c.noise);
			ReadErosion(r, "erosion", c.erosion);
		}

		template<typename Config>
		bool Parse(const std::string& text, const char* kind, Config& out, std::string& error)
		{
			try {
				json root = json::parse(text);
				Reader reader(root, "preset");

				const json* fileKind = reader.Find("kind");
				if (!fileKind || !fileKind->is_string() || fileKind->get_ref<const std::string&>() != kind) {
					throw std::runtime_error(std::string("Not a preset of this mode, expected kind \"") + kind + "\"");
				}
				int version = 0;
				reader.Read("version", version);
				if (version < minVersion || version > currentVersion) {
					throw std::runtime_error("Unsupported preset version " + std::to_string(version));
				}

				Config config;
				ReadFields(reader, config);
				Check(config);
				out = std::move(config);
				return true;
			}
			catch (const std::exception& e) {
				error = e.what();
				return false;
			}
		}

		template<typename Config>
		bool SaveConfig(const std::filesystem::path& path, const Config& config, std::string& error)
		{
			try {
				Check(config);
			}
			catch (const std::exception& e) {
				error = e.what();
				return false;
			}
			const std::string text = ToJson(config) + "\n";
			return WriteFileAtomic(path, text.data(), text.size(), error);
		}

		template<typename Config>
		bool LoadConfig(const std::filesystem::path& path, Config& out, std::string& error)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				error = "Cannot open the file";
				return false;
			}
			std::ostringstream text;
			text << file.rdbuf();
			return FromJson(text.str(), out, error);
		}
	}

	std::string ToJson(const WorldConfig& c)
	{
		json root;
		root["kind"] = worldKind;
		root["version"] = currentVersion;
		root["width"] = c.width;
		root["height"] = c.height;

		json terrain;
		terrain["method"] = NameOf(methodNames, c.terrain.method);
		terrain["continentalness"] = NoiseToJson(c.terrain.continentalness);
		terrain["mountainousness"] = NoiseToJson(c.terrain.mountainousness);
		terrain["weirdness"] = NoiseToJson(c.terrain.weirdness);
		terrain["continentalnessSpline"] = SplineToJson(c.terrain.continentalnessSpline);
		terrain["mountainousnessSpline"] = SplineToJson(c.terrain.mountainousnessSpline);
		terrain["weirdnessSpline"] = SplineToJson(c.terrain.weirdnessSpline);
		terrain["seaLevel"] = ReadableFloat(c.terrain.seaLevel);
		terrain["coastWidth"] = ReadableFloat(c.terrain.coastWidth);
		terrain["mountainShare"] = ReadableFloat(c.terrain.mountainShare);
		terrain["peaksSpline"] = SplineToJson(c.terrain.peaksSpline);
		root["terrain"] = std::move(terrain);

		json biomes;
		biomes["temperature"] = NoiseToJson(c.biomes.temperature);
		biomes["humidity"] = NoiseToJson(c.biomes.humidity);
		biomes["blendWidth"] = ReadableFloat(c.biomes.blendWidth);
		json table = json::array();
		for (const biome::Biome& b : c.biomes.table) {
			table.push_back(BiomeToJson(b));
		}
		biomes["table"] = std::move(table);
		root["biomes"] = std::move(biomes);

		return root.dump(2);
	}

	std::string ToJson(const NoiseMapConfig& c)
	{
		json root;
		root["kind"] = noiseKind;
		root["version"] = currentVersion;
		root["width"] = c.width;
		root["height"] = c.height;
		root["noise"] = NoiseToJson(c.noise);
		root["erosion"] = ErosionToJson(c.erosion);
		return root.dump(2);
	}

	bool FromJson(const std::string& text, WorldConfig& out, std::string& error) { return Parse(text, worldKind, out, error); }
	bool FromJson(const std::string& text, NoiseMapConfig& out, std::string& error) { return Parse(text, noiseKind, out, error); }

	bool Save(const std::filesystem::path& path, const WorldConfig& config, std::string& error) { return SaveConfig(path, config, error); }
	bool Save(const std::filesystem::path& path, const NoiseMapConfig& config, std::string& error) { return SaveConfig(path, config, error); }

	bool Load(const std::filesystem::path& path, WorldConfig& out, std::string& error) { return LoadConfig(path, out, error); }
	bool Load(const std::filesystem::path& path, NoiseMapConfig& out, std::string& error) { return LoadConfig(path, out, error); }

	bool IsValidName(const std::string& name)
	{
		if (name.empty() || name.size() > 64 || name.front() == ' ' || name.back() == ' ') {
			return false;
		}
		return std::all_of(name.begin(), name.end(), [](char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
		});
	}

	std::vector<std::filesystem::path> List(const std::filesystem::path& folder)
	{
		std::vector<std::filesystem::path> presets;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
			if (it->is_regular_file(ec) && it->path().extension() == ".json") {
				presets.push_back(it->path());
			}
		}
		std::sort(presets.begin(), presets.end());
		return presets;
	}
}
