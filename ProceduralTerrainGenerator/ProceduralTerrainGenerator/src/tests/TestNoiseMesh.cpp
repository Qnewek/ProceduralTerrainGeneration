#include "TestNoiseMesh.h"

#include "Renderer.h"
#include "Noise.h"
#include "Camera.h"
#include "utilities.h"

#include "imgui/imgui.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace test
{
	TestNoiseMesh::TestNoiseMesh() :height(200), width(200), stride(8),
		meshVertices(nullptr), meshIndices(nullptr),
		noise(width,height), biomeNoise(width, height),
		deltaTime(0.0f), lastFrame(0.0f), camera(800, 600), lightSource(), seed(0)
	{
		prevCheck.prevCheckSum	 = noise.getConfigRef().getCheckSum();
		prevCheck.prevOpt		 = noise.getConfigRef().option;
		prevCheck.prevRidge		 = noise.getConfigRef().ridge;
		prevCheck.prevIsland	 = noise.getConfigRef().island;
		prevCheck.prevIslandType = noise.getConfigRef().islandType;
		prevCheck.symmetrical	 = noise.getConfigRef().symmetrical;
		prevCheck.seed           = seed;

		// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
		meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
		meshVertices = new float[width * height * stride];

		//Initial noises setup
		biomeNoise.generateFractalNoise();
		utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, meshVertices, meshIndices, 8, true, true);
		utilities::PaintBiome(meshVertices, noise, biomeNoise, stride, 6);

		//Mesh setup
		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(meshVertices, (height * width) * stride * sizeof(float));
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
		delete[] meshVertices;
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

		if (prevCheck.prevOpt		 != noise.getConfigRef().option			|| 
			prevCheck.prevCheckSum	 != noise.getConfigRef().getCheckSum()	||
			prevCheck.prevRidge		 != noise.getConfigRef().ridge			||
			prevCheck.prevIsland	 != noise.getConfigRef().island			||
			prevCheck.prevIslandType != noise.getConfigRef().islandType		||
			prevCheck.symmetrical    != noise.getConfigRef().symmetrical	||
			prevCheck.seed			 != seed)	
		{
			noise.setSeed(seed);
			utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, meshVertices, meshIndices, 8, true, false);
			utilities::PaintBiome(meshVertices, noise, biomeNoise, stride, 6);
			
			m_VertexBuffer->UpdateData(meshVertices, (height * width) * stride * sizeof(float));

			prevCheck.prevCheckSum	 = noise.getConfigRef().getCheckSum();
			prevCheck.prevOpt		 = noise.getConfigRef().option;
			prevCheck.prevRidge		 = noise.getConfigRef().ridge;
			prevCheck.prevIsland	 = noise.getConfigRef().island;
			prevCheck.prevIslandType = noise.getConfigRef().islandType;
			prevCheck.symmetrical    = noise.getConfigRef().symmetrical;
			prevCheck.seed = seed;
		}

		m_Shader->Bind();

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

		if (testSymmetrical)
		{
			m_Shader->SetUniformMat4f("model", glm::translate(model, glm::vec3(-1.0f, 0.0f, 0.0f)));
			renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
			m_Shader->SetUniformMat4f("model", glm::translate(model, glm::vec3(1.0f, 0.0f, 0.0f)));
			renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
			m_Shader->SetUniformMat4f("model", glm::translate(model, glm::vec3(0.0f, 0.0f, 1.0f)));
			renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
			m_Shader->SetUniformMat4f("model", glm::translate(model, glm::vec3(0.0f, 0.0f, -1.0f)));
			renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
		}
	}

	void TestNoiseMesh::OnImGuiRender()
	{
		ImVec2 minSize = ImVec2(200, 200);
		ImVec2 maxSize = ImVec2(800, 800);

		ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
		ImGui::Begin("Noise Settings");

		//Seed
		ImGui::InputInt("Seed", &seed);

		//Basic noise settings
		ImGui::SliderInt("Octaves", &noise.getConfigRef().octaves, 1, 8);

		ImGui::SliderFloat("Offset x",		 &noise.getConfigRef().xoffset,			 0.0f, 5.0f);
		ImGui::SliderFloat("Offset y",		 &noise.getConfigRef().yoffset,			 0.0f, 5.0f);
		ImGui::SliderFloat("Scale",			 &noise.getConfigRef().scale,			 0.01f, 3.0f);
		ImGui::SliderFloat("Constrast",		 &noise.getConfigRef().constrast,		 0.1f, 2.0f);
		ImGui::SliderFloat("Redistribution", &noise.getConfigRef().redistribution,	 0.1f, 10.0f);
		ImGui::SliderFloat("Lacunarity",	 &noise.getConfigRef().lacunarity,		 0.1f, 10.0f);
		ImGui::SliderFloat("Persistance",	 &noise.getConfigRef().persistance,		 0.1f, 1.0f);

		//Dealing with negatives settings
		static const char* options[] = { "REFIT_ALL", "FLATTEN_NEGATIVES", "REVERT_NEGATIVES"};
		static int current_option = static_cast<int>(noise.getConfigRef().option);

		if (ImGui::BeginCombo("Negatives: ", options[current_option]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(options); n++)
			{
				bool is_selected = (current_option == n);
				if (ImGui::Selectable(options[n], is_selected)) {
					current_option = n;
					noise.getConfigRef().option = static_cast<noise::Options>(n);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (noise.getConfigRef().option == noise::Options::REVERT_NEGATIVES)
			ImGui::SliderFloat("Revert Gain", &noise.getConfigRef().revertGain, 0.1f, 1.0f);

		//Ridged noise settings
		ImGui::Checkbox("Ridge", &noise.getConfigRef().ridge);
		if (noise.getConfigRef().ridge)
		{
			ImGui::SliderFloat("Ridge Gain",   &noise.getConfigRef().ridgeGain,	  0.1f, 10.0f);
			ImGui::SliderFloat("Ridge Offset", &noise.getConfigRef().ridgeOffset, 0.1f, 10.0f);
		}

		//Island settings
		ImGui::Checkbox("Island", &noise.getConfigRef().island);
		if (noise.getConfigRef().island)
		{
			static const char* islandTypes[] = { "CONE", "DIAGONAL", "EUKLIDEAN_SQUARED", 
												 "SQUARE_BUMP","HYPERBOLOID", "SQUIRCLE",
												 "TRIG" };
			static int current_island = static_cast<int>(noise.getConfigRef().islandType);

			if (ImGui::BeginCombo("Island type: ", islandTypes[current_island]))
			{
				for (int n = 0; n < IM_ARRAYSIZE(islandTypes); n++)
				{
					bool is_selected = (current_island == n);
					if (ImGui::Selectable(islandTypes[n], is_selected)) {
						current_island = n;
						noise.getConfigRef().islandType = static_cast<noise::IslandType>(n);
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SliderFloat("Mix Power", &noise.getConfigRef().mixPower, 0.0f, 1.0f);
		}
		ImGui::Checkbox("Symmetrical", &noise.getConfigRef().symmetrical);
		if (noise.getConfigRef().symmetrical) {
			ImGui::Checkbox("Test Symmetrical", &testSymmetrical);
		}

		//FPS
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}
}