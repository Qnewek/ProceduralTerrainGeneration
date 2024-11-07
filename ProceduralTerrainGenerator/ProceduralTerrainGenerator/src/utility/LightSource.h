#pragma once

#include "VertexBuffer.h"
#include "VertexBufferLayout.h"
#include "Renderer.h"

#include "glm/glm.hpp"

class LightSource
{
public:
	LightSource(glm::vec3 lightPos, float size);
	~LightSource();

	void Draw(Renderer& renderer, glm::mat4& view, glm::mat4& projection);

	void SetPosition(glm::vec3 position) { lightPos = position; }
	glm::vec3 GetPosition() { return lightPos; }
	void SetSize(float size) { m_Size = size; }

private:
	glm::vec3 lightPos;
	float m_Size;
	float* vertices;
	unsigned int* indices;

	//OpenGL stuff
	std::unique_ptr<VertexArray> m_VAO;
	std::unique_ptr < IndexBuffer> m_IndexBuffer;
	std::unique_ptr < Shader> m_Shader;
	std::unique_ptr < VertexBuffer> m_VertexBuffer;
};