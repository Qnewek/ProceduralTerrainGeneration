#pragma once

#include <string>
#include <unordered_map>

namespace object
{
	//Basic vertex structure
	//Contains position (x, y, z), normal (nx, ny, nz) vector and texture (u, v) coordinates
	struct vertex {
		float x, y, z;
		float nx, ny, nz;
		float u, v;
		int materialId;

		vertex(float x = 0.0f, float y = 0.0f, float z = 0.0f, float nx = 0.0f, float ny = 0.0f, float nz = 0.0f, float u = 0.0f, float v = 0.0f, int id = 0.0f) : x(x), y(y), z(z), nx(nx), ny(ny), nz(nz), u(u), v(v), materialId(id) {}
	};
	struct material
	{
		std::string texFileName;
		float ambient[3];
		float diffuse[3];
		float specular[3];
		float shininess;

		material() : texFileName(""), ambient{0.0f, 0.0f, 0.0f}, diffuse{0.0f, 0.0f, 0.0f}, specular{0.0f, 0.0f, 0.0f}, shininess(0.0f) {}
		material(std::string& fileName, float ambient[3], float diffuse[3], float specular[3], float shininess) : texFileName(fileName), ambient{ambient[0], ambient[1], ambient[2]}, diffuse{diffuse[0], diffuse[1], diffuse[2]}, specular{specular[0], specular[1], specular[2]}, shininess(shininess) {}
	};
	struct faceTriangle
	{
		unsigned int v1, v2, v3;

		faceTriangle(unsigned int v1 = 0, unsigned int v2 = 0, unsigned int v3 = 0) : v1(v1), v2(v2), v3(v3) {}
	};
	class Object
	{
	public:
		Object();
		Object(std::string name);
		~Object();

		bool isSpecified();
		
		bool asignVertices(vertex*& vertices);
		bool asignIndices(faceTriangle*& indices);
		void addMaterial(const material& mat, int id);
		void asignDirPath(const std::string& path) { dirPath = path;}

		material& getMaterial(int id) { return materials[id]; }
		static int getCount() { return count; }

	private:
		static int count;
		int id;
		faceTriangle* m_MeshIndices;
		vertex* m_MeshVertices;
		std::string m_Name;
		std::string dirPath;
		std::unordered_map<int, material> materials;
	};
}
