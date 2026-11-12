#include "VegetationMesh.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <numeric>
#include <unordered_map>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <meshoptimizer.h>

namespace vegetation
{
	namespace {
		//Bumped whenever the cache layout or the way levels are built changes, so old caches are rebuilt
		constexpr uint32_t cacheVersion = 3;
		constexpr char cacheMagic[4] = { 'P', 'T', 'G', 'V' };
		//Relative error allowed when collapsing edges (meshoptimizer: a part of the mesh extent)
		constexpr float surfaceError = 0.05f;
		//A level may end up this much over its target before more is done to get closer
		constexpr size_t overTarget = 3;	//in halves: 1.5 times
		//Surfaces of at least this many pieces are thinned out by dropping their smallest pieces (branches, twigs);
		//smaller ones (rock, trunk) fall back to vertex clustering
		constexpr size_t manyPieces = 16;
		//Pieces are dropped until the rest has at most this many times the target before collapsing edges
		constexpr size_t keptOverTarget = 4;
		//Kept pieces of foliage are enlarged at most this many times
		constexpr float maxCardScale = 4.0f;
		//Height of the lowest part of a model (as a part of its height) whose middle becomes x = z = 0
		constexpr float basePart = 0.03f;
		//Bases of grounded root nodes closer than this part of their footprint are pieces of one model, not variants
		constexpr float footprintMargin = 0.02f;
		//A root node whose lowest point is within this part of the file's height above its lowest point stands on the ground
		constexpr float groundPart = 0.05f;

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		//Uri of the image of a texture view, empty if none
		std::string ImageUri(const cgltf_texture_view& view)
		{
			if (!view.texture || !view.texture->image || !view.texture->image->uri) {
				return {};
			}
			std::string uri = view.texture->image->uri;
			cgltf_decode_uri(uri.data());
			return std::string(uri.c_str());
		}

		void ComputeNormals(SourcePart& part)
		{
			for (Vertex& v : part.vertices) {
				v.normal = glm::vec3(0.0f);
			}
			for (size_t i = 0; i + 2 < part.indices.size(); i += 3) {
				Vertex& a = part.vertices[part.indices[i]];
				Vertex& b = part.vertices[part.indices[i + 1]];
				Vertex& c = part.vertices[part.indices[i + 2]];
				const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
				a.normal += n;
				b.normal += n;
				c.normal += n;
			}
			for (Vertex& v : part.vertices) {
				const float length = glm::length(v.normal);
				v.normal = length > 0.0f ? v.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
			}
		}

		//Vertices used by indices, in order of first use, with the indices renumbered
		void Compact(const std::vector<Vertex>& source, std::vector<uint32_t>& indices, std::vector<Vertex>& vertices)
		{
			std::vector<uint32_t> remap(source.size(), UINT32_MAX);
			vertices.clear();
			for (uint32_t& index : indices) {
				if (remap[index] == UINT32_MAX) {
					remap[index] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(source[index]);
				}
				index = remap[index];
			}
		}

		//Copy of the triangles of a part, reordered for the vertex cache, with its vertices compacted
		void CopyOptimized(const std::vector<Vertex>& source, std::vector<uint32_t> triangles, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
		{
			indices = std::move(triangles);
			meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), source.size());
			Compact(source, indices, vertices);
		}

		uint32_t Find(std::vector<uint32_t>& parent, uint32_t i)
		{
			while (parent[i] != i) {
				parent[i] = parent[parent[i]];
				i = parent[i];
			}
			return i;
		}

		//Triangles of each connected piece of a part (connected through shared vertices)
		std::vector<std::vector<uint32_t>> Pieces(const SourcePart& part)
		{
			const size_t triangleCount = part.indices.size() / 3;
			std::vector<uint32_t> parent(part.vertices.size());
			std::iota(parent.begin(), parent.end(), 0u);
			for (size_t t = 0; t < triangleCount; t++) {
				const uint32_t a = Find(parent, part.indices[t * 3]);
				for (int k = 1; k < 3; k++) {
					const uint32_t b = Find(parent, part.indices[t * 3 + k]);
					if (a != b) {
						parent[b] = a;
					}
				}
			}
			std::unordered_map<uint32_t, uint32_t> pieceOfRoot;
			std::vector<std::vector<uint32_t>> pieces;
			for (size_t t = 0; t < triangleCount; t++) {
				const uint32_t root = Find(parent, part.indices[t * 3]);
				auto found = pieceOfRoot.find(root);
				if (found == pieceOfRoot.end()) {
					found = pieceOfRoot.emplace(root, static_cast<uint32_t>(pieces.size())).first;
					pieces.emplace_back();
				}
				pieces[found->second].push_back(static_cast<uint32_t>(t));
			}
			return pieces;
		}

		//Bounding box of the triangles of a piece
		void PieceBounds(const SourcePart& part, const std::vector<uint32_t>& triangles, glm::vec3& low, glm::vec3& high)
		{
			low = glm::vec3(INFINITY);
			high = glm::vec3(-INFINITY);
			for (const uint32_t t : triangles) {
				for (int k = 0; k < 3; k++) {
					const glm::vec3 p = part.vertices[part.indices[t * 3 + k]].position;
					low = glm::min(low, p);
					high = glm::max(high, p);
				}
			}
		}

		//Indices of these triangles of a part
		std::vector<uint32_t> TriangleIndices(const SourcePart& part, const std::vector<std::vector<uint32_t>>& pieces, const std::vector<uint32_t>& order, size_t count)
		{
			std::vector<uint32_t> indices;
			for (size_t i = 0; i < count; i++) {
				for (const uint32_t t : pieces[order[i]]) {
					indices.insert(indices.end(), { part.indices[t * 3], part.indices[t * 3 + 1], part.indices[t * 3 + 2] });
				}
			}
			return indices;
		}

		//Collapses edges of these indices towards targetIndices
		std::vector<uint32_t> Collapse(const SourcePart& part, const std::vector<uint32_t>& indices, size_t targetIndices)
		{
			std::vector<uint32_t> result(indices.size());
			float error = 0.0f;
			const size_t count = meshopt_simplify(result.data(), indices.data(), indices.size(), &part.vertices[0].position.x, part.vertices.size(),
				sizeof(Vertex), targetIndices, surfaceError, 0, &error);
			result.resize(count);
			return result;
		}

		//Order of pieces spread evenly over the model: a hash, so neighbours are not kept or dropped together
		uint32_t Hash(uint32_t x)
		{
			x ^= x >> 16;
			x *= 0x7feb352dU;
			x ^= x >> 15;
			x *= 0x846ca68bU;
			x ^= x >> 16;
			return x;
		}

		template <typename T>
		void Write(std::ofstream& out, const T& value)
		{
			out.write(reinterpret_cast<const char*>(&value), sizeof(T));
		}
		template <typename T>
		bool Read(std::ifstream& in, T& value)
		{
			return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
		}
		template <typename T>
		void WriteVector(std::ofstream& out, const std::vector<T>& values)
		{
			Write(out, static_cast<uint64_t>(values.size()));
			out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
		}
		template <typename T>
		bool ReadVector(std::ifstream& in, std::vector<T>& values, uint64_t maxCount)
		{
			uint64_t count = 0;
			if (!Read(in, count) || count > maxCount) {
				return false;
			}
			values.resize(static_cast<size_t>(count));
			return static_cast<bool>(in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T))));
		}
		void WriteString(std::ofstream& out, const std::string& text)
		{
			Write(out, static_cast<uint32_t>(text.size()));
			out.write(text.data(), static_cast<std::streamsize>(text.size()));
		}
		bool ReadString(std::ifstream& in, std::string& text)
		{
			uint32_t size = 0;
			if (!Read(in, size) || size > 4096) {
				return false;
			}
			text.resize(size);
			return static_cast<bool>(in.read(text.data(), size));
		}

		//Adds the triangle primitives of a node and its children to model, transformed into world space
		void AddNode(const cgltf_data* data, const cgltf_node& node, SourceModel& model, std::unordered_map<int, size_t>& partOfMaterial, int noMaterial)
		{
			if (node.mesh) {
				float world[16];
				cgltf_node_transform_world(&node, world);
				glm::mat4 transform;
				std::memcpy(&transform[0][0], world, sizeof(world));
				const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
				//A mirroring transform turns the triangles inside out
				const bool flip = glm::determinant(glm::mat3(transform)) < 0.0f;

				for (cgltf_size p = 0; p < node.mesh->primitives_count; p++) {
					const cgltf_primitive& primitive = node.mesh->primitives[p];
					if (primitive.type != cgltf_primitive_type_triangles) {
						continue;
					}
					const cgltf_accessor* positions = nullptr;
					const cgltf_accessor* normals = nullptr;
					const cgltf_accessor* uvs = nullptr;
					for (cgltf_size a = 0; a < primitive.attributes_count; a++) {
						const cgltf_attribute& attribute = primitive.attributes[a];
						if (attribute.type == cgltf_attribute_type_position) {
							positions = attribute.data;
						}
						else if (attribute.type == cgltf_attribute_type_normal) {
							normals = attribute.data;
						}
						else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) {
							uvs = attribute.data;
						}
					}
					if (!positions || positions->count == 0) {
						continue;
					}
					const int material = primitive.material ? static_cast<int>(primitive.material - data->materials) : noMaterial;
					auto found = partOfMaterial.find(material);
					if (found == partOfMaterial.end()) {
						found = partOfMaterial.emplace(material, model.parts.size()).first;
						model.parts.push_back(SourcePart{ material });
					}
					SourcePart& part = model.parts[found->second];
					const uint32_t first = static_cast<uint32_t>(part.vertices.size());
					const size_t count = positions->count;

					std::vector<float> values(count * 3);
					cgltf_accessor_unpack_floats(positions, values.data(), values.size());
					std::vector<float> normalValues;
					if (normals && normals->count == count) {
						normalValues.resize(count * 3);
						cgltf_accessor_unpack_floats(normals, normalValues.data(), normalValues.size());
					}
					std::vector<float> uvValues;
					if (uvs && uvs->count == count) {
						uvValues.resize(count * 2);
						cgltf_accessor_unpack_floats(uvs, uvValues.data(), uvValues.size());
					}
					for (size_t i = 0; i < count; i++) {
						Vertex v;
						v.position = glm::vec3(transform * glm::vec4(values[i * 3], values[i * 3 + 1], values[i * 3 + 2], 1.0f));
						if (!normalValues.empty()) {
							const glm::vec3 normal = normalMatrix * glm::vec3(normalValues[i * 3], normalValues[i * 3 + 1], normalValues[i * 3 + 2]);
							const float length = glm::length(normal);
							v.normal = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
						}
						if (!uvValues.empty()) {
							v.uv = glm::vec2(uvValues[i * 2], uvValues[i * 2 + 1]);
						}
						part.vertices.push_back(v);
					}
					const size_t indexStart = part.indices.size();
					if (primitive.indices) {
						for (cgltf_size i = 0; i < primitive.indices->count; i++) {
							part.indices.push_back(first + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
						}
					}
					else {
						for (size_t i = 0; i < count; i++) {
							part.indices.push_back(first + static_cast<uint32_t>(i));
						}
					}
					part.indices.resize(indexStart + (part.indices.size() - indexStart) / 3 * 3);
					if (flip) {
						for (size_t i = indexStart; i + 2 < part.indices.size(); i += 3) {
							std::swap(part.indices[i + 1], part.indices[i + 2]);
						}
					}
					if (normalValues.empty()) {
						ComputeNormals(part);
					}
				}
			}
			for (cgltf_size c = 0; c < node.children_count; c++) {
				AddNode(data, *node.children[c], model, partOfMaterial, noMaterial);
			}
		}

		//Footprint (XZ bounds) of a variant; empty (low > high) without vertices
		void Footprint(const SourceModel& model, glm::vec2& low, glm::vec2& high)
		{
			low = glm::vec2(INFINITY);
			high = glm::vec2(-INFINITY);
			for (const SourcePart& part : model.parts) {
				for (const Vertex& v : part.vertices) {
					low = glm::min(low, glm::vec2(v.position.x, v.position.z));
					high = glm::max(high, glm::vec2(v.position.x, v.position.z));
				}
			}
		}

		//Adds the parts of source to target, merging parts of the same material
		void Merge(SourceModel& target, SourceModel& source)
		{
			for (SourcePart& part : source.parts) {
				auto found = std::find_if(target.parts.begin(), target.parts.end(), [&](const SourcePart& p) { return p.material == part.material; });
				if (found == target.parts.end()) {
					target.parts.push_back(std::move(part));
					continue;
				}
				const uint32_t first = static_cast<uint32_t>(found->vertices.size());
				found->vertices.insert(found->vertices.end(), part.vertices.begin(), part.vertices.end());
				for (const uint32_t index : part.indices) {
					found->indices.push_back(first + index);
				}
			}
			source.parts.clear();
		}
	}

	size_t SourceModel::TriangleCount() const
	{
		size_t count = 0;
		for (const SourcePart& part : parts) {
			count += part.indices.size() / 3;
		}
		return count;
	}

	bool IsFoliageName(const std::string& name)
	{
		const std::string lower = Lower(name);
		for (const char* word : { "leaf", "leaves", "twig", "needle" }) {
			if (lower.find(word) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	bool LoadGltf(const std::filesystem::path& file, std::vector<Material>& materials, std::vector<SourceModel>& variants, std::string& error)
	{
		materials.clear();
		variants.clear();
		cgltf_options options{};
		cgltf_data* data = nullptr;
		const std::string path = file.string();
		if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
			error = "cannot parse " + path;
			return false;
		}
		if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
			cgltf_free(data);
			error = "cannot load the buffers of " + path;
			return false;
		}

		const std::filesystem::path folder = file.parent_path();
		for (cgltf_size m = 0; m < data->materials_count; m++) {
			const cgltf_material& source = data->materials[m];
			Material material;
			material.name = source.name ? source.name : "material " + std::to_string(m);
			material.albedo = ImageUri(source.pbr_metallic_roughness.base_color_texture);
			material.normal = ImageUri(source.normal_texture);
			material.arm = ImageUri(source.pbr_metallic_roughness.metallic_roughness_texture);
			material.alphaTest = source.alpha_mode != cgltf_alpha_mode_opaque;
			//The JPEG textures have no alpha; Poly Haven keeps it in a map named like the albedo with "_alpha_" for "_diff_"
			const size_t diff = material.albedo.rfind("_diff_");
			if (diff != std::string::npos) {
				std::string alpha = material.albedo;
				alpha.replace(diff, 6, "_alpha_");
				if (std::filesystem::exists(folder / alpha)) {
					material.alpha = alpha;
				}
			}
			if (material.alpha.empty()) {
				material.alphaTest = false;
			}
			material.foliage = material.alphaTest || IsFoliageName(material.name);
			materials.push_back(material);
		}
		//Primitives without a material share one extra material
		const int noMaterial = static_cast<int>(materials.size());
		materials.push_back(Material{ "default" });

		//Roots: the nodes of the scene, or every node without a parent if the file has no scene
		std::vector<const cgltf_node*> roots;
		const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene) {
			for (cgltf_size i = 0; i < scene->nodes_count; i++) {
				roots.push_back(scene->nodes[i]);
			}
		}
		else {
			for (cgltf_size n = 0; n < data->nodes_count; n++) {
				if (!data->nodes[n].parent) {
					roots.push_back(&data->nodes[n]);
				}
			}
		}
		std::vector<SourceModel> pieces;
		for (const cgltf_node* root : roots) {
			SourceModel model;
			std::unordered_map<int, size_t> partOfMaterial;
			AddNode(data, *root, model, partOfMaterial, noMaterial);
			if (!model.parts.empty()) {
				pieces.push_back(std::move(model));
			}
		}
		cgltf_free(data);
		if (pieces.empty()) {
			error = "no triangles in " + path;
			return false;
		}

		//Variants grow from the pieces standing on the ground (their lowest point near the lowest of the file); grounded pieces
		//whose bases overlap are one model (trunk and roots as two nodes). Every piece above the ground (a crown, leaves)
		//joins the grounded group under it: the one whose footprint holds its centre, else the nearest
		float lowest = INFINITY, highest = -INFINITY;
		std::vector<float> bottom(pieces.size());
		for (size_t i = 0; i < pieces.size(); i++) {
			bottom[i] = INFINITY;
			for (const SourcePart& part : pieces[i].parts) {
				for (const Vertex& v : part.vertices) {
					bottom[i] = std::min(bottom[i], v.position.y);
					highest = std::max(highest, v.position.y);
				}
			}
			lowest = std::min(lowest, bottom[i]);
		}
		const float groundLimit = lowest + (highest - lowest) * groundPart;
		std::vector<glm::vec2> low(pieces.size()), high(pieces.size()), baseLow(pieces.size()), baseHigh(pieces.size());
		std::vector<bool> grounded(pieces.size());
		for (size_t i = 0; i < pieces.size(); i++) {
			Footprint(pieces[i], low[i], high[i]);
			grounded[i] = bottom[i] <= groundLimit;
			//Base: footprint of the part of the piece near its bottom
			baseLow[i] = glm::vec2(INFINITY);
			baseHigh[i] = glm::vec2(-INFINITY);
			const float baseTop = bottom[i] + (highest - lowest) * basePart;
			for (const SourcePart& part : pieces[i].parts) {
				for (const Vertex& v : part.vertices) {
					if (v.position.y <= baseTop) {
						baseLow[i] = glm::min(baseLow[i], glm::vec2(v.position.x, v.position.z));
						baseHigh[i] = glm::max(baseHigh[i], glm::vec2(v.position.x, v.position.z));
					}
				}
			}
			const glm::vec2 margin = (high[i] - low[i]) * footprintMargin;
			baseLow[i] -= margin;
			baseHigh[i] += margin;
		}
		std::vector<uint32_t> parent(pieces.size());
		std::iota(parent.begin(), parent.end(), 0u);
		for (size_t i = 0; i < pieces.size(); i++) {
			for (size_t j = i + 1; j < pieces.size(); j++) {
				const bool overlap = baseLow[i].x <= baseHigh[j].x && baseLow[j].x <= baseHigh[i].x && baseLow[i].y <= baseHigh[j].y && baseLow[j].y <= baseHigh[i].y;
				if (grounded[i] && grounded[j] && overlap) {
					parent[Find(parent, static_cast<uint32_t>(j))] = Find(parent, static_cast<uint32_t>(i));
				}
			}
		}
		for (size_t i = 0; i < pieces.size(); i++) {
			if (grounded[i]) {
				continue;
			}
			const glm::vec2 center = (low[i] + high[i]) / 2.0f;
			size_t best = i;
			float bestDistance = INFINITY;
			for (size_t j = 0; j < pieces.size(); j++) {
				if (!grounded[j]) {
					continue;
				}
				const bool inside = center.x >= low[j].x && center.x <= high[j].x && center.y >= low[j].y && center.y <= high[j].y;
				const float distance = inside ? 0.0f : glm::length(center - (low[j] + high[j]) / 2.0f);
				if (distance < bestDistance) {
					bestDistance = distance;
					best = j;
				}
			}
			parent[Find(parent, static_cast<uint32_t>(i))] = Find(parent, static_cast<uint32_t>(best));
		}
		std::unordered_map<uint32_t, size_t> variantOfRoot;
		for (size_t i = 0; i < pieces.size(); i++) {
			const uint32_t group = Find(parent, static_cast<uint32_t>(i));
			auto found = variantOfRoot.find(group);
			if (found == variantOfRoot.end()) {
				variantOfRoot.emplace(group, variants.size());
				variants.push_back(std::move(pieces[i]));
			}
			else {
				Merge(variants[found->second], pieces[i]);
			}
		}
		return true;
	}

	void MoveBaseToOrigin(SourceModel& model)
	{
		glm::vec3 low(INFINITY), high(-INFINITY);
		for (const SourcePart& part : model.parts) {
			for (const Vertex& v : part.vertices) {
				low = glm::min(low, v.position);
				high = glm::max(high, v.position);
			}
		}
		if (low.x > high.x) {
			return;
		}
		const float baseTop = low.y + (high.y - low.y) * basePart;
		glm::vec2 baseLow(INFINITY), baseHigh(-INFINITY);
		for (const SourcePart& part : model.parts) {
			for (const Vertex& v : part.vertices) {
				if (v.position.y <= baseTop) {
					baseLow = glm::min(baseLow, glm::vec2(v.position.x, v.position.z));
					baseHigh = glm::max(baseHigh, glm::vec2(v.position.x, v.position.z));
				}
			}
		}
		const glm::vec2 center = (baseLow + baseHigh) / 2.0f;
		const glm::vec3 offset(-center.x, -low.y, -center.y);
		for (SourcePart& part : model.parts) {
			for (Vertex& v : part.vertices) {
				v.position += offset;
			}
		}
	}

	void SimplifySurface(const SourcePart& part, size_t targetTriangles, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		const size_t triangleCount = part.indices.size() / 3;
		if (triangleCount == 0) {
			vertices.clear();
			indices.clear();
			return;
		}
		targetTriangles = std::max<size_t>(targetTriangles, 1);
		if (triangleCount <= targetTriangles) {
			CopyOptimized(part.vertices, part.indices, vertices, indices);
			return;
		}
		const size_t targetIndices = targetTriangles * 3;
		const size_t acceptable = targetIndices * overTarget / 2;
		const std::vector<std::vector<uint32_t>> pieces = Pieces(part);

		std::vector<uint32_t> result;
		if (pieces.size() < manyPieces) {
			result = Collapse(part, part.indices, targetIndices);
			//Collapses stop at the error limit or where the mesh cannot collapse further (seams): cluster vertices instead
			if (result.size() > acceptable) {
				result.resize(part.indices.size());
				float error = 0.0f;
				result.resize(meshopt_simplifySloppy(result.data(), part.indices.data(), part.indices.size(), &part.vertices[0].position.x,
					part.vertices.size(), sizeof(Vertex), targetIndices, FLT_MAX, &error));
			}
		}
		else {
			//Largest pieces first; the smallest (twigs) go first when thinning out
			std::vector<float> size(pieces.size());
			for (size_t i = 0; i < pieces.size(); i++) {
				glm::vec3 low, high;
				PieceBounds(part, pieces[i], low, high);
				size[i] = glm::length(high - low);
			}
			std::vector<uint32_t> order(pieces.size());
			std::iota(order.begin(), order.end(), 0u);
			std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return size[a] > size[b]; });
			size_t kept = pieces.size();
			size_t keptTriangles = triangleCount;
			while (kept > 1 && keptTriangles > keptOverTarget * targetTriangles) {
				kept--;
				keptTriangles -= pieces[order[kept]].size();
			}
			result = Collapse(part, TriangleIndices(part, pieces, order, kept), targetIndices);
			while (result.size() > acceptable && kept > 1) {
				kept = std::max<size_t>(kept * 3 / 4, 1);
				result = Collapse(part, TriangleIndices(part, pieces, order, kept), targetIndices);
			}
		}
		CopyOptimized(part.vertices, std::move(result), vertices, indices);
	}

	void SimplifyCards(const SourcePart& part, size_t targetTriangles, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		const size_t triangleCount = part.indices.size() / 3;
		if (triangleCount <= targetTriangles) {
			CopyOptimized(part.vertices, part.indices, vertices, indices);
			return;
		}
		const std::vector<std::vector<uint32_t>> pieces = Pieces(part);
		//Too few pieces to drop some evenly (one big mesh): simplify it as a surface instead
		if (pieces.size() < manyPieces) {
			SimplifySurface(part, targetTriangles, vertices, indices);
			return;
		}

		std::vector<uint32_t> order(pieces.size());
		std::iota(order.begin(), order.end(), 0u);
		std::sort(order.begin(), order.end(), [](uint32_t a, uint32_t b) { return Hash(a) < Hash(b); });
		std::vector<uint32_t> kept;
		size_t keptTriangles = 0;
		for (const uint32_t piece : order) {
			if (keptTriangles >= targetTriangles) {
				break;
			}
			kept.push_back(piece);
			keptTriangles += pieces[piece].size();
		}
		//Area grows with the square of the scale
		const float scale = std::min(std::sqrt(static_cast<float>(triangleCount) / static_cast<float>(std::max<size_t>(keptTriangles, 1))), maxCardScale);

		std::vector<Vertex> scaled = part.vertices;
		std::vector<char> done(part.vertices.size(), 0);
		std::vector<uint32_t> result;
		for (const uint32_t piece : kept) {
			glm::vec3 low, high;
			PieceBounds(part, pieces[piece], low, high);
			const glm::vec3 center = (low + high) / 2.0f;
			for (const uint32_t t : pieces[piece]) {
				for (int k = 0; k < 3; k++) {
					const uint32_t index = part.indices[t * 3 + k];
					if (!done[index]) {
						scaled[index].position = center + (part.vertices[index].position - center) * scale;
						done[index] = 1;
					}
					result.push_back(index);
				}
			}
		}
		CopyOptimized(scaled, std::move(result), vertices, indices);
	}

	ModelMesh BuildLods(const std::vector<Material>& materials, const std::vector<SourceModel>& variants, const std::vector<uint32_t>& targetTriangles)
	{
		ModelMesh mesh;
		mesh.materials = materials;
		for (const SourceModel& model : variants) {
			Variant variant;
			variant.sourceTriangles = model.TriangleCount();
			size_t foliageTriangles = 0;
			for (const SourcePart& part : model.parts) {
				if (materials[part.material].foliage) {
					foliageTriangles += part.indices.size() / 3;
				}
			}
			const size_t surfaceTriangles = variant.sourceTriangles - foliageTriangles;
			//Triangles of each part in the previous level, so a level never grows
			std::vector<size_t> previous;
			for (const SourcePart& part : model.parts) {
				previous.push_back(part.indices.size() / 3);
			}

			for (const uint32_t target : targetTriangles) {
				//Budget of each kind: foliage at least foliageShare, but never more than it has; the rest for surfaces
				double foliageBudget = 0.0, surfaceBudget = static_cast<double>(target);
				if (foliageTriangles > 0 && surfaceTriangles > 0) {
					const double proportional = static_cast<double>(target) * static_cast<double>(foliageTriangles) / static_cast<double>(variant.sourceTriangles);
					foliageBudget = std::min(std::max(proportional, static_cast<double>(target) * foliageShare), static_cast<double>(foliageTriangles));
					surfaceBudget = static_cast<double>(target) - foliageBudget;
				}
				else if (foliageTriangles > 0) {
					foliageBudget = static_cast<double>(target);
					surfaceBudget = 0.0;
				}

				Lod lod;
				for (size_t p = 0; p < model.parts.size(); p++) {
					const SourcePart& part = model.parts[p];
					if (part.indices.empty()) {
						continue;
					}
					const bool foliage = materials[part.material].foliage;
					const double budget = foliage ? foliageBudget : surfaceBudget;
					const double kindTriangles = static_cast<double>(foliage ? foliageTriangles : surfaceTriangles);
					const size_t share = static_cast<size_t>(std::llround(budget * static_cast<double>(part.indices.size() / 3) / kindTriangles));
					const size_t partTarget = std::clamp<size_t>(share, 4, previous[p]);
					std::vector<Vertex> vertices;
					std::vector<uint32_t> indices;
					if (foliage) {
						SimplifyCards(part, partTarget, vertices, indices);
					}
					else {
						SimplifySurface(part, partTarget, vertices, indices);
					}
					if (indices.empty()) {
						continue;
					}
					previous[p] = indices.size() / 3;
					const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
					Part range;
					range.material = part.material;
					range.firstIndex = static_cast<uint32_t>(mesh.indices.size());
					range.indexCount = static_cast<uint32_t>(indices.size());
					for (const uint32_t index : indices) {
						mesh.indices.push_back(base + index);
					}
					mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());
					lod.parts.push_back(range);
					lod.triangles += range.indexCount / 3;
				}
				variant.lods.push_back(lod);
			}

			//Bounds of the first level (the others only shrink or enlarge cards within it)
			variant.boundsMin = glm::vec3(INFINITY);
			variant.boundsMax = glm::vec3(-INFINITY);
			for (const Lod& lod : variant.lods) {
				for (const Part& part : lod.parts) {
					for (uint32_t i = part.firstIndex; i < part.firstIndex + part.indexCount; i++) {
						variant.boundsMin = glm::min(variant.boundsMin, mesh.vertices[mesh.indices[i]].position);
						variant.boundsMax = glm::max(variant.boundsMax, mesh.vertices[mesh.indices[i]].position);
					}
				}
			}
			if (variant.boundsMin.x > variant.boundsMax.x) {
				variant.boundsMin = variant.boundsMax = glm::vec3(0.0f);
			}
			mesh.variants.push_back(variant);
		}
		return mesh;
	}

	bool SaveCache(const std::filesystem::path& file, const ModelMesh& mesh, uint64_t key)
	{
		std::error_code ignored;
		std::filesystem::create_directories(file.parent_path(), ignored);
		//Written under another name and renamed, so a broken-off write never leaves a damaged cache
		const std::filesystem::path temporary = file.string() + ".part";
		{
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			if (!out) {
				return false;
			}
			out.write(cacheMagic, sizeof(cacheMagic));
			Write(out, cacheVersion);
			Write(out, key);
			WriteVector(out, mesh.vertices);
			WriteVector(out, mesh.indices);
			Write(out, static_cast<uint32_t>(mesh.materials.size()));
			for (const Material& m : mesh.materials) {
				WriteString(out, m.name);
				WriteString(out, m.albedo);
				WriteString(out, m.normal);
				WriteString(out, m.arm);
				WriteString(out, m.alpha);
				Write(out, static_cast<uint8_t>(m.alphaTest));
				Write(out, static_cast<uint8_t>(m.foliage));
			}
			Write(out, static_cast<uint32_t>(mesh.variants.size()));
			for (const Variant& variant : mesh.variants) {
				Write(out, static_cast<uint64_t>(variant.sourceTriangles));
				Write(out, variant.boundsMin);
				Write(out, variant.boundsMax);
				Write(out, static_cast<uint32_t>(variant.lods.size()));
				for (const Lod& lod : variant.lods) {
					Write(out, lod.triangles);
					WriteVector(out, lod.parts);
				}
			}
			if (!out) {
				return false;
			}
		}
		std::filesystem::rename(temporary, file, ignored);
		return !ignored;
	}

	bool LoadCache(const std::filesystem::path& file, ModelMesh& mesh, uint64_t key)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in) {
			return false;
		}
		char magic[4] = {};
		uint32_t version = 0;
		uint64_t storedKey = 0;
		if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, cacheMagic, sizeof(magic)) != 0
			|| !Read(in, version) || version != cacheVersion || !Read(in, storedKey) || storedKey != key) {
			return false;
		}
		ModelMesh result;
		constexpr uint64_t maxElements = uint64_t(1) << 28;
		if (!ReadVector(in, result.vertices, maxElements) || !ReadVector(in, result.indices, maxElements)) {
			return false;
		}
		uint32_t materialCount = 0;
		if (!Read(in, materialCount) || materialCount > 256) {
			return false;
		}
		for (uint32_t i = 0; i < materialCount; i++) {
			Material m;
			uint8_t alphaTest = 0, foliage = 0;
			if (!ReadString(in, m.name) || !ReadString(in, m.albedo) || !ReadString(in, m.normal) || !ReadString(in, m.arm)
				|| !ReadString(in, m.alpha) || !Read(in, alphaTest) || !Read(in, foliage)) {
				return false;
			}
			m.alphaTest = alphaTest != 0;
			m.foliage = foliage != 0;
			result.materials.push_back(m);
		}
		uint32_t variantCount = 0;
		if (!Read(in, variantCount) || variantCount > 64) {
			return false;
		}
		for (uint32_t v = 0; v < variantCount; v++) {
			Variant variant;
			uint64_t sourceTriangles = 0;
			uint32_t lodCount = 0;
			if (!Read(in, sourceTriangles) || !Read(in, variant.boundsMin) || !Read(in, variant.boundsMax) || !Read(in, lodCount) || lodCount > 16) {
				return false;
			}
			variant.sourceTriangles = static_cast<size_t>(sourceTriangles);
			for (uint32_t i = 0; i < lodCount; i++) {
				Lod lod;
				if (!Read(in, lod.triangles) || !ReadVector(in, lod.parts, 256)) {
					return false;
				}
				variant.lods.push_back(lod);
			}
			result.variants.push_back(variant);
		}
		//Every range has to stay inside the arrays
		for (const uint32_t index : result.indices) {
			if (index >= result.vertices.size()) {
				return false;
			}
		}
		for (const Variant& variant : result.variants) {
			for (const Lod& lod : variant.lods) {
				for (const Part& part : lod.parts) {
					if (part.material < 0 || part.material >= static_cast<int>(result.materials.size())
						|| static_cast<uint64_t>(part.firstIndex) + part.indexCount > result.indices.size()) {
						return false;
					}
				}
			}
		}
		mesh = std::move(result);
		return true;
	}
}
