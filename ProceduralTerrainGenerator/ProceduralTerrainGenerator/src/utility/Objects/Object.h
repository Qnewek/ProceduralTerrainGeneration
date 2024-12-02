#pragma once

#include <string>
#include <unordered_map>

namespace object
{
	//Basiec vertex structure
	//Contains position (x, y, z), normal (nx, ny, nz) vector and texture (u, v) coordinates
	struct vertex {
		float x, y, z;
		float nx, ny, nz;
		float u, v;

		vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v) : x(x), y(y), z(z), nx(nx), ny(ny), nz(nz), u(u), v(v) {}
	};
	struct material
	{
		int id;
		std::string texturePath;
		float ambient[3];
		float diffuse[3];
		float specular[3];
		float shininess;
	};
	class Object
	{
	public:
		Object();
		Object(int id, std::string name);
		~Object();

		bool isSpecified();
		bool asignVertices(vertex*& vertices);
		bool asignIndices(unsigned int*& indices);
		bool addMaterial(material& mat);

	private:
		unsigned int* m_MeshIndices;
		vertex* m_MeshVertices;
		std::string m_Name;
		std::string texAtlasPath;
		std::unordered_map<int, material> materials;
		int id;
	};
}
