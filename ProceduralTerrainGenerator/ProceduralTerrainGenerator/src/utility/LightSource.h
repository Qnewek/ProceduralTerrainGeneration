#pragma once

#include "VertexBufferLayout.h"
#include "glm/glm.hpp"

class LightSource
{
public:
	LightSource(glm::vec3 lightPos, float _size);
	~LightSource();

	void Initialize();
	void Draw(Renderer& renderer, glm::mat4& view, glm::mat4& projection);
	void ImGuiDraw();

	void SetPosition(glm::vec3 position) { lightPos = position; }
	void SetAmbient(glm::vec3 amb) { ambient = amb; }
	void SetDiffuse(glm::vec3 diff) { diffuse = diff; }
	void SetSpecular(glm::vec3 spec) { specular = spec; }

	const glm::vec3& GetPosition() { return lightPos; }
	const glm::vec3& GetAmbient() { return ambient; }
	const glm::vec3& GetDiffuse() { return diffuse; }
	const glm::vec3& GetSpecular() { return specular; }
	float GetSize() { return size; };

private:
	glm::vec3 lightPos, ambient, diffuse, specular;
	float size;

	//OpenGL stuff
	std::unique_ptr<VertexArray> m_VAO;
	std::unique_ptr < IndexBuffer> m_IndexBuffer;
	std::unique_ptr < Shader> m_Shader;
	std::unique_ptr < VertexBuffer> m_VertexBuffer;
};