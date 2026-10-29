#include "NoiseBasedGenerationSys.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "imgui/imgui.h"

NoiseBasedGenerationSys::NoiseBasedGenerationSys() : noise(), erosion(1, 1), width(0), height(0)
{
}

NoiseBasedGenerationSys::~NoiseBasedGenerationSys()
{
}

bool NoiseBasedGenerationSys::Initialize(int _width, int _height, float _heightScale)
{
	this->width = _width;
	this->height = _height;
	renderSettings.heightScale = _heightScale;
	noise.Initialize(width, height);
	if (!view.Initialize(width, height)) {
		return false;
	}

	if(!GenerateNoise(0.0f, 0.0f)) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}

	std::cout << "[LOG] NoiseBasedGenerationSys initialized\n";
	return true;
}

bool NoiseBasedGenerationSys::Resize()
{
	if (height != noise.GetHeight() || width != noise.GetWidth()) {
		erosionDraw = false;
		noise.Resize(width, height);
		view.Resize(width, height);
		return  true;
	}
	return false;
}

bool NoiseBasedGenerationSys::GenerateNoise(float originx, float originy)
{
	Resize();
	auto start = std::chrono::steady_clock::now();
	if (!noise.GenerateFractalNoise(originx, originy)) {
		return false;
	}
	lastGenerationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

	view.SetLayer(TerrainView::Layer::TERRAIN, noise.GetField());
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
	view.SetLayer(TerrainView::Layer::OVERLAY, erosion.GetField());

	std::cout << "[LOG] Erosion simulated successfully" << std::endl;
	return true;
}

void NoiseBasedGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light)
{
	if (infiniteGeneration) {
		if(oldCamPos.x != camera.GetPosition().x || oldCamPos.z != camera.GetPosition().z) {
			GenerateNoise(camera.GetPosition().x, camera.GetPosition().z);
			oldCamPos = camera.GetPosition();
		}
		camera.CameraAnchor(true);
	}
	view.Draw(renderer, camera, light, renderSettings);

	if (erosionDraw) {
		//Eroded map next to the base one along X, separated by a 5% gap
		float shift = view.GetWorldWidth() * 1.05f;
		view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::OVERLAY, glm::vec3(shift, 0.0f, 0.0f));
	}
}

void NoiseBasedGenerationSys::ImGuiRightPanel()
{
	if(utilities::MapSizeImGui(width, height)) {
		GenerateNoise(0.0f, 0.0f);
	}
	
	bool regen = utilities::NoiseImGui(noise.GetConfigRef());
	ImGui::Checkbox("Instant Update", &instantUpdate);
	if (!instantUpdate) {
		if (ImGui::Button("Generate new noise")) {
			GenerateNoise(0.0f, 0.0f);
			erosionDraw = false;
			erosion.ChangeMap();
		}
	}
	else if(regen){
		GenerateNoise(0.0f, 0.0f);
		erosionDraw = false;
		erosion.ChangeMap();
	}
	ErosionImGui();
	utilities::SavingImGui();
}

void NoiseBasedGenerationSys::ImGuiLeftPanel()
{
	TerrainRenderSettings& s = renderSettings;
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, infiniteGeneration)) {
		if(s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::GREYSCALE;
		}
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
	ImGui::Text("Noise generation: %.1f ms (%dx%d)", lastGenerationMs, noise.GetWidth(), noise.GetHeight());
	if (noise.GetHeight() * noise.GetWidth() > 500 * 500) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		ImGui::TextWrapped(
			"Warning!\n"
			"It is highly recommended to set a smaller map size while adjusting settings,"
			"due to high complexity of the algorithm for large maps!"
		);
		ImGui::PopStyleColor();
	}
}

