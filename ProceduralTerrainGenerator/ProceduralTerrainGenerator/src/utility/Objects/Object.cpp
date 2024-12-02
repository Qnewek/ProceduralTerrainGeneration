#include "Object.h"

object::Object::Object() : m_Name("Object"), texAtlasPath(""), id(0), m_MeshVertices(nullptr), m_MeshIndices(nullptr)
{
}

object::Object::Object(int id, std::string name) : m_Name(name), texAtlasPath(""), id(id), m_MeshVertices(nullptr), m_MeshIndices(nullptr)
{
}

object::Object::~Object()
{
	if (m_MeshVertices)
		delete[] m_MeshVertices;
	if (m_MeshIndices)
		delete[] m_MeshIndices;
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

bool object::Object::asignIndices(unsigned int*& indices)
{
	if (indices)
	{
		m_MeshIndices = indices;
		indices = nullptr;
		return true;
	}
}

bool object::Object::addMaterial(material& mat)
{
	return false;
}
