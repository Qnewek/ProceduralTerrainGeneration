#include "LightSource.h"

#include "utilities.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "imgui/imgui.h"

#include <iostream>

LightSource::LightSource(glm::vec3 lightPos, float _size) : lightPos(lightPos), size(_size), ambient(0.2f, 0.2f, 0.2f), diffuse(0.5f, 0.5f, 0.5f), specular(1.0f, 1.0f, 1.0f)
{
}

LightSource::~LightSource()
{

}

void LightSource::Initialize() {
	float cubeVertices[] = {
		// Front row
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		// Back row
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f
	};

	unsigned int cubeIndices[] = {
		// Front
		0, 1, 2, 0, 2, 3,
		// Back 
		4, 6, 5, 4, 7, 6,
		// Left 
		4, 0, 3, 4, 3, 7,
		// Right
		1, 5, 6, 1, 6, 2,
		// Top (y = +1)
		3, 2, 6, 3, 6, 7,
		// Bottom 
		4, 5, 1, 4, 1, 0
	};

	m_VAO = std::make_unique<VertexArray>();
	m_VertexBuffer = std::make_unique<VertexBuffer>(cubeVertices, 24 * sizeof(float));
	m_IndexBuffer = std::make_unique<IndexBuffer>(cubeIndices, 36);
	m_Shader = std::make_unique<Shader>("res/shaders/Light_source_vertex.shader", "res/shaders/Light_source_fragment.shader");


	VertexBufferLayout layout;
	layout.Push<float>(3);

	m_VAO->AddBuffer(*m_VertexBuffer, layout);
	std::cout << "[LOG] Light source initialized" << std::endl;
}

void LightSource::Draw(Renderer& renderer, glm::mat4& view, glm::mat4& projection) {
	m_Shader->Bind();
	m_Shader->SetUniformMat4f("view", view);
	m_Shader->SetUniformMat4f("projection", projection);

	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, lightPos);
	model = glm::scale(model, glm::vec3(size));
	m_Shader->SetUniformMat4f("model", model);

	renderer.DrawTriangles(*m_VAO, *m_IndexBuffer, *m_Shader);
	m_Shader->Unbind();
}

void LightSource::ImGuiDraw() {
	if (ImGui::CollapsingHeader("Light parameters:")) {
		ImGui::DragFloat3("Light source position", (float*)&lightPos, 1.0f);
		ImGui::DragFloat("Light size", &size, 0.01f, 0.01f, 10.0f);
		ImGui::ColorEdit3("Ambient", (float*)&ambient);
		ImGui::ColorEdit3("Diffuse", (float*)&diffuse);
		ImGui::ColorEdit3("Specular", (float*)&specular);
		ImGui::Checkbox("Light On/Off", &lightOn);
	}
}

void LightSource::SetLightUniforms(Shader& shader)
{
	shader.SetLightUniforms(lightPos, ambient, diffuse, specular);
	shader.SetUniform1i("lightOn", lightOn);
}
