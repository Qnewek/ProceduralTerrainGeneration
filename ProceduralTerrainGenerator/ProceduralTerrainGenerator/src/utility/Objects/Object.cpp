#include "Object.h"

int object::Object::count = 0;

object::Object::Object() : m_Name("Object"), dirPath(""), id(0), m_MeshVertices(nullptr), m_MeshIndices(nullptr)
{
	count++;
}

object::Object::Object(std::string name) : m_Name(name), dirPath(""), id(count), m_MeshVertices(nullptr), m_MeshIndices(nullptr)
{
	count++;
}

object::Object::~Object()
{
	if (m_MeshVertices)
		delete[] m_MeshVertices;
	if (m_MeshIndices)
		delete[] m_MeshIndices;
	count--;
}

bool object::Object::isSpecified()
{
	if (m_MeshVertices && m_MeshIndices && id >= 0 && !m_Name.empty())
		return true;
	return false;
}

bool object::Object::asignVertices(vertex*& vertices)
{
	if (vertices)
	{
		m_MeshVertices = vertices;
		vertices = nullptr;
		return true;
	}
	return false;
}

bool object::Object::asignIndices(faceTriangle*& indices)
{
	if (indices)
	{
		m_MeshIndices = indices;
		indices = nullptr;
		return true;
	}
}

void object::Object::addMaterial(const material& mat, int id)
{
	materials.emplace(id, mat);
}
