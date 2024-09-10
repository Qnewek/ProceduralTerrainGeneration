#include "LightSource.h"

#include "utilities.h"

#include <memory>

LightSource::LightSource() :
	lightPos(glm::vec3(1.2f, 1.0f, 2.0f))
{
	utilities::GenCubeLayout(vertices, indices);
	m_Shader = std::make_unique<Shader>("res/shaders/light_source_vertex.shader", "res/shaders/light_source_fragment.shader");
	m_VAO = std::make_unique<VertexArray>();
	m_VertexBuffer = std::make_unique<VertexBuffer>(24 * sizeof(float));
	m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 36);

	VertexBufferLayout layout;
	layout.Push<float>(3);

	m_VAO->AddBuffer(*m_VertexBuffer, layout);
}

LightSource::~LightSource()
{
	delete[] vertices;
	delete[] indices;
}