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
		mesh(nullptr), meshIndices(nullptr), octaves(8), prevOpt(noise::Options::NOTHING),
		scale(400), constrast(1.2f), option(noise::Options::NOTHING),
		deltaTime(0.0f), lastFrame(0.0f), camera(800, 600), lightSource()
	{
		checkSum = (float)(octaves + scale + constrast);
		mesh = new float[width * height * 6];
		// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
		meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];

		utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", mesh, meshIndices, width, height, scale, octaves, constrast, option, true, true);

		GLCALL(glEnable(GL_BLEND));
		GLCALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

		//Mesh setup
		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(mesh, (height * width) * 6 * sizeof(float));
		m_IndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
		m_Shader = std::make_unique<Shader>("res/shaders/mesh_vertex.shader", "res/shaders/mesh_fragment.shader");

		VertexBufferLayout layout;
		layout.Push<float>(3);
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

		if (prevOpt != option || checkSum - ((float)octaves + scale + constrast) != 0)
		{
			checkSum = (float)octaves + scale + constrast;
			prevOpt = option;
			utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", mesh, meshIndices, width, height, scale, octaves, constrast, option, true, false);
			m_VertexBuffer->UpdateData(mesh, (height * width) * 6 * sizeof(float));
		}

		m_Shader->Bind();
		m_Shader->SetUniform3fv("objectColor", glm::vec3(1.0f, 0.5f, 0.31f));
		m_Shader->SetUniform3fv("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(-0.5f, -0.25f, 0.0f));

		m_Shader->SetUniformMat4f("model", model);
		m_Shader->SetUniformMat4f("view", *camera.GetViewMatrix());
		m_Shader->SetUniformMat4f("projection", *camera.GetProjectionMatrix());
		m_Shader->SetUniform3fv("lightPos", lightSource.GetPosition());

		lightSource.Draw(renderer, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

	void TestNoiseMesh::OnImGuiRender()
	{
		ImGui::SliderInt("Octaves", &octaves, 1, 10);
		ImGui::SliderFloat("Scale", &scale, 100.0f, 1000.0f);
		ImGui::SliderFloat("Constrast", &constrast, 0.1f, 2.0f);
		static const char* options[] = { "NOTHING", "FLATTEN_NEGATIVES", "REVERT_NEGATIVES", "REFIT_BASIC"};
		static int current_option = static_cast<int>(option);

		// Combo box
		if (ImGui::BeginCombo("Noise Options", options[current_option]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(options); n++)
			{
				bool is_selected = (current_option == n);
				if (ImGui::Selectable(options[n], is_selected)) {
					current_option = n;
					option = static_cast<noise::Options>(n);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
}