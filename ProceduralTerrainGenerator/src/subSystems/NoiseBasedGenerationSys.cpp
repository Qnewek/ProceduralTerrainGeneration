#include "NoiseBasedGenerationSys.h"

#include <iostream>

#include "imgui/imgui.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

NoiseBasedGenerationSys::NoiseBasedGenerationSys() : noise(), erosion(1, 1), vertices(nullptr),
width(0), height(0), heightScale(1.0f), modelScale(1.0f), topoBandWidth(0.2f), topoStep(10.0f), stride(5), mapResolution(100)
{
}

NoiseBasedGenerationSys::~NoiseBasedGenerationSys()
{
	if (vertices) {
		delete[] vertices;
		vertices = nullptr;
	}
}

bool NoiseBasedGenerationSys::Initialize(int _height, int _width, float _heightScale)
{
	mainVAO = std::make_unique<VertexArray>();
	layout.Push<float>(3);
	layout.Push<float>(2);
	mainShader = std::make_unique<Shader>("res/shaders/HeightMapShaders/HeightMap_vertex.shader", "res/shaders/HeightMapShaders/HeightMap_fragment.shader", "res/shaders/HeightMapShaders/HeightMap_tesscontrol.shader", "res/shaders/HeightMapShaders/HeightMap_tesseval.shader");
	mainShader->Bind();
	mainShader->SetUniform1i("displayMode", static_cast<int>(displayMode));

	this->height = _height;
	this->width = _width;
	this->heightScale = _heightScale;
	noise.Initialize(height, width);
	vertices = new float[mapResolution * mapResolution * stride * 4];
	utilities::GenerateVerticesForResolution(vertices, height, width, mapResolution, stride, 0, 3);
	mainVertexBuffer = std::make_unique<VertexBuffer>(vertices, (mapResolution * mapResolution) * stride * 4 * sizeof(float));
	mainVAO->AddBuffer(*mainVertexBuffer, layout);

	if(!GenerateNoise()) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}

	std::cout << "[LOG] NoiseBasedGenerationSys initialized\n";
	return true;
}

bool NoiseBasedGenerationSys::Resize()
{
	if (!vertices || height != noise.GetHeight() || width != noise.GetWidth()) {
		erosionDraw = false;
		noise.Resize(width, height);

		if (vertices) {
			delete[] vertices;
		}

		vertices = new float[mapResolution * mapResolution * stride * 4];
		utilities::GenerateVerticesForResolution(vertices, height, width, mapResolution, stride, 0, 3);
		return  true;
	}
	return false;
}

bool NoiseBasedGenerationSys::GenerateNoise()
{
	Resize();
	if (!noise.GenerateFractalNoise()) {
		return false;
	}

	terrainTexture = std::make_unique<TextureClass>(noise.GetMap(), width, height);
	std::cout << "[LOG] Noise based terrain initialized" << std::endl;
	return true;
}

bool NoiseBasedGenerationSys::SimulateErosion()
{
	if(width <=1 || height <= 1) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}
	if (!noise.GetMap()) {
		std::cout << "[ERROR] Noise map not initialized" << std::endl;
		return false;
	}
	if(height != erosion.GetHeight() || width != erosion.GetWidth()) {
		erosion.Resize(width, height);
	}

	erosion.SetMap(noise.GetMap());
	erosion.Erode(std::nullopt);
	erosion.DontChangeMap();
	erosionDraw = true;
	erosionTexture = std::make_unique<TextureClass>(erosion.GetMap(), width, height);

	std::cout << "[LOG] Erosion simulated successfully" << std::endl;
	return true;
}

void NoiseBasedGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light)
{
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::scale(model, glm::vec3(modelScale, modelScale, modelScale));
	light.SetLightUniforms(*mainShader);
	camera.SetUniforms(*mainShader);
	mainShader->SetModel(model);
	mainShader->SetUniform1f("step", topoStep);
	mainShader->SetUniform1f("bandWidth", topoBandWidth);
	mainShader->SetUniform1i("size", height / 2);
	mainShader->SetUniform1i("flatten", map2d);
	mainShader->SetUniform1i("heightMap", 0);
	mainShader->SetUniform1f("heightScale", heightScale);

	terrainTexture->Bind(0);
	renderer.DrawPatches(*mainVAO, *mainShader, mapResolution * mapResolution, 4);

	if (erosionDraw) {
		model = glm::translate(model, glm::vec3(width + 1.0f, 0.0f, 0.0f));
		mainShader->SetModel(model);
		erosionTexture->Bind(1);
		mainShader->SetUniform1i("heightMap", 1);
		renderer.DrawPatches(*mainVAO, *mainShader, mapResolution * mapResolution, 4);
	}
}

void NoiseBasedGenerationSys::ImGuiRightPanel()
{
	if(utilities::MapSizeImGui(width, height)) {
		GenerateNoise();
	}
	
	bool regen = utilities::NoiseImGui(noise.GetConfigRef());
	ImGui::Checkbox("Instant Update", &instantUpdate);
	if (!instantUpdate) {
		if (ImGui::Button("Generate new noise")) {
			GenerateNoise();
			erosionDraw = false;
			erosion.ChangeMap();
		}
	}
	else if(regen){
		GenerateNoise();
		erosionDraw = false;
		erosion.ChangeMap();
	}
	ErosionImGui();
	utilities::SavingImGui();
}

void NoiseBasedGenerationSys::ImGuiLeftPanel()
{
	if (utilities::DisplayModeImGui(modelScale, topoStep, topoBandWidth, heightScale, displayMode, wireFrame, map2d)) {
		if(displayMode == utilities::heightMapMode::BIOMES) {
			displayMode = utilities::heightMapMode::GREYSCALE;
		}
		mainShader->Bind();
		mainShader->SetUniform1i("displayMode", static_cast<int>(displayMode));
	}
}

void NoiseBasedGenerationSys::ErosionImGui()
{
	//Erosion settings
	if (ImGui::CollapsingHeader("Erosion Settings")) {
		ImGui::InputInt("Droplet count", &erosion.GetDropletCountRef());
		ImGui::InputInt("Droplet lifetime", &erosion.GetConfigRef().dropletLifetime);
		ImGui::InputFloat("Inertia", &erosion.GetConfigRef().inertia);
		ImGui::InputFloat("Droplet init capacity", &erosion.GetConfigRef().initialCapacity, 0.01f, 1.0f);
		ImGui::InputFloat("Droplet init velocity", &erosion.GetConfigRef().initialVelocity, 0.0f, 1.0f);
		ImGui::InputFloat("Droplet init water", &erosion.GetConfigRef().initialWater, 0.0f, 1.0f);
		ImGui::InputFloat("Erosion rate", &erosion.GetConfigRef().erosionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Deposition rate", &erosion.GetConfigRef().depositionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Evaporation rate", &erosion.GetConfigRef().evaporationRate, 0.0f, 1.0f);
		ImGui::InputFloat("Gravity", &erosion.GetConfigRef().gravity, 0.0f, 10.0f);
		ImGui::InputFloat("Min slope", &erosion.GetConfigRef().minSlope, 0.0f, 1.0f);
		ImGui::InputInt("Erosion radius", &erosion.GetConfigRef().erosionRadius);
		ImGui::InputFloat("Blur", &erosion.GetConfigRef().blur, 0.0f, 1.0f);

		if (ImGui::Button("Erode map")) {
			SimulateErosion();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset")) {
			erosionDraw = false;
			erosion.ChangeMap();
		}
	}
}

void NoiseBasedGenerationSys::ImGuiOutput()
{
	if (noise.GetHeight() * noise.GetWidth() > 500 * 500) {
		ImGui::PushStyleColor(ImGuiCol_Text, (1.0f, 0.0f, 0.0f, 1.0f));
		ImGui::TextWrapped(
			"Warning!\n"
			"It is highly recommended to set a smaller map size while adjusting settings,"
			"due to high complexity of the algorithm for large maps!"
		);
		ImGui::PopStyleColor();
	}
}

