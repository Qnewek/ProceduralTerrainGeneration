#pragma once

#include <string>
#include <unordered_map>
#include <array>

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
		std::string ambientFileName;
		std::string diffuseFileName;
		std::string specularFileName;

		float ambient[3];
		float diffuse[3];
		float specular[3];
		float shininess;

		material() : ambientFileName(""), diffuseFileName(""), specularFileName(""), ambient{0.0f, 0.0f, 0.0f}, diffuse{0.0f, 0.0f, 0.0f}, specular{0.0f, 0.0f, 0.0f}, shininess(0.0f) {}
		material(const std::string& diffuse_texname, const std::string& ambient_texname, const std::string& specular_texname,
			const std::array<float, 3>& ambient, const std::array<float, 3>& diffuse, const std::array<float, 3>& specular, float shininess) :diffuseFileName(diffuse_texname), ambientFileName(ambient_texname), specularFileName(specular_texname), ambient{ ambient[0], ambient[1], ambient[2] }, diffuse{ diffuse[0], diffuse[1], diffuse[2] }, specular{ specular[0], specular[1], specular[2] }, shininess(shininess) {}
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
		
		bool asignVertices(vertex*& vertices, int size);
		bool asignIndices(faceTriangle*& indices);
		void addMaterial(material mat);
		void asignDirPath(const std::string& path) { dirPath = path;}

		material& getMaterial(int id) { return materials[id]; }
		std::unordered_map<int, material>& getMaterials() { return materials; }
		static int getCount() { return count; }
		int getVerticesCount() { return m_VerticesCount; }
		vertex* getVertices() { return m_MeshVertices; }
		faceTriangle* getIndices() { return m_MeshIndices; }
		std::string getDirPath() { return dirPath; }

	private:
		static int count;
		int id;
		int m_VerticesCount = 0;
		faceTriangle* m_MeshIndices;
		vertex* m_MeshVertices;
		std::string m_Name;
		std::string dirPath;
		std::unordered_map<int, material> materials;
	};
}
