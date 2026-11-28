#pragma once

#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "TerrainMesh.h"

//Mesh files of the export pack: binary glTF 2.0 (.glb, one buffer, 32-bit indices) and Wavefront OBJ. Both keep the axes of
//terrainmesh (metres, Y up, right-handed), which are the axes of glTF and of the usual OBJ.
namespace meshfile {
	//Images are paths relative to the mesh file (external files); empty = none
	struct Material {
		std::string name;
		glm::vec4 color = glm::vec4(1.0f);	//base color (linear) and alpha
		std::string texture;				//base color image (alpha in A)
		std::string normalTexture;			//tangent-space normal map (OpenGL convention, as glTF)
		std::string ormTexture;				//occlusion, roughness, metalness in R, G, B (one image for both glTF slots)
		bool blend = false;					//alpha blending (water)
		bool mask = false;					//alpha test at 0.5 (leaves); ignored with blend
		bool doubleSided = false;
		bool repeat = false;				//textures repeat (models); otherwise clamped at the edge (maps of the terrain)
		float roughness = 1.0f;				//multiplies ormTexture.g if given
	};

	//Triangles of a mesh drawn with one material
	struct Primitive {
		const terrainmesh::Mesh* mesh = nullptr;
		int material = -1;		//index in the materials, -1 = none
	};

	//A named mesh in the scene with one primitive per material; nodes are not nested
	struct Node {
		std::string name;
		std::vector<Primitive> primitives;
	};

	//Binary glTF: the JSON chunk and a binary chunk with every vertex and index of the nodes. Primitives without triangles are left out,
	//and nodes left with none
	std::vector<unsigned char> EncodeGlb(const std::vector<Node>& nodes, const std::vector<Material>& materials, const std::string& generator,
		std::string* error = nullptr);

	//OBJ text with one object ("o") per node holding all its primitives; texture coordinates have v flipped (OBJ counts v from the bottom
	//of the image). Materials are not written
	std::string EncodeObj(const std::vector<Node>& nodes, const std::string& generator);
}
