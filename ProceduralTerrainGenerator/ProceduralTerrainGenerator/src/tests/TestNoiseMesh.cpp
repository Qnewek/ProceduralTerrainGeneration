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
	TestNoiseMesh::TestNoiseMesh() :height(100), width(100),
		mesh(nullptr), textureSeed(nullptr), map(nullptr), meshIndices(nullptr), octaves(8), prevOpt(noise::Options::REVERT_NEGATIVES),
		scale(400), constrast(1.2f), redistribution(1.0f), option(noise::Options::REVERT_NEGATIVES),
		deltaTime(0.0f), lastFrame(0.0f), camera(800, 600), lightSource()
	{
		checkSum = (float)(octaves + scale + constrast + redistribution);
		
		map = new float[height * width];
		textureSeed = new float[height * width];

		// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
		meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
		mesh = new float[width * height * 8];

		noise::getNoiseMap(textureSeed, width, height, scale, octaves, constrast, redistribution, noise::Options::REVERT_NEGATIVES);
		utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", mesh, map, meshIndices, width, height, 8, scale, octaves, constrast, redistribution, option, true, true);
		utilities::PaintBiome(mesh, map, textureSeed, width, height, 8, 6);

		GLCALL(glEnable(GL_BLEND));
		GLCALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

		//Mesh setup
		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(mesh, (height * width) * 8 * sizeof(float));
		m_IndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
		m_Shader = std::make_unique<Shader>("res/shaders/Lightning_vertex.shader", "res/shaders/Lightning_fragment.shader");
		m_Texture = std::make_unique<Texture>("res/textures/Basic_biome_texture_palette.jpg");

		VertexBufferLayout layout;
		layout.Push<float>(3);
		layout.Push<float>(3);
		layout.Push<float>(2);

		m_VAO->AddBuffer(*m_VertexBuffer, layout);
	}

	TestNoiseMesh::~TestNoiseMesh()
	{
		delete[] mesh;
		delete[] meshIndices;
		delete[] map;
		delete[] textureSeed;
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

		if (prevOpt != option || checkSum - ((float)octaves + scale + constrast + redistribution) != 0)
		{
			checkSum = (float)octaves + scale + constrast + redistribution;
			prevOpt = option;
			utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", mesh, map, meshIndices, width, height, 8, scale, octaves, constrast, redistribution, option, true, false);
			utilities::PaintBiome(mesh, map, textureSeed, width, height, 8, 6);
			m_VertexBuffer->UpdateData(mesh, (height * width) * 8 * sizeof(float));
		}

		m_Shader->Bind();

		//m_Shader->SetUniform3fv("material.ambient", glm::vec3(0.2f, 0.2f, 0.2f));
		//m_Shader->SetUniform3fv("material.diffuse", glm::vec3(1.0f, 0.5f, 0.31f));
		m_Shader->SetUniform3fv("material.specular", glm::vec3(0.5f, 0.5f, 0.5f));
		m_Shader->SetUniform1f("material.shininess", 16.0f);

		m_Shader->SetUniform3fv("light.ambient",  glm::vec3(0.2f, 0.2f, 0.2f));
		m_Shader->SetUniform3fv("light.diffuse",  glm::vec3(0.5f, 0.5f, 0.5f));
		m_Shader->SetUniform3fv("light.specular", glm::vec3(1.0f, 1.0f, 1.0f));

		m_Shader->SetUniform3fv("light.position", lightSource.GetPosition());
		m_Shader->SetUniform3fv("viewPos", camera.GetPosition());

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(-0.5f, -0.25f, 0.0f));

		m_Shader->SetUniformMat4f("model", model);
		m_Shader->SetUniformMat4f("view", *camera.GetViewMatrix());
		m_Shader->SetUniformMat4f("projection", *camera.GetProjectionMatrix());

		lightSource.Draw(renderer, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
		
		m_Texture->Bind();
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

	void TestNoiseMesh::OnImGuiRender()
	{
		ImGui::SliderInt("Octaves", &octaves, 1, 10);
		ImGui::SliderFloat("Scale", &scale, 100.0f, 1000.0f);
		ImGui::SliderFloat("Constrast", &constrast, 0.1f, 2.0f);
		ImGui::SliderFloat("Redistribution", &redistribution, 0.1f, 10.0f);

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