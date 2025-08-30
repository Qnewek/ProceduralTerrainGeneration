#include "NoiseBasedGenerationSys.h"

#include <iostream>

#include "imgui/imgui.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

NoiseBasedGenerationSys::NoiseBasedGenerationSys() : noise(), erosion(1, 1), vertices(nullptr), erosionVertices(nullptr), meshIndices(nullptr), 
width(0), height(0), heightScale(1.0f), modelScale(1.0f), topoBandWidth(0.2f), topoStep(10.0f), stride(9)
{
}

NoiseBasedGenerationSys::~NoiseBasedGenerationSys()
{
	if (vertices) {
		delete[] vertices;
		vertices = nullptr;
	}
	if (meshIndices) {
		delete[] meshIndices;
		meshIndices = nullptr;
	}
	if(erosionVertices) {
		delete[] erosionVertices;
		erosionVertices = nullptr;
	}
}

bool NoiseBasedGenerationSys::Initialize(int _height, int _width, float _heightScale)
{
	erosionVAO = std::make_unique<VertexArray>();
	mainVAO = std::make_unique<VertexArray>();
	layout.Push<float>(3);
	layout.Push<float>(3);
	layout.Push<float>(3);

	this->height = _height;
	this->width = _width;
	this->heightScale = _heightScale;
	noise.Initialize(height, width);

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
		if (meshIndices) {
			delete[] meshIndices;
		}

		vertices = new float[width * height * stride];
		meshIndices = new unsigned int[(height - 1) * width * 2]; // indices for strips
		return  true;
	}
	return false;
}

bool NoiseBasedGenerationSys::GenerateNoise()
{
	bool resized = Resize();
	if (!noise.GenerateFractalNoise()) {
		return false;
	}
	utilities::MapToVertices(noise.GetMap(), vertices, meshIndices, height, width, stride, heightScale, displayMode, true, resized, true);

	mainVertexBuffer = std::make_unique<VertexBuffer>(vertices, (height * width) * stride * sizeof(float));
	mainIndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (height - 1) * width * 2);
	mainShader = std::make_unique<Shader>("res/shaders/HeightMap_vertex.shader", "res/shaders/HeightMap_fragment.shader");
	mainVAO->AddBuffer(*mainVertexBuffer, layout);

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
	if(height != erosion.GetHeight() || width != erosion.GetWidth() || !erosionVertices) {
		if(erosionVertices) {
			delete[] erosionVertices;
		}
		erosionVertices = new float[height * width * stride];
		erosion.Resize(width, height);
		erosion.SetMap(noise.GetMap());
	}

	erosion.SetMap(noise.GetMap());
	erosion.DontChangeMap();

	utilities::benchmarkVoid(utilities::PerformErosion, "PerformErosion", erosion, erosionVertices, heightScale, std::nullopt, stride, displayMode);
	erosionVertexBuffer = std::make_unique<VertexBuffer>(erosionVertices, (height * width) * stride * sizeof(float));
	erosionVAO->AddBuffer(*erosionVertexBuffer, layout);
	erosionDraw = true;

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
	mainShader->SetUniform1i("flatten", map2d);
	mainShader->SetUniform1i("size", height / 2);
	mainShader->SetUniform1f("bandWidth", topoBandWidth);
	mainShader->SetUniform1i("flatten", map2d);

	renderer.DrawTriangleStrips(*mainVAO, *mainIndexBuffer, *mainShader, noise.GetHeight() - 1, noise.GetWidth() * 2);

	if (erosionDraw) {
		model = glm::translate(model, glm::vec3(width + 1.0f, 0.0f, 0.0f));
		mainShader->SetModel(model);
		renderer.DrawTriangleStrips(*erosionVAO, *mainIndexBuffer, *mainShader, erosion.GetHeight() - 1, erosion.GetWidth() * 2);
	}
}

void NoiseBasedGenerationSys::ImGuiDraw()
{
	if(utilities::MapSizeImGui(width, height)) {
		GenerateNoise();
	}
	if (utilities::DisplayModeImGui(modelScale, topoStep, topoBandWidth, heightScale, displayMode, wireFrame, map2d)) {
		utilities::PaintVerticesByHeight(vertices, width, height, heightScale, stride, displayMode, 1, 6);
		mainVertexBuffer->UpdateData(vertices, (height * width) * stride * sizeof(float));
		mainVAO->AddBuffer(*mainVertexBuffer, layout);
		if (erosionDraw) {
			utilities::PaintVerticesByHeight(erosionVertices, width, height, heightScale, stride, displayMode, 1, 6);
			erosionVertexBuffer->UpdateData(erosionVertices, (height * width) * stride * sizeof(float));
			erosionVAO->AddBuffer(*erosionVertexBuffer, layout);
		}
	}
	utilities::SavingImGui();
	
	ImGui::Checkbox("Instant Update", &instantUpdate);
	if (!instantUpdate) {
		if (ImGui::Button("Generate new noise")) {
			GenerateNoise();
			erosionDraw = false;
			erosion.ChangeMap();
		}
	}
	else if(utilities::NoiseImGui(noise.GetConfigRef())){
		GenerateNoise();
		erosionDraw = false;
		erosion.ChangeMap();
	}
	ErosionImGui();
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

