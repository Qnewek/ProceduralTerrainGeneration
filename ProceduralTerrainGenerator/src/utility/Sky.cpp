#include "Sky.h"

#include <iostream>

#include <GL/glew.h>

bool Sky::Initialize()
{
	shader = std::make_unique<Shader>("res/shaders/sky_vertex.shader", "res/shaders/sky_fragment.shader");
	if (!shader->IsValid()) {
		std::cout << "[ERROR] Sky: the shader could not be built" << std::endl;
		return false;
	}
	vao = std::make_unique<VertexArray>();
	return true;
}

void Sky::Draw(Camera& camera, const LightSource& sun)
{
	//Only the rotation of the camera matters for the sky
	const glm::mat4 rotation = glm::mat4(glm::mat3(*camera.GetViewMatrix()));
	shader->Bind();
	shader->SetUniformMat4f("inverseViewProjection", glm::inverse(*camera.GetProjectionMatrix() * rotation));
	shader->SetUniform3fv("sunDirection", sun.GetDirection());
	vao->Bind();
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glEnable(GL_DEPTH_TEST);
	shader->Unbind();
}
