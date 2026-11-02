#include "NoiseBasedGenerationSys.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "imgui.h"

NoiseBasedGenerationSys::NoiseBasedGenerationSys()
{
}

NoiseBasedGenerationSys::~NoiseBasedGenerationSys()
{
}

bool NoiseBasedGenerationSys::Initialize(int _width, int _height, float _heightScale)
{
	config.width = _width;
	config.height = _height;
	renderSettings.heightScale = _heightScale;
	if (!view.Initialize(config.width, config.height)) {
		return false;
	}
	auto initial = std::make_unique<NoiseMaps>();
	if (!initial->noise.Initialize(config.width, config.height)) {
		std::cout << "[ERROR] Invalid height or width value" << std::endl;
		return false;
	}

	//The first frame needs a map, so the initial generation runs synchronously
	NoiseRequest request;
	request.noise = true;
	GenerationResult result = Generate(std::move(initial), config, request);
	if (!result.ok) {
		std::cout << "[ERROR] NoiseBasedGenerationSys: initial generation failed" << std::endl;
		return false;
	}
	InstallResult(result);

	std::cout << "[LOG] NoiseBasedGenerationSys initialized\n";
	return true;
}

bool NoiseBasedGenerationSys::RunGeneration(NoiseMaps& out, const NoiseMapConfig& _config, NoiseRequest& request)
{
	//A new size invalidates the noise map and the accumulated erosion
	if (out.noise.GetWidth() != _config.width || out.noise.GetHeight() != _config.height) {
		if (!out.noise.Resize(_config.width, _config.height)) {
			return false;
		}
		if (!request.noise) {
			request.noise = true;
			request.originx = request.originy = 0.0f;
		}
		request.resetErosion = true;
	}

	if (request.noise && !out.noise.GenerateFractalNoise(_config.noise, request.originx, request.originy)) {
		return false;
	}
	if (request.resetErosion) {
		out.erosion.ChangeMap();
	}
	if (request.erode) {
		if (_config.width <= 1 || _config.height <= 1) {
			std::cout << "[ERROR] Invalid height or width value" << std::endl;
			return false;
		}
		if (_config.height != out.erosion.GetHeight() || _config.width != out.erosion.GetWidth()) {
			out.erosion.Resize(_config.width, _config.height);
		}
		//Copies the noise map only after a reset; otherwise erosion keeps accumulating on its own map
		out.erosion.SetMap(out.noise.GetMap());
		out.erosion.Erode(_config.erosion, std::nullopt);
		out.erosion.DontChangeMap();
		std::cout << "[LOG] Erosion simulated successfully" << std::endl;
	}
	return true;
}

NoiseBasedGenerationSys::GenerationResult NoiseBasedGenerationSys::Generate(std::unique_ptr<NoiseMaps> maps, const NoiseMapConfig& _config, NoiseRequest request)
{
	GenerationResult result;
	auto start = std::chrono::steady_clock::now();
	result.ok = RunGeneration(*maps, _config, request);
	result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	result.maps = std::move(maps);
	result.done = request;
	return result;
}

void NoiseBasedGenerationSys::Update()
{
	if (std::optional<GenerationResult> result = job.TryTake()) {
		InstallResult(*result);
	}
	if (!job.IsRunning() && pending.Any()) {
		StartJob();
	}
}

void NoiseBasedGenerationSys::StartJob()
{
	//The job works on copies of the maps and the config, so the front buffer and config stay free for the UI
	auto back = std::make_unique<NoiseMaps>(*generated);
	job.Start([back = std::move(back), snapshot = config, request = pending]() mutable {
		return Generate(std::move(back), snapshot, request);
	});
	pending = NoiseRequest();
}

void NoiseBasedGenerationSys::InstallResult(GenerationResult& result)
{
	if (!result.ok) {
		std::cout << "[ERROR] Noise generation failed, keeping the previous maps" << std::endl;
		return;
	}
	generated = std::move(result.maps);
	lastGenerationMs = result.ms;

	const NoiseRequest& done = result.done;
	//Size of the installed maps, not config.width/height - those may already hold the next resize
	view.Resize(generated->noise.GetWidth(), generated->noise.GetHeight());
	if (done.noise) {
		view.SetLayer(TerrainView::Layer::TERRAIN, generated->noise.GetField());
	}
	if (done.resetErosion) {
		erosionDraw = false;
	}
	//Not shown if a reset is already waiting - the eroded map is about to be dropped
	if (done.erode && !pending.resetErosion) {
		erosionDraw = true;
		view.SetLayer(TerrainView::Layer::OVERLAY, generated->erosion.GetField());
	}
}

void NoiseBasedGenerationSys::RequestNoise(float originx, float originy)
{
	pending.noise = true;
	pending.originx = originx;
	pending.originy = originy;
	pending.resetErosion = true;
	erosionDraw = false;
}

void NoiseBasedGenerationSys::RequestErosion()
{
	pending.erode = true;
}

void NoiseBasedGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light)
{
	if (infiniteGeneration) {
		if(oldCamPos.x != camera.GetPosition().x || oldCamPos.z != camera.GetPosition().z) {
			RequestNoise(camera.GetPosition().x, camera.GetPosition().z);
			oldCamPos = camera.GetPosition();
		}
		camera.CameraAnchor(true);
	}
	//Eroded map next to the base one along X, separated by a 5% gap; the 2D view frames both
	float shift = view.GetWorldWidth() * 1.05f;
	glm::vec2 half(view.GetWorldWidth() / 2.0f, view.GetWorldHeight() / 2.0f);
	if (erosionDraw) {
		view.SetFlatViewBounds(-half, glm::vec2(shift + half.x, half.y));
	}
	else {
		view.ResetFlatViewBounds();
	}

	view.Draw(renderer, camera, light, renderSettings);
	if (erosionDraw) {
		view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::OVERLAY, glm::vec3(shift, 0.0f, 0.0f));
	}
}

void NoiseBasedGenerationSys::ImGuiRightPanel()
{
	if(utilities::MapSizeImGui(config.width, config.height)) {
		RequestNoise(0.0f, 0.0f);
	}

	bool regen = utilities::NoiseImGui(config.noise);
	ImGui::Checkbox("Instant Update", &instantUpdate);
	if (!instantUpdate) {
		if (ImGui::Button("Generate new noise")) {
			RequestNoise(0.0f, 0.0f);
		}
	}
	else if(regen){
		RequestNoise(0.0f, 0.0f);
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
		erosion::ErosionConfig& e = config.erosion;
		ImGui::InputInt("Droplet count", &e.dropletCount);
		ImGui::InputInt("Droplet lifetime", &e.dropletLifetime);
		ImGui::InputFloat("Inertia", &e.inertia);
		ImGui::InputFloat("Droplet init capacity", &e.initialCapacity, 0.01f, 1.0f);
		ImGui::InputFloat("Droplet init velocity", &e.initialVelocity, 0.0f, 1.0f);
		ImGui::InputFloat("Droplet init water", &e.initialWater, 0.0f, 1.0f);
		ImGui::InputFloat("Erosion rate", &e.erosionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Deposition rate", &e.depositionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Evaporation rate", &e.evaporationRate, 0.0f, 1.0f);
		ImGui::InputFloat("Gravity", &e.gravity, 0.0f, 10.0f);
		ImGui::InputFloat("Min slope", &e.minSlope, 0.0f, 1.0f);
		ImGui::InputInt("Erosion radius", &e.erosionRadius);
		ImGui::InputFloat("Blur", &e.blur, 0.0f, 1.0f);

		if (ImGui::Button("Erode map")) {
			RequestErosion();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset")) {
			//Drops a not yet started erosion too, so the reset is not followed by an erosion of the old request
			pending.erode = false;
			pending.resetErosion = true;
			erosionDraw = false;
		}
	}
}

void NoiseBasedGenerationSys::ImGuiOutput()
{
	const noise::SimplexNoiseClass& noise = generated->noise;
	ImGui::Text("Last generation: %.1f ms (%dx%d)%s", lastGenerationMs, noise.GetWidth(), noise.GetHeight(),
		job.IsRunning() ? "  [generating...]" : "");
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

