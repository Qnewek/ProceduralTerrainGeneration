#include "LightSource.h"

#include "utilities.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <memory>
#include <iostream>

LightSource::LightSource(glm::vec3 lightPos, float size) :vertices(new float[24]), indices(new unsigned int[36]),
	lightPos(lightPos), m_Size(size)
{
	utilities::GenCubeLayout(vertices, indices, m_Size);

	m_VAO = std::make_unique<VertexArray>();
	m_VertexBuffer = std::make_unique<VertexBuffer>(vertices, 24 * sizeof(float));
	m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 36);
	m_Shader = std::make_unique<Shader>("res/shaders/light_source_vertex.shader", "res/shaders/light_source_fragment.shader");

	VertexBufferLayout layout;
	layout.Push<float>(3);

	m_VAO->AddBuffer(*m_VertexBuffer, layout);
}

LightSource::~LightSource()
{
	delete[] vertices;
	delete[] indices;
}

void LightSource::Draw(Renderer& renderer, glm::mat4& view, glm::mat4& projection) {
	m_Shader->Bind();
	m_Shader->SetUniformMat4f("view", view);
	m_Shader->SetUniformMat4f("projection", projection);

	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, lightPos);
	model = glm::scale(model, glm::vec3(0.2f));
	m_Shader->SetUniformMat4f("model", model);

	renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	m_Shader->Unbind();
}