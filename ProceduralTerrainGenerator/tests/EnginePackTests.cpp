#include "doctest/doctest.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "EngineExport.h"
#include "ExportManifest.h"
#include "FileIO.h"
#include "HeightmapExport.h"
#include "MaterialExport.h"
#include "MeshExport.h"
#include "VegetationExport.h"

//Sample export packs for checking the engine importers (tools/check_godot_import.bat), written by the same writers as the Saving panel:
//	full    - 257 x 193 texels of 1 m, sea, 3 materials (one with downloaded textures, one without), glTF mesh with water, vegetation with models
//	dem     - 301 x 211 texels of 30 m, no sea, materials without textures, glTF mesh, vegetation without model files
//	minimal - 129 x 129, height R16 only (what Noise heightmap exports without options)
//Run with: ptg_tests --no-skip -tc="Engine sample packs"; they go to out/engine_samples/
namespace {
	const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();

	struct Sample {
		std::string name{}, mode{};
		int width = 0, height = 0;
		float metresPerTexel = 1.0f;
		float metresPerUnit = 64.0f;
		heightmap::ValueRange range = heightmap::terrainRange;
		std::optional<float> seaLevel{};
		bool materials = false, textures = false, meshes = false, vegetation = false, models = false;
	};

	HeightField Hills(int width, int height)
	{
		HeightField map(width, height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				map.At(x, y) = 0.35f + 0.25f * std::sin(x / 23.0f) * std::cos(y / 29.0f) + 0.1f * std::sin((x + 2 * y) / 11.0f);
			}
		}
		return map;
	}

	//Three materials by height: low, middle, high
	splatmap::Shares SharesByHeight(const HeightField& map)
	{
		splatmap::Shares shares;
		shares.width = map.GetWidth();
		shares.height = map.GetHeight();
		shares.channels = 3;
		shares.data.resize(map.Size() * 3);
		for (size_t i = 0; i < map.Size(); i++) {
			const float h = map[i];
			const int m = h < 0.3f ? 0 : h < 0.5f ? 1 : 2;
			shares.data[i * 3 + m] = 255;
		}
		return shares;
	}

	//Models of res/vegetation used by the sample plants; empty if they are not downloaded
	std::vector<vegexport::Model> SampleModels()
	{
		const std::filesystem::path vegetation = sourceRoot / "res" / "vegetation";
		std::ifstream in(vegetation / "vegetation_models.json");
		if (!in) {
			return {};
		}
		const nlohmann::json list = nlohmann::json::parse(in);
		std::vector<vegexport::Model> models;
		for (const auto& entry : list["models"]) {
			const std::string name = entry["name"].get<std::string>();
			if (name != "Deciduous tree" && name != "Shrub" && name != "Rock") {
				continue;
			}
			const std::string asset = entry["asset"].get<std::string>();
			const std::string kind = entry["kind"].get<std::string>();
			const std::vector<uint32_t> lods = entry.contains("lods") ? entry["lods"] : list["lodTriangles"][kind];
			vegexport::Model model{ name, kind, asset, 0, vegetation / "models" / asset / (asset + ".gltf"), lods,
				vegetation / "cache" / (asset + ".mesh") };
			if (!std::filesystem::exists(model.source)) {
				return {};
			}
			models.push_back(std::move(model));
		}
		return models;
	}

	void WriteSample(const Sample& s, const std::filesystem::path& root)
	{
		const std::filesystem::path folder = root / s.name;
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder);
		const HeightField map = Hills(s.width, s.height);

		exportpack::Manifest manifest;
		manifest.name = s.name;
		manifest.mode = s.mode;
		manifest.width = s.width;
		manifest.height = s.height;
		manifest.metresPerTexel = s.metresPerTexel;
		manifest.metresPerUnit = s.metresPerUnit;
		manifest.range = s.range;
		manifest.mapValues = heightmap::MinMax(map);
		manifest.seaLevel = s.seaLevel;
		std::string error;
		size_t clipped = 0;
		const std::string heightFile = heightmap::FileName(s.name + "_height", heightmap::Format::R16, s.width, s.height);
		REQUIRE_MESSAGE(heightmap::Export(folder / heightFile, map, heightmap::Format::R16, s.range, clipped, error, s.metresPerUnit), error);
		manifest.files.push_back(exportpack::HeightFile(heightFile, "height", heightmap::Format::R16));

		if (s.materials) {
			splatmap::Shares shares = SharesByHeight(map);
			splatmap::Normalize(shares);
			const std::vector<float> coverage = splatmap::Coverage(shares);
			const std::string splatFile = s.name + "_splat_0.png";
			REQUIRE_MESSAGE(splatmap::WritePng8(folder / splatFile, splatmap::Rgba(shares, 0), s.width, s.height, 4, error), error);
			manifest.files.push_back(exportpack::Png8File(splatFile, "splat", 4));
			const char* names[] = { "Grass", "Cliff rock", "Snow" };
			std::vector<engineexport::MaterialTextures> sources(3);
			for (int m = 0; m < 3; m++) {
				exportpack::Material material;
				material.id = m;
				material.name = names[m];
				material.splatFile = splatFile;
				material.splatChannel = m;
				material.coverage = coverage[m];
				manifest.materials.push_back(material);
				sources[m].tileMetres = engineexport::TileMetres(4.0f + 2.0f * m, 1.0f, s.metresPerTexel);
				sources[m].fallbackColor[0] = 0.3f + 0.3f * m;
				sources[m].fallbackColor[1] = 0.4f + 0.2f * m;
				sources[m].fallbackColor[2] = 0.15f + 0.4f * m;
			}
			//Only the first material has textures (if downloaded); the others show the fallback color
			sources[0].folder = s.textures ? sourceRoot / "res" / "textures" / "terrain" / "Grass004" : folder / "missing_asset";
			sources[1].folder = folder / "missing_asset";
			sources[2].folder = folder / "missing_asset";
			REQUIRE_MESSAGE(engineexport::CopyMaterialTextures(sources, manifest.materials, folder, manifest.files, error), error);
		}

		if (s.meshes) {
			meshexport::Settings settings;
			settings.name = s.name;
			settings.lodLevels = 2;
			settings.tileMetres = 128.0f * s.metresPerTexel;
			settings.metresPerUnit = s.metresPerUnit;
			settings.metresPerTexel = s.metresPerTexel;
			settings.water = s.seaLevel.has_value();
			settings.seaLevel = s.seaLevel;
			const meshexport::Result meshes = meshexport::Write(map, settings, folder);
			REQUIRE_MESSAGE(meshes.ok, meshes.error);
			manifest.files.insert(manifest.files.end(), meshes.files.begin(), meshes.files.end());
			manifest.meshLevels = meshes.levels;
			manifest.tileMetres = settings.tileMetres;
			manifest.waterMesh = meshes.waterFile;
			manifest.waterNodes = meshes.waterNodes;
		}

		if (s.vegetation) {
			std::vector<vegexport::Model> models = SampleModels();
			if (models.empty()) {
				MESSAGE(s.name, ": vegetation models not downloaded, pack without vegetation");
			}
			else {
				//The variant count the app would load: written once to learn it
				const std::filesystem::path probe = root / "_probe";
				for (vegexport::Model& model : models) {
					const vegexport::ModelFile file = vegexport::WriteModel(model, probe);
					REQUIRE_MESSAGE(!file.path.empty(), model.name, ": ", file.error);
					model.variants = static_cast<int>(file.nodes.size());
				}
				std::filesystem::remove_all(probe);
				vegexport::Input input;
				const biome::Range land{ 0.05f, 1.0f };
				input.plants = { { { "Deciduous tree", 0.4f, 0.8f, 1.2f, 35.0f, land, 1.0f }, { "Shrub", 0.2f, 0.8f, 1.2f, 40.0f, land, 1.0f },
					{ "Rock", 0.1f, 1.0f, 2.0f, 70.0f, land, 0.0f } } };
				input.models = models;
				input.settings.waterHeight = s.seaLevel.value_or(0.0f);
				input.settings.seed = 11;
				input.metresPerUnit = s.metresPerUnit;
				input.metresPerTexel = s.metresPerTexel;
				const BiomeMap ids(s.width, s.height);
				const vegexport::Result vegetation = vegexport::Write(map, {}, ids, input, s.name, s.models, folder);
				REQUIRE_MESSAGE(vegetation.ok, vegetation.error);
				REQUIRE(vegetation.instances > 0);
				manifest.files.insert(manifest.files.end(), vegetation.files.begin(), vegetation.files.end());
				manifest.vegetationInstances = vegetation.instances;
				for (const exportpack::File& file : vegetation.files) {
					if (file.content == "vegetation") {
						manifest.vegetationFile = file.path;
					}
					else {
						manifest.vegetationModels.push_back(file.path);
					}
				}
			}
		}

		REQUIRE_MESSAGE(engineexport::CopyEngineScripts(sourceRoot / "res" / "engine", folder, manifest.files, error), error);
		const std::string json = exportpack::ToJson(manifest);
		REQUIRE(WriteFileAtomic(folder / exportpack::ManifestName(s.name), json.data(), json.size(), error));
		MESSAGE(s.name, ": ", manifest.files.size(), " files, ", manifest.vegetationInstances, " plants");
	}
}

TEST_CASE("Engine sample packs" * doctest::skip())
{
	const std::filesystem::path root = sourceRoot.parent_path() / "out" / "engine_samples";
	std::filesystem::create_directories(root);

	Sample full;
	full.name = "full";
	full.mode = "TerrainGen";
	full.width = 257;
	full.height = 193;
	full.seaLevel = 0.2f;
	full.materials = full.textures = full.meshes = full.vegetation = full.models = true;
	WriteSample(full, root);

	Sample dem;
	dem.name = "dem";
	dem.mode = "Imported map";
	dem.width = 301;
	dem.height = 211;
	dem.metresPerTexel = 30.0f;
	dem.metresPerUnit = 1500.0f;
	dem.materials = dem.meshes = dem.vegetation = true;
	WriteSample(dem, root);

	Sample minimal;
	minimal.name = "minimal";
	minimal.mode = "Noise heightmap";
	minimal.width = 129;
	minimal.height = 129;
	minimal.range = heightmap::noiseRange;
	WriteSample(minimal, root);
}
