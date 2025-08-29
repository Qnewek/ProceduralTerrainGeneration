#include "TerrainGenerationSys.h"

#include <iostream>

#include "imgui/imgui.h"

TerrainGenerationSys::TerrainGenerationSys() : terrainVertices(nullptr), terrainIndices(nullptr), noiseVertices(nullptr),
heightScale(1.0f), modelScale(1.0f), stride(9), width(0), height(0), terrainGen(), biomeGen() {

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
	
	terrainGen.Initialize(width, height);
	biomeGen.Initialize(width, height);
	GenerateTerrain();

	std::cout << "[LOG] TerrainGenerationSys initialized\n";
	return true;

}
bool TerrainGenerationSys::GenerateTerrain() {
	if(!terrainVertices || terrainGen.GetHeight() != height || terrainGen.GetWidth() != width) {
		if(terrainVertices) {
			delete[] terrainVertices;
		}
		if(terrainIndices) {
			delete[] terrainIndices;
		}
		if(noiseVertices) {
			delete[] noiseVertices;
		}
		if (!terrainGen.Resize(width, height)) {
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
	utilities::PaintVerticesByHeight(terrainVertices, width, height, heightScale, stride, displayMode, 1, 6);

	mainVertexBuffer = std::make_unique<VertexBuffer>(terrainVertices, width * height * stride * sizeof(float));
	mainVAO->AddBuffer(*mainVertexBuffer, layout);
	return true;
}
bool TerrainGenerationSys::GenerateBiomes()
{
	if(!biomeGen.Biomify(terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::CONTINENTALNESS), 
		terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS), 
		terrainGen.GetSelectedNoise(TerrainGenerator::WorldGenParameter::WEIRDNESS))) {
		std::cout << "[ERROR] Biomes couldnt be generated\n";
	}
	return true;
}
void TerrainGenerationSys::UpdateVertex()
{
	mainVertexBuffer->UpdateData(terrainVertices, (height * width) * stride * sizeof(float));
	mainVAO->AddBuffer(*mainVertexBuffer, layout);
}
void TerrainGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(modelScale, modelScale, modelScale));
	light.SetLightUniforms(*mainShader);
	camera.SetUniforms(*mainShader);
	mainShader->SetModel(model);
	mainShader->SetUniform1i("flatten", map2d);
	mainShader->SetUniform1i("size", height/2);

	renderer.DrawTriangleStrips(*mainVAO, *mainIndexBuffer, *mainShader, height- 1, width * 2);
}
void TerrainGenerationSys::ImGuiDraw() {
	//Trash variables for function call
	float topoStep = 0.1f, topoBandWidth = 0.05f;
	//
	if(utilities::MapSizeImGui(height, width)) {
		GenerateTerrain();
	}
	if (utilities::DisplayModeImGui(modelScale, topoStep, topoBandWidth, heightScale, displayMode, wireFrame, map2d)) {
		utilities::PaintVerticesByHeight(terrainVertices, width, height, heightScale, stride, displayMode, 1, 6);
		UpdateVertex();
		biomesGeneration = false;
	}
	utilities::SavingImGui();
	if (ImGui::CollapsingHeader("Terrain settings")) {
		bool regenerate = false;
		ImGui::Text("Evaluating method");
		static int evaluatingOption = 0;
		const char* methodOptions[] = { "Linear", "Spline" };
		if (ImGui::BeginCombo("Method: ", methodOptions[evaluatingOption]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(methodOptions); n++)
			{
				bool is_selected = (evaluatingOption == n);
				if (ImGui::Selectable(methodOptions[n], is_selected)) {
					evaluatingOption = n;
					terrainGen.GetEvaluationMethod() = static_cast<TerrainGenerator::EvaluationMethod>(evaluatingOption);
					changeTerrain = true;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::Text("Edit component noise:");
		static int editedNoise = 0;
		const char* noiseOptions[] = { "None", "Continentalness", "Mountainousness", "WEIRDNESS" };
		if (ImGui::BeginCombo("Noise: ", noiseOptions[editedNoise]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(noiseOptions); n++)
			{
				bool is_selected = (editedNoise == n);
				if (ImGui::Selectable(noiseOptions[n], is_selected)) {
					editedNoise = n;
					regenerate = true;
					biomesGeneration = false;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (editedNoise != 0) {
			TerrainGenerator::WorldGenParameter param = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
			switch (editedNoise) {
			case 1:
				param = TerrainGenerator::WorldGenParameter::CONTINENTALNESS;
				break;
			case 2:
				param = TerrainGenerator::WorldGenParameter::MOUNTAINOUSNESS;
				break;
			case 3:
				param = TerrainGenerator::WorldGenParameter::WEIRDNESS;
				break;
			default:
				break;
			}
			if (utilities::NoiseImGui(terrainGen.GetSelectedNoiseConfig(param)) || regenerate) {
				terrainGen.GetSelectedNoise(param).GenerateFractalNoise();
				utilities::MapToVertices(terrainGen.GetSelectedNoise(param).GetMap(), noiseVertices, terrainIndices, height, width, stride, heightScale, displayMode, true, false, true);
				noiseVertexBuffer = std::make_unique<VertexBuffer>(noiseVertices, width * height * stride * sizeof(float));
				mainVAO->AddBuffer(*noiseVertexBuffer, layout);
				changeTerrain = true;
				biomeGen.Regenerate();
			}
		}
		else {
			if (changeTerrain) {
				GenerateTerrain();
				biomeGen.Regenerate();
				changeTerrain = false;
			}
			else {
				mainVAO->AddBuffer(*mainVertexBuffer, layout);
			}

			if (ImGui::Checkbox("Biomes generation", &biomesGeneration)) {
				if (biomesGeneration) {
					if (!biomeGen.IsGenerated()) {
						GenerateBiomes();
					}
					utilities::PaintVerticesByBiome(terrainVertices, biomeGen, width, height, stride, 6);
				}
				else {
					utilities::PaintVerticesByHeight(terrainVertices, width, height, heightScale, stride, displayMode, 1, 6);	
				}
				UpdateVertex();
			}
			if (biomesGeneration) {
				if (ImGui::CollapsingHeader("Biome generation settings", ImGuiTreeNodeFlags_DefaultOpen)) {

				}
			}
		}
		ImGui::SliderInt("Sampling resolution", &terrainGen.GetResolitionRef(), 100, 1000);
		if (ImGui::Button("Change resolution")) {
			terrainGen.SetResolution();
			GenerateTerrain();
		}
	}
}
void TerrainGenerationSys::ImGuiOutput() {

}