#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "glm/glm.hpp"

//Meshes of vegetation and rock models: glTF loading, levels of detail and their cache on disk. No OpenGL here.
//Models are in metres, as in glTF; the base of a model (where it stands) is moved to the origin, y up.
//One file may hold several variants of a model side by side (Poly Haven does that); each becomes its own variant.
namespace vegetation
{
	struct Vertex {
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec2 uv = glm::vec2(0.0f);
	};

	//Material of a model; texture paths are relative to the folder of the model file, empty = none
	struct Material {
		std::string name;
		std::string albedo, normal, arm, alpha;	//arm = ambient occlusion, roughness, metalness in R, G, B (glTF)
		bool alphaTest = false;		//cut out where the alpha map is below one half
		bool foliage = false;		//simplified by dropping cards instead of collapsing edges
	};

	//Geometry of one material, before levels of detail
	struct SourcePart {
		int material = 0;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
	};
	//One variant of a model; parts index the material list of the file
	struct SourceModel {
		std::vector<SourcePart> parts;	//at most one per material
		size_t TriangleCount() const;
	};

	//A range of ModelMesh::indices drawn with one material
	struct Part {
		int material = 0;
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
	};
	struct Lod {
		std::vector<Part> parts;
		uint32_t triangles = 0;
	};
	struct Variant {
		std::vector<Lod> lods;			//nearest first
		glm::vec3 boundsMin = glm::vec3(0.0f), boundsMax = glm::vec3(0.0f);
		size_t sourceTriangles = 0;		//triangles of the variant as loaded
	};
	//All levels of detail of all variants of a model in one vertex and index array (indices are absolute)
	struct ModelMesh {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<Material> materials;
		std::vector<Variant> variants;
	};

	//Material names treated as foliage when the material is not alpha-tested: leaf, leaves, twig, needle (any case)
	bool IsFoliageName(const std::string& name);

	//Loads all triangle primitives of a glTF file (with its buffers), transformed by their nodes and merged per material.
	//Every root node of the scene with its children is one variant, unless its footprint (XZ) overlaps another one's:
	//then they are pieces of one model and stay together. Normals are computed where the file has none
	//@return false and error if the file cannot be read
	bool LoadGltf(const std::filesystem::path& file, std::vector<Material>& materials, std::vector<SourceModel>& variants, std::string& error);

	//Moves the model so it stands on the origin: lowest point at y = 0, the middle of its lowest part (trunk) at x = z = 0
	void MoveBaseToOrigin(SourceModel& model);

	//Simplified copy of a surface (trunk, branches, rock) with about targetTriangles triangles, by collapsing edges.
	//A surface of many pieces (branches) first loses its smallest pieces until the rest can get close to the target;
	//a surface of a few pieces (rock, trunk) falls back to vertex clustering instead. A part already under the target is copied
	void SimplifySurface(const SourcePart& part, size_t targetTriangles, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
	//Simplified copy of foliage with about targetTriangles triangles: keeps a part of its connected pieces (leaf cards, twigs),
	//picked evenly, and enlarges each kept piece around its centre so they cover about the same area as all of them did
	void SimplifyCards(const SourcePart& part, size_t targetTriangles, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

	//Levels of detail of every variant with about these numbers of triangles (nearest first). Foliage gets at least
	//foliageShare of a level (it shapes the outline of a tree), each part a share proportional to its triangles within its kind;
	//a level never has more triangles in a part than the level before
	ModelMesh BuildLods(const std::vector<Material>& materials, const std::vector<SourceModel>& variants, const std::vector<uint32_t>& targetTriangles);
	constexpr float foliageShare = 0.6f;

	//Cache of a ModelMesh on disk. key identifies the source and the settings it was built with
	bool SaveCache(const std::filesystem::path& file, const ModelMesh& mesh, uint64_t key);
	//@return false if the file is missing, damaged, of another format version or built for another key
	bool LoadCache(const std::filesystem::path& file, ModelMesh& mesh, uint64_t key);
}
