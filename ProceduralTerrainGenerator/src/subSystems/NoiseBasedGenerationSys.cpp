#include "NoiseBasedGenerationSys.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "imgui.h"

#include "Preset.h"

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
	if (!view.Initialize(config.width, config.height) || !chunks.Initialize() || !gpu.Initialize()) {
		return false;
	}
	eroded = std::make_unique<erosion::Erosion>(1, 1);

	//The first frame needs a map
	RequestNoise();
	GenerateFinite();

	std::cout << "[LOG] NoiseBasedGenerationSys initialized\n";
	return true;
}

NoiseBasedGenerationSys::ErosionResult NoiseBasedGenerationSys::RunErosion(std::unique_ptr<erosion::Erosion> maps, HeightField base, const erosion::ErosionConfig& erosionConfig, NoiseRequest request)
{
	auto start = std::chrono::steady_clock::now();
	if (request.resetErosion) {
		maps->ChangeMap();
	}
	if (request.erode) {
		if (base.GetHeight() != maps->GetHeight() || base.GetWidth() != maps->GetWidth()) {
			maps->Resize(base.GetWidth(), base.GetHeight());
		}
		//Copies the noise map only after a reset; otherwise erosion keeps accumulating on its own map
		maps->SetMap(base.Data());
		maps->Erode(erosionConfig, std::nullopt);
		maps->DontChangeMap();
		std::cout << "[LOG] Erosion simulated successfully" << std::endl;
	}

	ErosionResult result;
	result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	result.erosion = std::move(maps);
	result.done = request;
	return result;
}

void NoiseBasedGenerationSys::Update(Camera& camera)
{
	gpu.PollMeasure(lastGenerationMs);

	if (std::optional<ErosionResult> result = job.TryTake()) {
		InstallResult(*result);
	}

	const bool leftInfinite = chunksShown && !infiniteGeneration;
	chunksShown = infiniteGeneration;
	//The noise changed while only the chunks were shown
	if (leftInfinite && finiteOutdated) {
		RequestNoise();
		finiteOutdated = false;
	}
	if (infiniteGeneration) {
		UpdateChunks(camera);
	}
	else {
		GenerateFinite();
	}
	//After generating: the map size may have changed meanwhile
	if (leftInfinite) {
		view.BringCameraToMap(camera, renderSettings);
	}

	if (!job.IsRunning() && pending.Any()) {
		StartJob();
	}
}

namespace {
	bool ValidMapSize(int width, int height)
	{
		return width >= 2 && height >= 2 && width <= 8192 && height <= 8192;
	}
}

void NoiseBasedGenerationSys::GenerateFinite()
{
	if (!pending.noise) {
		return;
	}
	if (ValidMapSize(config.width, config.height)) {
		view.Resize(config.width, config.height);
		gpu.BeginMeasure();
		gpu.GenerateNoise(config.noise, MapArea{}, view.LayerTexture(TerrainView::Layer::TERRAIN, config.width, config.height));
		gpu.EndMeasure();
	}
	else {
		std::cout << "[ERROR] Map size must be between 2 and 8192, keeping the previous map" << std::endl;
	}
	pending.noise = false;
}

void NoiseBasedGenerationSys::UpdateChunks(const Camera& camera)
{
	if (pending.noise) {
		chunks.Invalidate();
		pending.noise = false;
		finiteOutdated = true;
	}
	//An invalid size waiting in the UI keeps the previous one
	if (ValidMapSize(config.width, config.height)) {
		chunks.AlignWithMap(config.width, config.height);
	}

	const glm::vec3 position = camera.GetPosition() / renderSettings.modelScale;
	if (chunks.Plan(glm::vec2(position.x, position.z), camera.GetRenderDistance() / renderSettings.modelScale) == 0) {
		return;
	}
	gpu.BeginMeasure();
	chunks.GenerateNext([&](const MapArea& area, TextureClass& height, TextureClass*, TextureClass*) {
		gpu.GenerateNoise(config.noise, area, height);
	});
	gpu.EndMeasure();
}

void NoiseBasedGenerationSys::StartJob()
{
	//The job works on a copy, so the map shown and the config stay free for the UI
	auto back = std::make_unique<erosion::Erosion>(*eroded);
	HeightField base;
	if (pending.erode) {
		base = view.ReadLayer(TerrainView::Layer::TERRAIN);
	}
	job.Start([back = std::move(back), base = std::move(base), erosionConfig = config.erosion, request = pending]() mutable {
		return RunErosion(std::move(back), std::move(base), erosionConfig, request);
	});
	pending = NoiseRequest();
}

void NoiseBasedGenerationSys::InstallResult(ErosionResult& result)
{
	eroded = std::move(result.erosion);

	const NoiseRequest& done = result.done;
	if (done.resetErosion) {
		erosionDraw = false;
	}
	//Not shown if a reset is already waiting - the eroded map is about to be dropped
	if (done.erode && !pending.resetErosion) {
		lastErosionMs = result.ms;
		erosionDraw = true;
		view.SetLayer(TerrainView::Layer::OVERLAY, eroded->GetField());
	}
}

void NoiseBasedGenerationSys::RequestNoise()
{
	pending.noise = true;
	pending.resetErosion = true;
	erosionDraw = false;
}

void NoiseBasedGenerationSys::RequestErosion()
{
	pending.erode = true;
}

void NoiseBasedGenerationSys::PresetsImGui()
{
	std::string error;
	switch (presets.ImGuiDraw()) {
	case PresetPanel::Action::SAVE:
		if (preset::Save(presets.SavePath(), config, error)) {
			presets.ReportSaved();
		}
		else {
			presets.ReportError(error);
		}
		break;
	case PresetPanel::Action::LOAD: {
		NoiseMapConfig loaded;
		if (preset::Load(presets.LoadPath(), loaded, error)) {
			ApplyConfig(loaded);
			presets.ReportLoaded();
		}
		else {
			presets.ReportError(error);
		}
		break;
	}
	case PresetPanel::Action::NONE:
		break;
	}
}

void NoiseBasedGenerationSys::ApplyConfig(const NoiseMapConfig& loaded)
{
	config = loaded;
	pending.erode = false;
	RequestNoise();
}

void NoiseBasedGenerationSys::Draw(Renderer& renderer, Camera& camera, LightSource& light)
{
	if (chunksShown) {
		chunks.Draw(renderer, camera, light, renderSettings);
		return;
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
		RequestNoise();
	}

	bool regen = utilities::NoiseImGui(config.noise);
	ImGui::Checkbox("Instant Update", &instantUpdate);
	if (!instantUpdate) {
		if (ImGui::Button("Generate new noise")) {
			RequestNoise();
		}
	}
	else if(regen){
		RequestNoise();
	}
	if (infiniteGeneration) {
		ImGui::TextDisabled("Erosion and saving work on the finite map (Partial generation)");
		PresetsImGui();
		return;
	}
	ErosionImGui();
	PresetsImGui();
	//The base map, plus the eroded one in its own file when it is shown
	std::vector<ExportPanel::Target> exportTargets = { { view.GetMapWidth(), view.GetMapHeight(), "", [this] { return view.ReadLayer(TerrainView::Layer::TERRAIN); } } };
	if (erosionDraw) {
		const HeightField& field = eroded->GetField();
		exportTargets.push_back({ field.GetWidth(), field.GetHeight(), "_eroded", [&field] { return field; } });
	}
	exporter.ImGuiDraw(exportTargets, heightmap::noiseRange);
}

void NoiseBasedGenerationSys::ImGuiLeftPanel()
{
	TerrainRenderSettings& s = renderSettings;
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, &infiniteGeneration)) {
		if(s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::GREYSCALE;
		}
	}
	if (infiniteGeneration) {
		chunks.ImGuiDraw();
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
	if (chunksShown) {
		ImGui::Text("Last generation (GPU): %.2f ms (up to %d chunks of %dx%d)", lastGenerationMs, ChunkWorld::maxChunksPerFrame, ChunkWorld::chunkSize, ChunkWorld::chunkSize);
		ImGui::Text("Chunks: %d in view, %d cached, %d waiting", chunks.GetVisibleCount(), chunks.GetCachedCount(), chunks.GetPendingCount());
		ImGui::Text("Coarsest level in view: %d (1 sample per %d texels)", chunks.GetCoarsestLevel(), 1 << chunks.GetCoarsestLevel());
		return;
	}
	ImGui::Text("Last generation (GPU): %.2f ms (%dx%d)", lastGenerationMs, view.GetMapWidth(), view.GetMapHeight());
	ImGui::Text("Last erosion (CPU): %.1f ms%s", lastErosionMs, job.IsRunning() ? "  [eroding...]" : "");
}

