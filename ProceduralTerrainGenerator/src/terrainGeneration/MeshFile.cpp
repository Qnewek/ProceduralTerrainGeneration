#include "MeshFile.h"

#include <charconv>
#include <cstring>
#include <limits>

#include <nlohmann/json.hpp>

namespace meshfile {
	using Json = nlohmann::ordered_json;

	namespace {
		//glTF constants
		constexpr int FLOAT = 5126;
		constexpr int UNSIGNED_INT = 5125;
		constexpr int ARRAY_BUFFER = 34962;
		constexpr int ELEMENT_ARRAY_BUFFER = 34963;
		constexpr int TRIANGLES = 4;

		void Append(std::vector<unsigned char>& out, const void* data, size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			out.insert(out.end(), bytes, bytes + size);
		}
		void PutU32(std::vector<unsigned char>& out, uint32_t value)
		{
			for (int i = 0; i < 4; i++) {
				out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
			}
		}
		void Pad4(std::vector<unsigned char>& out, unsigned char with)
		{
			while (out.size() % 4 != 0) {
				out.push_back(with);
			}
		}
	}

	std::vector<unsigned char> EncodeGlb(const std::vector<Node>& nodes, const std::vector<Material>& materials, const std::string& generator)
	{
		static_assert(sizeof(glm::vec3) == 12 && sizeof(glm::vec2) == 8, "tightly packed vectors");
		Json j;
		j["asset"] = { { "version", "2.0" }, { "generator", generator } };
		Json jsonNodes = Json::array(), meshes = Json::array(), accessors = Json::array(), views = Json::array();
		std::vector<unsigned char> bin;

		//Adds a buffer view over data appended to the binary chunk (4-byte aligned) and an accessor over it
		auto addAccessor = [&](const void* data, size_t bytes, size_t count, int componentType, const char* type, int target) {
			Pad4(bin, 0);
			views.push_back({ { "buffer", 0 }, { "byteOffset", bin.size() }, { "byteLength", bytes }, { "target", target } });
			Append(bin, data, bytes);
			accessors.push_back({ { "bufferView", views.size() - 1 }, { "componentType", componentType }, { "count", count }, { "type", type } });
			return static_cast<int>(accessors.size() - 1);
		};

		for (const Node& node : nodes) {
			Json primitives = Json::array();
			for (const Primitive& p : node.primitives) {
				const terrainmesh::Mesh* mesh = p.mesh;
				if (!mesh || mesh->indices.empty() || mesh->positions.empty()) {
					continue;
				}
				const size_t count = mesh->positions.size();
				Json attributes;
				const int position = addAccessor(mesh->positions.data(), count * sizeof(glm::vec3), count, FLOAT, "VEC3", ARRAY_BUFFER);
				//POSITION needs its bounds
				glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
				for (const glm::vec3& v : mesh->positions) {
					low = glm::min(low, v);
					high = glm::max(high, v);
				}
				accessors[position]["min"] = { low.x, low.y, low.z };
				accessors[position]["max"] = { high.x, high.y, high.z };
				attributes["POSITION"] = position;
				if (mesh->normals.size() == count) {
					attributes["NORMAL"] = addAccessor(mesh->normals.data(), count * sizeof(glm::vec3), count, FLOAT, "VEC3", ARRAY_BUFFER);
				}
				if (mesh->uvs.size() == count) {
					attributes["TEXCOORD_0"] = addAccessor(mesh->uvs.data(), count * sizeof(glm::vec2), count, FLOAT, "VEC2", ARRAY_BUFFER);
				}
				const int indices = addAccessor(mesh->indices.data(), mesh->indices.size() * sizeof(uint32_t), mesh->indices.size(), UNSIGNED_INT,
					"SCALAR", ELEMENT_ARRAY_BUFFER);

				Json primitive = { { "attributes", attributes }, { "indices", indices }, { "mode", TRIANGLES } };
				if (p.material >= 0 && p.material < static_cast<int>(materials.size())) {
					primitive["material"] = p.material;
				}
				primitives.push_back(primitive);
			}
			if (primitives.empty()) {
				continue;
			}
			meshes.push_back({ { "name", node.name }, { "primitives", primitives } });
			jsonNodes.push_back({ { "name", node.name }, { "mesh", meshes.size() - 1 } });
		}

		//glTF arrays, when present, must not be empty
		j["scene"] = 0;
		if (jsonNodes.empty()) {
			j["scenes"] = Json::array({ Json::object() });
		}
		else {
			Json sceneNodes = Json::array();
			for (size_t n = 0; n < jsonNodes.size(); n++) {
				sceneNodes.push_back(n);
			}
			j["scenes"] = Json::array({ { { "nodes", sceneNodes } } });
			j["nodes"] = jsonNodes;
			j["meshes"] = meshes;
		}

		if (!materials.empty()) {
			Json jsonMaterials = Json::array(), images = Json::array(), textures = Json::array();
			//One image per path, one texture per image and sampler
			auto texture = [&](const std::string& uri, int sampler) {
				size_t image = 0;
				while (image < images.size() && images[image]["uri"] != uri) {
					image++;
				}
				if (image == images.size()) {
					images.push_back({ { "uri", uri } });
				}
				for (size_t t = 0; t < textures.size(); t++) {
					if (textures[t]["source"] == image && textures[t]["sampler"] == sampler) {
						return t;
					}
				}
				textures.push_back({ { "source", image }, { "sampler", sampler } });
				return textures.size() - 1;
			};
			for (const Material& m : materials) {
				const int sampler = m.repeat ? 1 : 0;
				Json pbr = { { "baseColorFactor", { m.color.r, m.color.g, m.color.b, m.color.a } }, { "metallicFactor", m.ormTexture.empty() ? 0.0f : 1.0f },
					{ "roughnessFactor", m.roughness } };
				Json material = { { "name", m.name } };
				if (!m.texture.empty()) {
					pbr["baseColorTexture"] = { { "index", texture(m.texture, sampler) } };
				}
				if (!m.ormTexture.empty()) {
					const size_t orm = texture(m.ormTexture, sampler);
					pbr["metallicRoughnessTexture"] = { { "index", orm } };
					material["occlusionTexture"] = { { "index", orm } };
				}
				if (!m.normalTexture.empty()) {
					material["normalTexture"] = { { "index", texture(m.normalTexture, sampler) } };
				}
				material["pbrMetallicRoughness"] = pbr;
				if (m.blend) {
					material["alphaMode"] = "BLEND";
				}
				else if (m.mask) {
					material["alphaMode"] = "MASK";
					material["alphaCutoff"] = 0.5f;
				}
				if (m.doubleSided) {
					material["doubleSided"] = true;
				}
				jsonMaterials.push_back(material);
			}
			j["materials"] = jsonMaterials;
			if (!images.empty()) {
				//Linear filtering with mipmaps; 0 = clamped at the edge (maps of the terrain), 1 = repeated (models)
				j["samplers"] = Json::array({ { { "magFilter", 9729 }, { "minFilter", 9987 }, { "wrapS", 33071 }, { "wrapT", 33071 } },
					{ { "magFilter", 9729 }, { "minFilter", 9987 }, { "wrapS", 10497 }, { "wrapT", 10497 } } });
				j["images"] = images;
				j["textures"] = textures;
			}
		}

		Pad4(bin, 0);
		if (!bin.empty()) {
			j["accessors"] = accessors;
			j["bufferViews"] = views;
			j["buffers"] = Json::array({ { { "byteLength", bin.size() } } });
		}

		std::string text = j.dump();
		while (text.size() % 4 != 0) {
			text += ' ';
		}
		std::vector<unsigned char> out;
		const size_t total = 12 + 8 + text.size() + (bin.empty() ? 0 : 8 + bin.size());
		out.reserve(total);
		PutU32(out, 0x46546C67u);	//"glTF"
		PutU32(out, 2);
		PutU32(out, static_cast<uint32_t>(total));
		PutU32(out, static_cast<uint32_t>(text.size()));
		PutU32(out, 0x4E4F534Au);	//"JSON"
		Append(out, text.data(), text.size());
		if (!bin.empty()) {
			PutU32(out, static_cast<uint32_t>(bin.size()));
			PutU32(out, 0x004E4942u);	//"BIN\0"
			Append(out, bin.data(), bin.size());
		}
		return out;
	}

	namespace {
		void PutFloat(std::string& out, float value)
		{
			char buffer[32];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			out.append(buffer, result.ptr);
		}
		void PutInt(std::string& out, size_t value)
		{
			char buffer[24];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			out.append(buffer, result.ptr);
		}
	}

	namespace {
		//1-based indices of the next vertex, texture coordinate and normal of an OBJ file
		struct ObjBase {
			size_t position = 1, uv = 1, normal = 1;
		};

		void PutObjMesh(std::string& out, const terrainmesh::Mesh& mesh, ObjBase& base)
		{
			const size_t count = mesh.positions.size();
			const bool normals = mesh.normals.size() == count;
			const bool uvs = mesh.uvs.size() == count;
			out.reserve(out.size() + count * (normals && uvs ? 90 : 40) + mesh.indices.size() * 12);
			for (const glm::vec3& p : mesh.positions) {
				out += "v ";
				PutFloat(out, p.x);
				out += ' ';
				PutFloat(out, p.y);
				out += ' ';
				PutFloat(out, p.z);
				out += '\n';
			}
			if (uvs) {
				for (const glm::vec2& t : mesh.uvs) {
					out += "vt ";
					PutFloat(out, t.x);
					out += ' ';
					PutFloat(out, 1.0f - t.y);
					out += '\n';
				}
			}
			if (normals) {
				for (const glm::vec3& n : mesh.normals) {
					out += "vn ";
					PutFloat(out, n.x);
					out += ' ';
					PutFloat(out, n.y);
					out += ' ';
					PutFloat(out, n.z);
					out += '\n';
				}
			}
			for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
				out += 'f';
				for (int k = 0; k < 3; k++) {
					const size_t v = mesh.indices[t + k];
					out += ' ';
					PutInt(out, base.position + v);
					if (uvs || normals) {
						out += '/';
						if (uvs) {
							PutInt(out, base.uv + v);
						}
						if (normals) {
							out += '/';
							PutInt(out, base.normal + v);
						}
					}
				}
				out += '\n';
			}
			base.position += count;
			base.uv += uvs ? count : 0;
			base.normal += normals ? count : 0;
		}
	}

	std::string EncodeObj(const std::vector<Node>& nodes, const std::string& generator)
	{
		std::string out = "# " + generator + "\n# metres, Y up\n";
		//OBJ indices are 1-based and global over the file
		ObjBase base;
		for (const Node& node : nodes) {
			bool named = false;
			for (const Primitive& primitive : node.primitives) {
				if (!primitive.mesh || primitive.mesh->indices.empty() || primitive.mesh->positions.empty()) {
					continue;
				}
				if (!named) {
					out += "o " + node.name + "\n";
					named = true;
				}
				PutObjMesh(out, *primitive.mesh, base);
			}
		}
		return out;
	}
}
