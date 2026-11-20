#pragma once

#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "TerrainMesh.h"

//Mesh files of the export pack: binary glTF 2.0 (.glb, one buffer, 32-bit indices) and Wavefront OBJ. Both keep the axes of
//terrainmesh (metres, Y up, right-handed), which are the axes of glTF and of the usual OBJ.
namespace meshfile {
	struct Material {
		std::string name;
		glm::vec4 color = glm::vec4(1.0f);	//base color (linear) and alpha
		std::string texture;				//base color image, a path relative to the mesh file (an external file); empty = none
		bool blend = false;					//alpha blending (water)
		float roughness = 1.0f;
	};

	//A named mesh in the scene; nodes are not nested
	struct Node {
		std::string name;
		const terrainmesh::Mesh* mesh = nullptr;
		int material = -1;		//index in the materials, -1 = none
	};

	//Binary glTF: the JSON chunk and a binary chunk with every vertex and index of the nodes. Nodes without triangles are left out
	std::vector<unsigned char> EncodeGlb(const std::vector<Node>& nodes, const std::vector<Material>& materials, const std::string& generator);

	//OBJ text with one object ("o") per node; texture coordinates have v flipped (OBJ counts v from the bottom of the image).
	//Materials are not written
	std::string EncodeObj(const std::vector<Node>& nodes, const std::string& generator);
}
