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
	TestNoiseMesh::TestNoiseMesh() :height(500), width(500), stride(8),
		meshVertices(nullptr), meshIndices(nullptr), traceVertices(nullptr), erosionVertices(nullptr),
		noise(width, height), biomeNoise(width, height), 
		erosionWindow(false), erosionPerform(false), testSymmetrical(false), trackDraw(false), erosionDraw(false),
		lightSource(glm::vec3(2.0f, 1.0f, 2.0f)), seed(0), erosion(width, height), meshColor(MONO),
		deltaTime(0.0f), lastFrame(0.0f), camera(800, 600)
	{
		//Update variable for checking the change in noise settings
		UpdatePrevCheckers();

		// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
		meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
		meshVertices = new float[width * height * stride];

		//Initial fractal noise generation in order to draw something on the start of the test
		utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, meshVertices, meshIndices, 8, true, true);
		
		//Simple biome based on fractal noise and sampling from texture
		biomeNoise.generateFractalNoise();
		if (meshColor == MONO)
			utilities::PaintGrey(meshVertices, width, height, stride, 6);
		else if (meshColor == PSEUDO_BIOME) {
			utilities::PaintBiome(meshVertices, noise, biomeNoise, stride, 6);
		}
		//OpenGl setup for drawing the terrain
		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(meshVertices, (height * width) * stride * sizeof(float));
		m_IndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
		m_Shader = std::make_unique<Shader>("res/shaders/Lightning_vertex.shader", "res/shaders/Lightning_fragment.shader");
		m_Texture = std::make_unique<Texture>("res/textures/Basic_biome_texture_palette.jpg");

		erosionBuffer = std::make_unique<VertexBuffer>(erosionVertices, (height * width) * stride * sizeof(float));

		//Layout of the vertex buffer
		//Succesively: 
		// 3 floats for position [x,y,z], 
		// 3 floats for normal vector indicating direction the vertex faces
		// 2 floats for texture coordinates based on height
		layout.Push<float>(3);
		layout.Push<float>(3);
		layout.Push<float>(2);

		m_VAO->AddBuffer(*m_VertexBuffer, layout);

		m_TrackShader = std::make_unique<Shader>("res/shaders/Trace_vertex.shader", "res/shaders/Trace_fragment.shader");
		m_TrackVAO = std::make_unique<VertexArray>();
	}

	TestNoiseMesh::~TestNoiseMesh()
	{
		delete[] meshVertices;
		delete[] meshIndices;
		delete[] traceVertices;
		delete[] erosionVertices;
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

		CheckChange();
		PerformErosion();

		//Translate model to be in the center of the screen at the time of first render
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(-0.5f, -0.25f, 0.0f));
		
		//Since we are using texture sampling we dont need to set ambient and diffuse color 
		//but there is no function that takes only some parameters
		m_Shader->SetMaterialUniforms(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), 16.0f);
		m_Shader->SetLightUniforms(lightSource.GetPosition(), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
		m_Shader->SetViewPos(camera.GetPosition());
		m_Shader->SetMVP(model, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());

		//Render lightning source cube
		lightSource.Draw(renderer, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
		//Render terrain
		m_Texture->Bind();
		m_VertexBuffer->Bind();
		m_VAO->AddBuffer(*m_VertexBuffer, layout);
		renderer.DrawWithTexture(*m_VAO, *m_IndexBuffer, *m_Shader);

		if (erosionDraw) {
			model = glm::translate(model, glm::vec3(1.2f, 0.0f, 0.0f));
			erosionBuffer->Bind();
			m_Shader->SetMVP(model, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
			
			m_VAO->AddBuffer(*erosionBuffer, layout);
			renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
			PrintTrack(model);
			erosionBuffer->Unbind();
		}

		if (testSymmetrical)
			DrawAdjacent(renderer, model);
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
		//Generating "symmetrical" noise and checkbox for miroring it on the four sides
		ImGui::Checkbox("Symmetrical", &noise.getConfigRef().symmetrical);
		if (noise.getConfigRef().symmetrical) {
			ImGui::Checkbox("Test Symmetrical", &testSymmetrical);
		}

		if (ImGui::Button("Erosion")) {
			erosionWindow = true;
		}

		//FPS
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();

		if (erosionWindow) {
			ErosionWindowRender();
		}
	}

	void TestNoiseMesh::ErosionWindowRender() {
		ImVec2 minSize = ImVec2(300, 300);
		ImVec2 maxSize = ImVec2(800, 800);

		ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
		ImGui::Begin("Erosion Settings");
		
		ImGui::InputInt("Droplet count", &erosion.getDropletCountRef());
		ImGui::InputInt("Droplet lifetime", &erosion.getConfigRef().dropletLifetime);
		ImGui::InputFloat("Inertia", &erosion.getConfigRef().inertia);
		ImGui::InputFloat("Droplet init capacity", &erosion.getConfigRef().initialCapacity, 0.01f, 1.0f);
		ImGui::InputFloat("Droplet init velocity", &erosion.getConfigRef().initialVelocity, 0.0f, 1.0f);
		ImGui::InputFloat("Droplet init water", &erosion.getConfigRef().initialWater, 0.0f, 1.0f);
		ImGui::InputFloat("Erosion rate", &erosion.getConfigRef().erosionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Deposition rate", &erosion.getConfigRef().depositionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Evaporation rate", &erosion.getConfigRef().evaporationRate, 0.0f, 1.0f);
		ImGui::InputFloat("Gravity", &erosion.getConfigRef().gravity, 0.0f, 10.0f);
		ImGui::InputFloat("Min slope", &erosion.getConfigRef().minSlope, 0.0f, 1.0f);
		ImGui::InputInt("Erosion radius", &erosion.getConfigRef().erosionRadius);
		ImGui::InputFloat("Blur", &erosion.getConfigRef().blur, 0.0f, 1.0f);

		ImGui::Checkbox("Show traces of droplets", &trackDraw);

		if (ImGui::Button("Erode map")) {
			erosionPerform = true;
		}

		ImGui::End();
	}

	void TestNoiseMesh::CheckChange() {
		if (prevCheck.prevOpt != noise.getConfigRef().option ||
			prevCheck.prevCheckSum != noise.getConfigRef().getCheckSum() ||
			prevCheck.prevRidge != noise.getConfigRef().ridge ||
			prevCheck.prevIsland != noise.getConfigRef().island ||
			prevCheck.prevIslandType != noise.getConfigRef().islandType ||
			prevCheck.symmetrical != noise.getConfigRef().symmetrical ||
			prevCheck.seed != seed)
		{
			noise.setSeed(seed);
			utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, meshVertices, meshIndices, 8, true, false);
			
			//Function only changes [y] value of texture sampling
			//it doesnt generate new biome noise
			utilities::PaintBiome(meshVertices, noise, biomeNoise, stride, 6);

			m_VertexBuffer->UpdateData(meshVertices, (height * width) * stride * sizeof(float));

			UpdatePrevCheckers();
			//Despite if the track is drawn or not we deactivate it
			if(traceVertices)
			{
				delete[] traceVertices;
				traceVertices = nullptr;
			}
			trackDraw = false;
			erosionDraw = false;
		}
	}


	void TestNoiseMesh::DrawAdjacent(Renderer& renderer, glm::mat4& model) {
		//West
		m_Shader->SetModel(glm::translate(model, glm::vec3(-1.0f, 0.0f, 0.0f)));
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
		//East
		m_Shader->SetModel(glm::translate(model, glm::vec3(1.0f, 0.0f, 0.0f)));
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
		//North
		m_Shader->SetModel(glm::translate(model, glm::vec3(0.0f, 0.0f, 1.0f)));
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
		//South
		m_Shader->SetModel(glm::translate(model, glm::vec3(0.0f, 0.0f, -1.0f)));
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

	void TestNoiseMesh::PerformErosion() {
		if (erosionPerform) {
			if(trackDraw)
			{
				delete[] traceVertices;
				traceVertices = new float[(erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3];
				
				for (int i = 0; i < (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3; i++) {
					traceVertices[i] = 0.0f;
				}
			}

			if (!erosionVertices) {
				erosionVertices = new float[width * height * stride];
			}

			m_TrackBuffer = std::make_unique<VertexBuffer>(traceVertices, (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3 * sizeof(float));

			utilities::benchmark_void(utilities::PerformErosion, "PerformErosion", erosionVertices, meshIndices, trackDraw ? std::optional<float*>(traceVertices) : std::nullopt, stride, 1, noise.getMap(), erosion);
			if (meshColor == MONO)
				utilities::PaintGrey(erosionVertices, width, height, stride, 6);
			else if (meshColor == PSEUDO_BIOME) {
				utilities::PaintBiome(erosionVertices, noise, biomeNoise, stride, 6);
			}

			erosionBuffer->UpdateData(erosionVertices, (height * width) * stride * sizeof(float));
			erosionPerform = false;
			erosionDraw = true;
		}
	}

	void TestNoiseMesh::UpdatePrevCheckers() {
		prevCheck.prevCheckSum = noise.getConfigRef().getCheckSum();
		prevCheck.prevOpt = noise.getConfigRef().option;
		prevCheck.prevRidge = noise.getConfigRef().ridge;
		prevCheck.prevIsland = noise.getConfigRef().island;
		prevCheck.prevIslandType = noise.getConfigRef().islandType;
		prevCheck.symmetrical = noise.getConfigRef().symmetrical;
		prevCheck.seed = seed;
	}

	void TestNoiseMesh::PrintTrack(glm::mat4& model) {
		if (trackDraw && traceVertices) {
		    m_TrackVAO->Bind();
			m_TrackBuffer->UpdateData(traceVertices, (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3 * sizeof(float));
			m_TrackShader->SetMVP(model, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());

			m_TrackShader->Bind();
			m_TrackBuffer->Bind();

			GLCALL(glEnableVertexAttribArray(0));
			GLCALL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0));

			GLCALL(glPointSize(2.0f));
			GLCALL(glDrawArrays(GL_POINTS, 0, (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef()));
		}
	}
}