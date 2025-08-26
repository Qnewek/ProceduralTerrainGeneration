#include "TerrainGenerationSys.h"

#include <iostream>

#include "utilities.h"
#include "imgui/imgui.h"

TerrainGenerationSys::TerrainGenerationSys() : terrainVertices(nullptr), terrainIndices(nullptr), noiseVertices(nullptr),
heightScale(1.0f), modelScale(1.0f), stride(9), width(0), height(0), terrainGen() {

}
TerrainGenerationSys::~TerrainGenerationSys() {
	if(terrainVertices) {
		delete[] terrainVertices;
		terrainVertices = nullptr;
	}
	if(terrainIndices) {
		delete[] terrainIndices;
		terrainIndices = nullptr;
	}
}

bool TerrainGenerationSys::Initialize(unsigned int _height, unsigned int _width, float _heightScale) {
	mainVAO = std::make_unique<VertexArray>();
	mainShader = std::make_unique<Shader>("res/shaders/Terrain_vertex.shader", "res/shaders/Terrain_fragment.shader");

	layout.Push<float>(3);
	layout.Push<float>(3);
	layout.Push<float>(3);

	height = _height;
	width = _width;
	heightScale = _heightScale;

	std::vector<std::vector<double>> splines = { {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 40.0 ,64.0, 66.0, 68.0, 70.0},	//Continentalness {X,Y}
	            {-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 5.0, 10.0, 20.0, 30.0, 80.0, 100.0, 170.0},	//Mountainousness {X,Y}
	            {-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} };
	
	std::vector<biome::Biome>biomes = {
	    biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, 1.f),
		biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, 2, 1.f),
		biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, 7, 1.f),
		biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, 8, 1.f),
		biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, 0, 1.f),
	    biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, 5, 1.f)
	};
	
	std::vector<std::vector<RangedLevel>> ranges = {
	    {{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
	    {{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
	    {{-1.0f, -0.7f, 0},{-0.7f, -0.2f, 1},{ -0.2f, 0.03f, 2},{0.03f, 0.3f, 3},{0.3f, 1.1f, 4}},
	    {{-1.0f, -0.78f, 0},{-0.78f, -0.37f, 1},{-0.37f, -0.2f, 2},{-0.2f, 0.05f, 3},{0.05f, 0.45f, 4},{0.45f, 0.55f, 5},{0.55f, 1.1f, 6}}
	};
	
	terrainGen.SetSplines(splines);
	terrainGen.SetBiomes(biomes);
	terrainGen.SetRanges(ranges);
	GenerateTerrain();

	return true;

}
bool TerrainGenerationSys::GenerateTerrain() {
	if(terrainGen.GetHeight() != height || terrainGen.GetWidth() != width) {
		if(terrainVertices) {
			delete[] terrainVertices;
		}
		if(terrainIndices) {
			delete[] terrainIndices;
		}
		if(noiseVertices) {
			delete[] noiseVertices;
		}
		if (!terrainGen.SetSize(width, height)) {
			std::cout << "[ERROR] TerrainGen size couldnt be set\n";
			return false;
		}
		terrainVertices = new float[width * height * stride];
		noiseVertices = new float[width * height * stride];
		terrainIndices = new unsigned int[(height - 1) * width * 2]; // indices for strips probably will be changed

		utilities::MeshIndicesStrips(terrainIndices, width, height);
		mainIndexBuffer = std::make_unique<IndexBuffer>(terrainIndices, (height - 1) * width * 2);;
	}

	terrainGen.GenerateTerrain();

	utilities::ParseNoiseIntoVertices(terrainVertices, terrainGen.GetHeightMap(), width, height, heightScale, stride, 0);
	utilities::CalculateHeightMapNormals(terrainVertices, stride, 3, width, height);
	utilities::PaintVerticesByHeight(terrainVertices, width, height, heightScale, stride, utilities::heightMapMode::TOPOGRAPHICAL, 1, 6);

	mainVertexBuffer = std::make_unique<VertexBuffer>(terrainVertices, width * height * stride * sizeof(float));
	mainVAO->AddBuffer(*mainVertexBuffer, layout);
	return true;
}

void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(modelScale, modelScale, modelScale));
	mainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
	mainShader->SetLightUniforms(light.GetPosition(), light.GetAmbient(), light.GetDiffuse(), light.GetSpecular());
	mainShader->SetViewPos(camera.GetPosition());
	mainShader->SetUniform1f("step", 10.0f);
	mainShader->SetUniform1f("bandWidth", 0.2f);
	mainShader->SetMVP(model, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());

	renderer.DrawTriangleStrips(*mainVAO, *mainIndexBuffer, *mainShader, height- 1, width * 2);
}
void TerrainGenerationSys::ImGuiDraw() {
	if (ImGui::CollapsingHeader("Size settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Resize map");
		ImGui::InputInt("Height", &height);
		ImGui::InputInt("Width", &width);
		if (ImGui::Button("Resize")) {
			GenerateTerrain();
		}
	}
	if (ImGui::CollapsingHeader("Display settings:", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Model scale:");
		ImGui::SliderFloat("Model scale", &modelScale, 0.1f, 5.0f);

		if (ImGui::Checkbox("WireFrame", &wireFrame)) {
			if (wireFrame) {
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
				glDisable(GL_CULL_FACE);
			}
			else {
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
				glEnable(GL_CULL_FACE);
			}
		}

		if (ImGui::Button("Save file")) {
			std::cout << "[LOG] Well it will work one day i promise!" << std::endl;
		}
	}
	if(ImGui::CollapsingHeader("Terrain settings")) {
		ImGui::Text("Edit component noise:");
		bool regenerate = false, changeTerrain = false;
		static int worldParamOption = 0;
		const char* options[] = { "None", "Continentalness", "Mountainousness", "PV", "Humidity", "Temperature" };
		if (ImGui::BeginCombo("Noise: ", options[worldParamOption]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(options); n++)
			{
				bool is_selected = (worldParamOption == n);
				if (ImGui::Selectable(options[n], is_selected)) {
					worldParamOption = n;
					regenerate = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if(worldParamOption != 0) {
			WorldParameter param = WorldParameter::CONTINENTALNESS;
			switch (worldParamOption) {
			case 1:
				param = WorldParameter::CONTINENTALNESS;
				break;
			case 2:
				param = WorldParameter::MOUNTAINOUSNESS;
				break;
			case 3:
				param = WorldParameter::PV;
				break;
			case 4:
				param = WorldParameter::HUMIDITY;
				break;
			case 5:
				param = WorldParameter::TEMPERATURE;
				break;
			default:
				break;
			}
			if (utilities::NoiseImGui(terrainGen.GetSelectedNoiseConfig(param)) || regenerate) {
				terrainGen.GetSelectedNoise(param).GenerateFractalNoise();
				utilities::MapToVertices(terrainGen.GetSelectedNoise(param).GetMap(), noiseVertices, terrainIndices, height, width, stride, heightScale, utilities::heightMapMode::TOPOGRAPHICAL, true, false, true);
				noiseVertexBuffer = std::make_unique<VertexBuffer>(noiseVertices, width * height * stride * sizeof(float));
				mainVAO->AddBuffer(*noiseVertexBuffer, layout);
				changeTerrain = true;
			}
		}
		else {
			if (changeTerrain) {
				GenerateTerrain();
			}
			mainVAO->AddBuffer(*mainVertexBuffer, layout);
		}
	}

}
void TerrainGenerationSys::ImGuiOutput() {

}