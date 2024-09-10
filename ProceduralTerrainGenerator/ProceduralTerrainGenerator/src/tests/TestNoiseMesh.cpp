#include "TestNoiseMesh.h"

#include "Renderer.h"
#include "imgui/imgui.h"
#include "Noise.h"
#include "utilities.h"
#include "Camera.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <iostream>
#include <memory>

namespace test
{
	TestNoiseMesh::TestNoiseMesh() :height(500), width(500),
		mesh(nullptr), meshIndices(nullptr), octaves(8), prevOpt(noise::Options::REVERT_NEGATIVES),
		scale(400), constrast(1.2f), option(noise::Options::REVERT_NEGATIVES), 
		deltaTime(0.0f), lastFrame(0.0f), camera(800, 600)
	{
		mesh = new float[width * height*3];
		// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
		meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];

		utilities::benchmark_void(noise::getNoiseMesh, "getNoiseMesh", mesh, height, width, scale, octaves, constrast, option);
		utilities::SimpleMeshIndicies(meshIndices, width, height);

		GLCALL(glEnable(GL_BLEND));
		GLCALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

		//Mesh setup
		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(mesh, (height*width)* 3 * sizeof(float));
		m_IndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
		m_Shader = std::make_unique<Shader>("res/shaders/mesh_vertex.shader", "res/shaders/mesh_fragment.shader");

		//Lightning


		VertexBufferLayout layout;
		layout.Push<float>(3);

		m_VAO->AddBuffer(*m_VertexBuffer, layout);

	}

	TestNoiseMesh::~TestNoiseMesh()
	{
		delete[] mesh;
		delete[] meshIndices;
	}

	void TestNoiseMesh::OnUpdate(float deltaTime)
	{
	}

	void TestNoiseMesh::OnRender(GLFWwindow& window, Renderer& renderer)
	{
		renderer.Clear();

		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;
		camera.SteerCamera(&window, deltaTime);

		m_Shader->Bind();
		m_Shader->SetUniform4f("color", 0.90f, 0.27f, 0.30f, 1.0f);
		m_Shader->SetUniform4f("lightColor", 1.0f, 1.0f, 1.0f, 1.0f);

		glm::mat4 model = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 projection = glm::mat4(1.0f);

		model = glm::translate(model, glm::vec3(-0.5f, -0.25f, 0.0f));
		camera.GetViewMatrix(view);
		camera.GetProjectionMatrix(projection);

		m_Shader->SetUniformMat4f("model", model);
		m_Shader->SetUniformMat4f("view", view);
		m_Shader->SetUniformMat4f("projection", projection);

		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

	void TestNoiseMesh::OnImGuiRender()
	{
		ImGui::SliderInt("Octaves", &octaves, 1, 10);
		ImGui::SliderFloat("Scale", &scale, 100.0f, 1000.0f);
		ImGui::SliderFloat("Constrast", &constrast, 0.1f, 2.0f);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
}