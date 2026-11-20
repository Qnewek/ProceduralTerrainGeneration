#include "NoiseBasedGenerationSys.h"

#include <algorithm>
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
	if (!view.Initialize(config.width, config.height) || !pipesView.Initialize(config.width, config.height) || !chunks.Initialize()
		|| !gpu.Initialize() || !erosion.Initialize()) {
		return false;
	}

	//The first frame needs a map
	RequestNoise();
	GenerateFinite();

	std::cout << "[LOG] NoiseBasedGenerationSys initialized\n";
	return true;
}

void NoiseBasedGenerationSys::Update(Camera& camera)
{
	gpu.PollMeasure(lastGenerationMs);
	exporter.Update();
	for (int i = 0; i < methodCount; i++) {
		erosion.PollMeasure(static_cast<erosion::Method>(i), eroded[i].lastMs);
	}

	const bool leftInfinite = chunksShown && !infiniteGeneration;
	chunksShown = infiniteGeneration;
	//The noise changed while only the chunks were shown
	if (leftInfinite && finiteOutdated) {
		RequestNoise();
		finiteOutdated = false;
	}
	//The finite map shows the part of the world under the camera; a new part is new noise, which resets erosion
	if (leftInfinite || (centerOnCamera && !infiniteGeneration)) {
		MoveMap(OffsetUnder(camera));
	}
	centerOnCamera = false;
	if (infiniteGeneration) {
		UpdateChunks(camera);
	}
	else {
		GenerateFinite();
		//After the noise map, so that an erosion requested with new noise erodes the new map
		UpdateErosion();
	}
	//After generating: the map size may have changed meanwhile
	if (leftInfinite) {
		view.BringCameraToMap(camera, renderSettings);
	}
}

void NoiseBasedGenerationSys::UpdateErosion()
{
	if (pending.resetErosion) {
		for (ErodedMap& map : eroded) {
			map.shown = false;
			map.fromBase = true;
		}
		pending.resetErosion = false;
	}
	if (!pending.erode) {
		return;
	}
	pending.erode = false;
	const TextureClass* base = view.GetLayerTexture(TerrainView::Layer::TERRAIN);
	if (!base) {
		return;
	}
	const int width = base->GetWidth(), height = base->GetHeight();
	std::vector<erosion::Method> methods = { config.erosion.method };
	if (compareMethods) {
		methods = { erosion::Method::DROPLETS, erosion::Method::PIPES };
	}
	for (erosion::Method method : methods) {
		//An eroded map keeps the size of the noise map: a new size comes with new noise, which resets erosion
		TextureClass* target = nullptr;
		if (method == erosion::Method::PIPES) {
			pipesView.Resize(width, height);
			target = &pipesView.LayerTexture(TerrainView::Layer::TERRAIN, width, height);
		}
		else {
			target = &view.LayerTexture(TerrainView::Layer::OVERLAY, width, height);
		}
		ErodedMap& map = eroded[static_cast<int>(method)];
		erosion::ErosionConfig methodConfig = config.erosion;
		methodConfig.method = method;
		erosion.Erode(methodConfig, map.fromBase ? *base : *target, *target, erosionRuns++);
		map.fromBase = false;
		map.shown = true;
	}
}

const TextureClass* NoiseBasedGenerationSys::ErodedTexture(erosion::Method method) const
{
	return method == erosion::Method::PIPES ? pipesView.GetLayerTexture(TerrainView::Layer::TERRAIN) : view.GetLayerTexture(TerrainView::Layer::OVERLAY);
}

std::vector<erosion::Method> NoiseBasedGenerationSys::ShownMethods() const
{
	std::vector<erosion::Method> methods;
	for (erosion::Method method : { erosion::Method::DROPLETS, erosion::Method::PIPES }) {
		//Without comparing only the chosen method; its map stays hidden until it erodes once after a reset
		const bool wanted = compareMethods || method == config.erosion.method;
		if (wanted && eroded[static_cast<int>(method)].shown && ErodedTexture(method)) {
			methods.push_back(method);
		}
	}
	return methods;
}

const char* NoiseBasedGenerationSys::MethodName(erosion::Method method)
{
	return method == erosion::Method::PIPES ? "Water flow (pipes)" : "Droplets";
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
		//The eroded maps are made from this one, so they lie where it lies
		view.SetMapOffset(mapOffset);
		pipesView.SetMapOffset(mapOffset);
		MapArea area;
		area.origin = glm::vec2(mapOffset);
		area.islandSize = glm::ivec2(config.width, config.height);
		gpu.BeginMeasure();
		gpu.GenerateNoise(config.noise, area, view.LayerTexture(TerrainView::Layer::TERRAIN, config.width, config.height));
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

	if (chunks.Plan(ChunkWorld::ViewFrom(camera, renderSettings)) == 0) {
		return;
	}
	gpu.BeginMeasure();
	chunks.GenerateNext([&](const MapArea& area, TextureClass& height, TextureClass*, TextureClass*, TextureClass*) {
		gpu.GenerateNoise(config.noise, area, height);
	});
	gpu.EndMeasure();
}

void NoiseBasedGenerationSys::RequestNoise()
{
	pending.noise = true;
	pending.resetErosion = true;
}

void NoiseBasedGenerationSys::MoveMap(glm::ivec2 offset)
{
	if (offset != mapOffset) {
		mapOffset = offset;
		RequestNoise();
	}
}

glm::ivec2 NoiseBasedGenerationSys::OffsetUnder(const Camera& camera) const
{
	//The middle of the map lies at its offset * worldUnitsPerTexel (see TerrainGenerationSys::OffsetUnder)
	const glm::vec3 position = camera.GetPosition() / renderSettings.modelScale;
	return glm::ivec2(glm::round(glm::vec2(position.x, position.z) / TerrainView::worldUnitsPerTexel));
}

void NoiseBasedGenerationSys::GoToMap(glm::ivec2 offset, Camera& camera)
{
	//The chunks do not depend on the offset, and leaving the infinite world puts the map under the camera anyway
	if (!infiniteGeneration) {
		MoveMap(offset);
	}
	TerrainView::LookDownAtMap(camera, renderSettings, offset, glm::ivec2(config.width, config.height));
}

void NoiseBasedGenerationSys::LoadPack(const exportpack::PackSource& source, Camera& camera)
{
	if (source.mode != packMode) {
		packs.ReportError("The pack was made in the " + source.mode + " mode");
		return;
	}
	if (source.generation.empty()) {
		packs.ReportError("The pack has no generation parameters");
		return;
	}
	if (!preset::ValidMapOffset(source.worldOrigin)) {
		packs.ReportError("The world position of the pack is out of range");
		return;
	}
	NoiseMapConfig loaded;
	std::string error;
	if (!preset::FromJson(source.generation, loaded, error)) {
		packs.ReportError("Generation parameters: " + error);
		return;
	}
	ApplyConfig(loaded);
	GoToMap(source.worldOrigin, camera);
	packs.ReportLoaded();
}

void NoiseBasedGenerationSys::RequestErosion()
{
	pending.erode = true;
}

void NoiseBasedGenerationSys::ImGuiPresets(Camera& camera)
{
	std::string error;
	switch (presets.ImGuiDraw()) {
	case PresetPanel::Action::SAVE: {
		const std::optional<glm::ivec2> offset = presetPosition ? std::optional<glm::ivec2>(PlaceOffset(camera)) : std::nullopt;
		if (preset::Save(presets.SavePath(), config, offset, error)) {
			presets.ReportSaved();
		}
		else {
			presets.ReportError(error);
		}
		break;
	}
	case PresetPanel::Action::LOAD: {
		NoiseMapConfig loaded;
		std::optional<glm::ivec2> offset;
		if (preset::Load(presets.LoadPath(), loaded, offset, error)) {
			ApplyConfig(loaded);
			if (presetPosition && offset) {
				GoToMap(*offset, camera);
			}
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
	ImGui::Checkbox("Map position", &presetPosition);
	ImGui::SetItemTooltip("Saving: the preset also keeps where the map lies (in the infinite world: the map under the camera).\n"
		"Loading: a preset with a position moves the map there and the camera above it. Off = only the parameters");

	ImGui::SeparatorText("Places");
	const auto here = [&](const std::string& name) {
		places::Place place;
		place.name = name;
		place.camera = camera.GetPosition() / renderSettings.modelScale;
		place.yaw = camera.GetYaw();
		place.pitch = camera.GetPitch();
		place.mapOffset = PlaceOffset(camera);
		return place;
	};
	if (const std::optional<places::Place> place = places.ImGuiDraw(here)) {
		//As in GoToMap: the infinite world needs only the camera
		if (!infiniteGeneration) {
			MoveMap(place->mapOffset);
		}
		camera.SetView(place->camera * renderSettings.modelScale, place->yaw, place->pitch);
	}

	ImGui::SeparatorText("Export packs");
	if (const std::optional<exportpack::PackSource> source = packs.ImGuiDraw()) {
		LoadPack(*source, camera);
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
	labels.clear();
	if (chunksShown) {
		chunks.Draw(renderer, camera, light, renderSettings);
		return;
	}
	//Eroded maps next to the base one along X, separated by a 5% gap; the 2D view frames all of them
	const float shift = view.GetWorldWidth() * 1.05f;
	const glm::vec2 half(view.GetWorldWidth() / 2.0f, view.GetWorldHeight() / 2.0f);
	const std::vector<erosion::Method> methods = ShownMethods();
	//Around the middle of the map, which lies at its offset (TerrainView::GetWorldCenter); Draw adds that to the offsets below
	const glm::vec2 center = view.GetWorldCenter();
	const glm::vec2 flatMin = center - half, flatMax = center + glm::vec2(shift * static_cast<float>(methods.size()) + half.x, half.y);
	//Both views draw with the same 2D camera
	view.SetFlatViewBounds(flatMin, flatMax);
	pipesView.SetFlatViewBounds(flatMin, flatMax);

	view.Draw(renderer, camera, light, renderSettings);
	std::vector<std::pair<float, std::string>> mapNames = { { 0.0f, "Noise map" } };
	for (size_t i = 0; i < methods.size(); i++) {
		const glm::vec3 offset(shift * static_cast<float>(i + 1), 0.0f, 0.0f);
		if (methods[i] == erosion::Method::PIPES) {
			pipesView.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::TERRAIN, offset);
		}
		else {
			view.Draw(renderer, camera, light, renderSettings, TerrainView::Layer::OVERLAY, offset);
		}
		mapNames.push_back({ offset.x, std::string("Eroded: ") + MethodName(methods[i]) });
	}
	//A single map needs no name
	if (methods.empty()) {
		return;
	}
	const glm::mat4 viewProjection = renderSettings.map2d
		? [&] { const FrameUniforms::Block block = TerrainView::FrameBlock(camera, light, renderSettings, flatMin, flatMax); return block.projection * block.view; }()
		: camera.GetViewProjection();
	PlaceLabels(viewProjection, mapNames);
}

void NoiseBasedGenerationSys::PlaceLabels(const glm::mat4& viewProjection, const std::vector<std::pair<float, std::string>>& maps)
{
	//Viewport of this frame in framebuffer pixels, origin at the bottom left; ImGui counts window points from the top left
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	const ImGuiIO& io = ImGui::GetIO();
	const float framebufferHeight = io.DisplaySize.y * io.DisplayFramebufferScale.y;

	const float halfDepth = view.GetWorldHeight() / 2.0f;
	//Above the highest possible terrain, so that the label is not inside a mountain in 3D; the 2D view ignores the height
	const float top = std::abs(renderSettings.heightScale);
	const glm::vec2 center = view.GetWorldCenter();
	for (const auto& [offsetX, text] : maps) {
		const glm::vec3 world = renderSettings.modelScale * glm::vec3(center.x + offsetX, top, center.y - halfDepth);
		const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
		//Behind the camera
		if (clip.w <= 1e-4f) {
			continue;
		}
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		if (ndc.z < -1.0f || ndc.z > 1.0f) {
			continue;
		}
		const float x = viewport[0] + (ndc.x * 0.5f + 0.5f) * viewport[2];
		const float y = framebufferHeight - (viewport[1] + (ndc.y * 0.5f + 0.5f) * viewport[3]);
		labels.push_back({ ImVec2(x / io.DisplayFramebufferScale.x, y / io.DisplayFramebufferScale.y), text });
	}
	//Kept for ImGuiMapLabels: the viewport rectangle in ImGui coordinates, labels outside of it are clipped
	labelClipMin = ImVec2(viewport[0] / io.DisplayFramebufferScale.x, (framebufferHeight - viewport[1] - viewport[3]) / io.DisplayFramebufferScale.y);
	labelClipMax = ImVec2((viewport[0] + viewport[2]) / io.DisplayFramebufferScale.x, (framebufferHeight - viewport[1]) / io.DisplayFramebufferScale.y);
}

void NoiseBasedGenerationSys::ImGuiMapLabels()
{
	if (labels.empty()) {
		return;
	}
	//The background list draws over the 3D scene and under the panels
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float size = ImGui::GetFontSize() * 1.3f;
	const ImVec2 padding(6.0f, 3.0f);
	drawList->PushClipRect(labelClipMin, labelClipMax, true);
	for (const MapLabel& label : labels) {
		const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.text.c_str());
		//Centered over the top edge of the map, just above it
		const ImVec2 textPos(label.position.x - textSize.x * 0.5f, label.position.y - textSize.y - padding.y * 2.0f - 4.0f);
		drawList->AddRectFilled(ImVec2(textPos.x - padding.x, textPos.y - padding.y), ImVec2(textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y),
			IM_COL32(0, 0, 0, 170), 4.0f);
		drawList->AddText(font, size, textPos, IM_COL32(255, 255, 255, 255), label.text.c_str());
	}
	drawList->PopClipRect();
}

void NoiseBasedGenerationSys::ImGuiRightPanel()
{
	const PanelTabs::Tab tabs[] = {
		{ "Noise", [this] { NoiseTabImGui(); } },
		{ "Erosion", [this] { ErosionImGui(); } },
	};
	generationTabs.Draw("generation", tabs);
}

void NoiseBasedGenerationSys::NoiseTabImGui()
{
	if(utilities::MapSizeImGui(config.width, config.height)) {
		RequestNoise();
	}

	ImGui::SeparatorText("Noise");
	ImGui::Checkbox("Instant Update", &instantUpdate);
	if (!instantUpdate) {
		ImGui::SameLine();
		if (ImGui::Button("Generate new noise")) {
			RequestNoise();
		}
	}
	const bool regen = utilities::NoiseImGui(config.noise);
	if (instantUpdate && regen) {
		RequestNoise();
	}
}

void NoiseBasedGenerationSys::ImGuiDisplay()
{
	TerrainRenderSettings& s = renderSettings;
	if (utilities::DisplayModeImGui(s.modelScale, s.topoStep, s.topoBandWidth, s.heightScale, s.displayMode, s.wireFrame, s.map2d, &infiniteGeneration)) {
		if(s.displayMode == utilities::heightMapMode::BIOMES) {
			s.displayMode = utilities::heightMapMode::GREYSCALE;
		}
	}
	utilities::TerrainDetailImGui(s.tessPixels);
	if (infiniteGeneration) {
		ImGui::SeparatorText("Infinite generation");
		chunks.ImGuiDraw();
		return;
	}
	ImGui::SeparatorText("Partial map");
	const glm::ivec2 shown = view.GetMapOffset();
	ImGui::Text("Texels %d .. %d x %d .. %d of the world", shown.x, shown.x + view.GetMapWidth() - 1, shown.y, shown.y + view.GetMapHeight() - 1);
	ImGui::SetItemTooltip("Leaving Infinite generation centres the map on the camera; a new position is new noise (erosion starts again)");
	if (ImGui::Button("Centre on camera")) {
		centerOnCamera = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("World origin")) {
		MoveMap(glm::ivec2(0));
	}
}

void NoiseBasedGenerationSys::ImGuiSaving()
{
	if (infiniteGeneration) {
		ImGui::TextWrapped("Saving works on the finite map: switch to Partial generation in the Display tab.");
		return;
	}
	ExportPanel::Pack pack;
	pack.mode = packMode;
	pack.range = heightmap::noiseRange;
	pack.metresPerUnit = renderSettings.heightScale / TerrainView::worldUnitsPerTexel;
	pack.worldOrigin = view.GetMapOffset();
	pack.generation = [this] { return preset::ToJson(config); };
	//The base map, plus every eroded map shown, each in its own file
	pack.heightmaps = { { view.GetMapWidth(), view.GetMapHeight(), "", [this] { return view.ReadLayer(TerrainView::Layer::TERRAIN); } } };
	for (erosion::Method method : ShownMethods()) {
		const TextureClass& texture = *ErodedTexture(method);
		if (method == erosion::Method::PIPES) {
			pack.heightmaps.push_back({ texture.GetWidth(), texture.GetHeight(), "_eroded_pipes", [this] { return pipesView.ReadLayer(TerrainView::Layer::TERRAIN); } });
		}
		else {
			pack.heightmaps.push_back({ texture.GetWidth(), texture.GetHeight(), "_eroded_droplets", [this] { return view.ReadLayer(TerrainView::Layer::OVERLAY); } });
		}
	}
	exporter.ImGuiDraw(pack);
}

void NoiseBasedGenerationSys::ErosionImGui()
{
	if (infiniteGeneration) {
		ImGui::TextWrapped("Erosion works on the finite map: switch to Partial generation in the Display tab.");
		return;
	}
	erosion::ErosionConfig& e = config.erosion;
	ImGui::Checkbox("Compare both methods", &compareMethods);
	ImGui::SetItemTooltip("Erode map runs both methods, each on its own map, shown side by side next to the noise map");
	if (compareMethods) {
		ImGui::TextUnformatted("Parameters of:");
	}
	int method = static_cast<int>(e.method);
	ImGui::RadioButton("Droplets", &method, static_cast<int>(erosion::Method::DROPLETS));
	ImGui::SameLine();
	ImGui::RadioButton("Water flow (pipes)", &method, static_cast<int>(erosion::Method::PIPES));
	e.method = static_cast<erosion::Method>(method);

	if (e.method == erosion::Method::DROPLETS) {
		ImGui::TextWrapped("Particles running down the slopes, eroding and depositing on the way.");
		ImGui::InputInt("Droplet count", &e.dropletCount, 1000, 10000);
		ImGui::InputInt("Droplet lifetime", &e.dropletLifetime);
		ImGui::InputFloat("Inertia", &e.inertia);
		ImGui::InputFloat("Droplet init velocity", &e.initialVelocity, 0.0f, 1.0f);
		ImGui::InputFloat("Droplet init water", &e.initialWater, 0.0f, 1.0f);
		ImGui::InputFloat("Erosion rate", &e.erosionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Deposition rate", &e.depositionRate, 0.0f, 1.0f);
		ImGui::InputFloat("Evaporation rate", &e.evaporationRate, 0.0f, 1.0f);
		ImGui::InputFloat("Gravity", &e.gravity, 0.0f, 10.0f);
		ImGui::InputFloat("Min slope", &e.minSlope, 0.0f, 1.0f);
		ImGui::InputInt("Erosion radius", &e.erosionRadius);
		ImGui::InputFloat("Blur", &e.blur, 0.0f, 1.0f);
		e.dropletCount = std::max(e.dropletCount, 0);
		e.dropletLifetime = std::max(e.dropletLifetime, 0);
		e.erosionRadius = std::max(e.erosionRadius, 0);
	}
	else {
		erosion::PipeConfig& p = e.pipes;
		ImGui::TextWrapped("Rain flowing over the whole map: water carves channels and leaves sediment where it slows down; steep slopes crumble (thermal erosion).");
		ImGui::SliderInt("Iterations", &p.iterations, 1, 2000);
		ImGui::SliderFloat("Time step", &p.timeStep, 0.005f, 0.2f, "%.3f");
		ImGui::SliderFloat("Height scale", &p.heightScale, 1.0f, 256.0f, "%.0f texels");
		ImGui::SetItemTooltip("Height of the map value 1 in texels: higher = steeper slopes, faster water");
		ImGui::SliderFloat("Rain", &p.rainRate, 0.0f, 0.2f, "%.3f");
		ImGui::SliderFloat("Evaporation", &p.evaporationRate, 0.0f, 0.5f, "%.3f");
		ImGui::SliderFloat("Sediment capacity", &p.sedimentCapacity, 0.0f, 5.0f);
		ImGui::SliderFloat("Min tilt", &p.minTilt, 0.0f, 0.5f, "%.3f");
		ImGui::SetItemTooltip("Water on flatter ground carries sediment as if the slope had this sine");
		ImGui::SliderFloat("Dissolving rate", &p.dissolvingRate, 0.0f, 5.0f);
		ImGui::SliderFloat("Deposition rate", &p.depositionRate, 0.0f, 5.0f);
		ImGui::SeparatorText("Thermal erosion");
		ImGui::SliderFloat("Talus angle", &p.talusAngle, 1.0f, 89.0f, "%.0f deg");
		ImGui::SliderFloat("Thermal rate", &p.thermalRate, 0.0f, 5.0f);
		ImGui::SetItemTooltip("0 = no thermal erosion");
		p.iterations = std::max(p.iterations, 0);
		p.timeStep = std::max(p.timeStep, 0.001f);
		p.heightScale = std::max(p.heightScale, 0.01f);
	}

	ImGui::Separator();
	if (ImGui::Button(compareMethods ? "Erode map (both methods)" : "Erode map")) {
		RequestErosion();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		//Drops a not yet started erosion too, so the reset is not followed by an erosion of the old request
		pending.erode = false;
		pending.resetErosion = true;
	}
}

void NoiseBasedGenerationSys::ImGuiOutput()
{
	if (chunksShown) {
		ImGui::Text("Last generation (GPU): %.2f ms (up to %d chunks of %dx%d)", lastGenerationMs, ChunkWorld::maxChunksPerFrame, ChunkWorld::chunkSize, ChunkWorld::chunkSize);
		ImGui::Text("Chunks: %d drawn of %d in range, %d cached, %d waiting", chunks.GetVisibleCount(), chunks.GetInRangeCount(), chunks.GetCachedCount(), chunks.GetPendingCount());
		ImGui::Text("Coarsest level in view: %d (1 sample per %d texels)", chunks.GetCoarsestLevel(), 1 << chunks.GetCoarsestLevel());
		chunks.ImGuiUpdateTimes();
		return;
	}
	ImGui::Text("Last generation (GPU): %.2f ms (%dx%d)", lastGenerationMs, view.GetMapWidth(), view.GetMapHeight());
	ImGui::Text("Last erosion (GPU): droplets %.1f ms, water flow (pipes) %.1f ms", eroded[static_cast<int>(erosion::Method::DROPLETS)].lastMs,
		eroded[static_cast<int>(erosion::Method::PIPES)].lastMs);
}

