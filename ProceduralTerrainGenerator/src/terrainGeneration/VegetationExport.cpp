#include "VegetationExport.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <system_error>

#include <lodepng.h>
#include <nlohmann/json.hpp>

#include "stb_image.h"

#include "FileIO.h"
#include "MeshFile.h"
#include "TerrainMesh.h"
#include "VegetationMesh.h"

namespace vegexport {
	using Json = nlohmann::ordered_json;
	//Vegetation grows only on generated maps, where a texel is 1 m (as the app places it: TerrainView::worldUnitsPerTexel world units)
	constexpr float metresPerTexel = 1.0f;

	std::vector<Placed> PlaceMap(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input)
	{
		std::vector<Placed> placed;
		const int width = height.GetWidth(), mapHeight = height.GetHeight();
		if (width < 2 || mapHeight < 2 || surface.size() != height.Size() || ids.GetWidth() != width || ids.GetHeight() != mapHeight) {
			return placed;
		}
		std::vector<vegetation::ModelInfo> infos;
		for (const Model& m : input.models) {
			infos.push_back({ m.name, m.variants, vegetation::LayerOfKind(m.kind) });
		}

		//Maps of a chunk with a margin of one texel, as ChunkWorld reads them back (bilinear heights near the edge)
		constexpr int size = chunkTexels + 3;
		std::vector<float> chunkHeight(static_cast<size_t>(size) * size);
		std::vector<glm::vec4> chunkSurface(chunkHeight.size());
		std::vector<int> chunkIds(chunkHeight.size());
		std::vector<vegetation::Instance> instances;
		//Chunks of the world grid (cells are aligned to it), map texel t = world map position mapTexel + t
		auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
		const glm::ivec2 offset = input.mapTexel;
		const glm::ivec2 firstChunk(floorDiv(offset.x, chunkTexels), floorDiv(offset.y, chunkTexels));
		const glm::ivec2 lastChunk(floorDiv(offset.x + width - 1, chunkTexels), floorDiv(offset.y + mapHeight - 1, chunkTexels));
		for (int cy = firstChunk.y; cy <= lastChunk.y; cy++) {
			for (int cx = firstChunk.x; cx <= lastChunk.x; cx++) {
				const glm::ivec2 chunkTexel(cx * chunkTexels, cy * chunkTexels);
				const glm::ivec2 first = chunkTexel - 1;
				for (int y = 0; y < size; y++) {
					const int my = std::clamp(first.y + y - offset.y, 0, mapHeight - 1);
					for (int x = 0; x < size; x++) {
						const int mx = std::clamp(first.x + x - offset.x, 0, width - 1);
						const size_t from = static_cast<size_t>(my) * width + mx;
						const size_t to = static_cast<size_t>(y) * size + x;
						chunkHeight[to] = height[from];
						chunkSurface[to] = surface[from];
						chunkIds[to] = ids[from];
					}
				}
				vegetation::ChunkMaps maps;
				maps.size = size;
				maps.firstTexel = first;
				maps.step = 1;
				maps.height = chunkHeight.data();
				maps.surface = chunkSurface.data();
				maps.ids = chunkIds.data();

				vegetation::PlacementSettings settings = input.settings;
				settings.chunkTexel = chunkTexel;
				settings.chunkTexels = chunkTexels;
				//Positions straight in map texels (= metres); the slope comes from the surface map, not from these
				settings.mapOrigin = glm::vec2(0.0f);
				settings.worldUnitsPerTexel = 1.0f;
				settings.rivers = input.rivers.get();
				settings.layerMask = input.layerMask;
				instances.clear();
				vegetation::PlaceChunk(maps, input.plants, infos, settings, instances);
				for (const vegetation::Instance& in : instances) {
					//Map texels, between the centres of the edge texels
					const float x = in.position.x - static_cast<float>(offset.x), z = in.position.z - static_cast<float>(offset.y);
					if (x < 0.0f || z < 0.0f || x > static_cast<float>(width - 1) || z > static_cast<float>(mapHeight - 1)) {
						continue;
					}
					Placed p;
					p.position = glm::vec3(x * metresPerTexel, in.position.y * input.metresPerUnit, z * metresPerTexel);
					p.yaw = in.yaw;
					p.scale = in.scale / vegetation::unitsPerMetre;
					p.model = in.model;
					p.variant = in.variant;
					p.layer = in.layer;
					placed.push_back(p);
				}
			}
		}
		return placed;
	}

	std::vector<vegetation::Instance> ToAppInstances(const std::vector<Placed>& placed, glm::vec2 texelZeroWorld, float metresPerUnit)
	{
		std::vector<vegetation::Instance> instances;
		instances.reserve(placed.size());
		//1 texel = 1 m = vegetation::unitsPerMetre world units
		const float unitsPerMetre = vegetation::unitsPerMetre / metresPerTexel;
		for (const Placed& p : placed) {
			vegetation::Instance in;
			in.position = glm::vec3(texelZeroWorld.x + p.position.x * unitsPerMetre, p.position.y / metresPerUnit, texelZeroWorld.y + p.position.z * unitsPerMetre);
			in.scale = p.scale * vegetation::unitsPerMetre;
			in.yaw = p.yaw;
			in.model = p.model;
			in.variant = p.variant;
			in.layer = p.layer;
			instances.push_back(in);
		}
		return instances;
	}

	namespace {
		const char* generator = "Procedural Terrain Generator";

		struct Image {
			int width = 0, height = 0;
			std::vector<unsigned char> pixels;
		};
		bool LoadImage(const std::filesystem::path& path, int channels, Image& image)
		{
			stbi_set_flip_vertically_on_load_thread(0);
			unsigned char* data = stbi_load(path.string().c_str(), &image.width, &image.height, nullptr, channels);
			if (!data) {
				return false;
			}
			image.pixels.assign(data, data + static_cast<size_t>(image.width) * image.height * channels);
			stbi_image_free(data);
			return true;
		}

		//Copies a texture of a model into the pack once; returns its path relative to the model file, empty on failure
		std::string CopyTexture(const std::filesystem::path& sourceFolder, const std::string& relative, const std::filesystem::path& textures)
		{
			const std::filesystem::path from = sourceFolder / relative;
			const std::string fileName = from.filename().string();
			std::error_code ec;
			std::filesystem::create_directories(textures, ec);
			std::filesystem::copy_file(from, textures / fileName, std::filesystem::copy_options::overwrite_existing, ec);
			return ec ? std::string() : "textures/" + fileName;
		}

		//Albedo with the alpha map in A, as an RGBA PNG; the alpha map is resampled (nearest) to the albedo
		std::string MergeAlpha(const std::filesystem::path& sourceFolder, const std::string& albedoPath, const std::string& alphaPath,
			const std::filesystem::path& textures)
		{
			Image albedo, alpha;
			if (!LoadImage(sourceFolder / albedoPath, 3, albedo) || !LoadImage(sourceFolder / alphaPath, 1, alpha)) {
				return std::string();
			}
			std::vector<unsigned char> rgba(static_cast<size_t>(albedo.width) * albedo.height * 4);
			for (int y = 0; y < albedo.height; y++) {
				const int ay = y * alpha.height / albedo.height;
				for (int x = 0; x < albedo.width; x++) {
					const int ax = x * alpha.width / albedo.width;
					const size_t i = static_cast<size_t>(y) * albedo.width + x;
					rgba[i * 4 + 0] = albedo.pixels[i * 3 + 0];
					rgba[i * 4 + 1] = albedo.pixels[i * 3 + 1];
					rgba[i * 4 + 2] = albedo.pixels[i * 3 + 2];
					rgba[i * 4 + 3] = alpha.pixels[static_cast<size_t>(ay) * alpha.width + ax];
				}
			}
			std::vector<unsigned char> png;
			if (lodepng::encode(png, rgba, static_cast<unsigned>(albedo.width), static_cast<unsigned>(albedo.height), LCT_RGBA, 8) != 0) {
				return std::string();
			}
			const std::string fileName = std::filesystem::path(albedoPath).stem().string() + "_rgba.png";
			std::string error;
			return WriteFileAtomic(textures / fileName, png.data(), png.size(), error) ? "textures/" + fileName : std::string();
		}

		void PutFixed(std::string& out, float value, int decimals)
		{
			char buffer[32];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, decimals);
			out.append(buffer, result.ptr);
		}
	}

	ModelFile WriteModel(const Model& model, const std::filesystem::path& folder)
	{
		ModelFile file;
		const std::filesystem::path sourceFolder = model.source.parent_path();
		if (!std::filesystem::exists(model.source)) {
			file.error = "missing " + model.source.filename().string();
			return file;
		}
		//The levels the app built and cached, else built again the same way (a large tree takes seconds)
		vegetation::ModelMesh built;
		if (model.cache.empty() || model.lodTargets.empty()
			|| !vegetation::LoadCache(model.cache, built, vegetation::CacheKey(sourceFolder, model.lodTargets))) {
			std::vector<vegetation::Material> materials;
			std::vector<vegetation::SourceModel> variants;
			if (!vegetation::LoadGltf(model.source, materials, variants, file.error)) {
				return file;
			}
			//Without targets one level as large as the largest variant, which copies the source
			std::vector<uint32_t> targets = model.lodTargets;
			size_t largest = 0;
			for (vegetation::SourceModel& variant : variants) {
				vegetation::MoveBaseToOrigin(variant);
				largest = std::max(largest, variant.TriangleCount());
			}
			if (targets.empty()) {
				targets.push_back(static_cast<uint32_t>(std::min<size_t>(largest, UINT32_MAX)));
			}
			built = vegetation::BuildLods(materials, variants, targets);
		}
		const std::filesystem::path textures = folder / "vegetation" / "textures";

		std::vector<meshfile::Material> out(built.materials.size());
		for (size_t m = 0; m < built.materials.size(); m++) {
			const vegetation::Material& source = built.materials[m];
			meshfile::Material& material = out[m];
			material.name = source.name;
			material.repeat = true;
			material.mask = source.alphaTest;
			material.doubleSided = source.alphaTest || source.foliage;
			if (!source.albedo.empty()) {
				material.texture = source.alpha.empty() ? CopyTexture(sourceFolder, source.albedo, textures)
					: MergeAlpha(sourceFolder, source.albedo, source.alpha, textures);
			}
			if (!source.normal.empty()) {
				material.normalTexture = CopyTexture(sourceFolder, source.normal, textures);
			}
			if (!source.arm.empty()) {
				material.ormTexture = CopyTexture(sourceFolder, source.arm, textures);
			}
		}

		//A primitive per part of a level, with only the vertices it uses; meshes outlive the nodes pointing at them
		size_t partCount = 0;
		for (const vegetation::Variant& variant : built.variants) {
			for (const vegetation::Lod& lod : variant.lods) {
				partCount += lod.parts.size();
			}
		}
		std::vector<terrainmesh::Mesh> meshes;
		meshes.reserve(partCount);
		std::vector<uint32_t> local(built.vertices.size(), UINT32_MAX);
		std::vector<meshfile::Node> nodes;
		file.nodes.resize(built.variants.size());
		for (size_t v = 0; v < built.variants.size(); v++) {
			const vegetation::Variant& variant = built.variants[v];
			for (size_t l = 0; l < variant.lods.size(); l++) {
				meshfile::Node node;
				node.name = model.name + "_" + std::to_string(v) + "_LOD" + std::to_string(l);
				for (const vegetation::Part& part : variant.lods[l].parts) {
					terrainmesh::Mesh mesh;
					mesh.indices.reserve(part.indexCount);
					for (uint32_t i = part.firstIndex; i < part.firstIndex + part.indexCount; i++) {
						const uint32_t vertex = built.indices[i];
						if (local[vertex] == UINT32_MAX) {
							local[vertex] = static_cast<uint32_t>(mesh.positions.size());
							mesh.positions.push_back(built.vertices[vertex].position);
							mesh.normals.push_back(built.vertices[vertex].normal);
							mesh.uvs.push_back(built.vertices[vertex].uv);
						}
						mesh.indices.push_back(local[vertex]);
					}
					//Reset only what this part touched
					for (uint32_t i = part.firstIndex; i < part.firstIndex + part.indexCount; i++) {
						local[built.indices[i]] = UINT32_MAX;
					}
					meshes.push_back(std::move(mesh));
					node.primitives.push_back({ &meshes.back(), part.material });
				}
				if (v == 0) {
					file.lodTriangles.push_back(variant.lods[l].triangles);
				}
				//EncodeGlb leaves out a node without triangles
				if (variant.lods[l].triangles == 0) {
					continue;
				}
				file.nodes[v].push_back(node.name);
				nodes.push_back(std::move(node));
			}
		}

		const std::string path = "vegetation/" + model.asset + ".glb";
		const std::vector<unsigned char> glb = meshfile::EncodeGlb(nodes, out, generator);
		if (!WriteFileAtomic(folder / path, glb.data(), glb.size(), file.error)) {
			file.nodes.clear();
			return file;
		}
		file.path = path;
		return file;
	}

	std::string InstancesJson(const std::vector<Placed>& placed, const std::vector<Model>& models, const std::vector<ModelFile>& files)
	{
		std::vector<size_t> counts(models.size(), 0);
		for (const Placed& p : placed) {
			if (p.model >= 0 && p.model < static_cast<int>(counts.size())) {
				counts[p.model]++;
			}
		}

		Json j;
		j["generator"] = generator;
		j["units"] = "metres, Y up, right-handed, the axes of the pack: x = map column, z = map row, texel (0, 0) at the origin";
		j["position"] = "where the base of the model stands on the terrain (the models have their base at the origin)";
		j["yaw"] = "radians around +Y, right-handed: x' = x cos + z sin, z' = z cos - x sin; glTF rotation [0, sin(yaw / 2), 0, cos(yaw / 2)]";
		j["scale"] = "uniform, metres per metre of the model";
		Json list = Json::array();
		for (size_t m = 0; m < models.size(); m++) {
			Json entry;
			entry["id"] = m;
			entry["name"] = models[m].name;
			entry["kind"] = models[m].kind;
			entry["layer"] = vegetation::layerNames[static_cast<int>(vegetation::LayerOfKind(models[m].kind))];
			entry["asset"] = models[m].asset;
			entry["variants"] = models[m].variants;
			entry["count"] = counts[m];
			if (m < files.size() && !files[m].path.empty()) {
				entry["file"] = files[m].path;
				entry["variantNodes"] = files[m].nodes;
				entry["lodTriangles"] = files[m].lodTriangles;
			}
			list.push_back(entry);
		}
		j["models"] = list;
		j["columns"] = { "x", "y", "z", "yaw", "scale", "model", "variant" };
		j["instances"] = Json::array();

		//The instances by hand, one row per line with centimetre positions; nlohmann would write every float with all its digits
		std::string text = j.dump(2);
		const std::string empty = "\"instances\": []";
		const size_t at = text.rfind(empty);
		std::string rows = "\"instances\": [";
		rows.reserve(placed.size() * 48 + 32);
		for (size_t i = 0; i < placed.size(); i++) {
			const Placed& p = placed[i];
			rows += i == 0 ? "\n    [" : ",\n    [";
			PutFixed(rows, p.position.x, 2);
			rows += ", ";
			PutFixed(rows, p.position.y, 2);
			rows += ", ";
			PutFixed(rows, p.position.z, 2);
			rows += ", ";
			PutFixed(rows, p.yaw, 4);
			rows += ", ";
			PutFixed(rows, p.scale, 3);
			rows += ", " + std::to_string(p.model) + ", " + std::to_string(p.variant) + "]";
		}
		rows += placed.empty() ? "]" : "\n  ]";
		text.replace(at, empty.size(), rows);
		return text;
	}

	Result Write(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input, const std::string& name,
		bool writeModels, const std::filesystem::path& folder)
	{
		const auto start = std::chrono::steady_clock::now();
		Result result;
		const std::vector<Placed> placed = PlaceMap(height, surface, ids, input);
		result.instances = placed.size();

		std::vector<ModelFile> files(input.models.size());
		if (writeModels) {
			std::vector<bool> used(input.models.size(), false);
			for (const Placed& p : placed) {
				used[p.model] = true;
			}
			for (size_t m = 0; m < input.models.size(); m++) {
				if (!used[m]) {
					continue;
				}
				files[m] = WriteModel(input.models[m], folder);
				if (files[m].path.empty()) {
					result.modelErrors.push_back(input.models[m].name + ": " + files[m].error);
					continue;
				}
				//The app places by its own count of variants; a file read now with another count would not match the ids
				if (static_cast<int>(files[m].nodes.size()) != input.models[m].variants) {
					result.modelErrors.push_back(input.models[m].name + ": the file has " + std::to_string(files[m].nodes.size()) + " variants, the app "
						+ std::to_string(input.models[m].variants));
				}
				result.files.push_back({ files[m].path, "vegetation_model", "glb", std::nullopt });
			}
		}

		const std::string json = InstancesJson(placed, input.models, files);
		const std::string path = name + "_vegetation.json";
		if (!WriteFileAtomic(folder / path, json.data(), json.size(), result.error)) {
			result.error = path + ": " + result.error;
			return result;
		}
		result.files.insert(result.files.begin(), exportpack::File{ path, "vegetation", "json", std::nullopt });
		result.ok = true;
		result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	}
}
