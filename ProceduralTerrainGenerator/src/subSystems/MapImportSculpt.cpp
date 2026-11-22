//Sculpt brushes of the "Imported map" mode: painting the working copy with the mouse in the 3D view and on the 2D map (part of MapImportSys)
#include "MapImportSys.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <numbers>
#include <utility>

#include <GL/glew.h>

namespace {
	//Points of the brush outline
	constexpr int outlinePoints = 48;
	constexpr float largestRadius = 512.0f;
	//Longest frame a dab is scaled by: after a stall the brush does not jump by a large step
	constexpr float longestFrame = 0.1f;

	bool TowardsTarget(sculpt::Brush brush)
	{
		return brush == sculpt::Brush::SMOOTH || brush == sculpt::Brush::FLATTEN;
	}

	ImU32 OutlineColor(sculpt::Brush brush)
	{
		switch (brush) {
		case sculpt::Brush::RAISE: return IM_COL32(255, 210, 80, 230);
		case sculpt::Brush::LOWER: return IM_COL32(90, 170, 255, 230);
		case sculpt::Brush::SMOOTH: return IM_COL32(140, 255, 140, 230);
		case sculpt::Brush::FLATTEN: return IM_COL32(255, 255, 255, 230);
		case sculpt::Brush::NOISE: return IM_COL32(230, 120, 255, 230);
		}
		return IM_COL32(255, 255, 255, 230);
	}
}

static_assert(sculpt::worldUnitsPerTexel == TerrainView::worldUnitsPerTexel, "sculpt picking assumes the texel size of TerrainView");

void MapImportSys::UpdateSculpt(const Camera& camera, bool active, float deltaTime)
{
	const bool pressed = std::exchange(paintPressed, false);
	const bool canPaint = sculptOn && active && !map.Empty() && !camera.MouseControl();
	if (stroke) {
		//Let go, also outside the window or after losing the focus (ImGui releases the buttons then), or the mode left
		if (!paintHeld || !canPaint) {
			FinishStroke();
			return;
		}
	}
	else {
		if (!pressed || !canPaint || !hoverTexel || Busy()) {
			return;
		}
		//The map keeps its units during a stroke (colours, Height scale and isolines stay put); raising may go above 1.5 units, with room for a long
		//stroke (a flat map has 1 m units), and letting go fits the units to the map again (ApplyEdit -> ShowMetres)
		float limit = 1.5f;
		if (!TowardsTarget(brush.brush)) {
			const float largest = std::max(std::abs(mapMin), std::abs(mapMax)) * metresPerUnit;
			limit = sculpt::StrokeLimit(metresPerUnit, largest, raiseRate);
		}
		const glm::ivec2 size(map.GetWidth(), map.GetHeight());
		const glm::ivec2 texel = glm::clamp(glm::ivec2(glm::round(*hoverTexel)), glm::ivec2(0), size - 1);
		try {
			stroke = Stroke{ WorkingMetres(), map.At(texel.x, texel.y), ++strokeSeed, false, limit };
		}
		catch (const std::bad_alloc&) {
			SetEditStatus("Sculpt: not enough memory for the history step", true);
			return;
		}
	}
	//Cursor over the sky or off the map: this frame paints nothing
	if (!hoverTexel) {
		return;
	}
	const float dt = std::clamp(deltaTime, 0.0f, longestFrame);
	sculpt::BrushConfig config = brush;
	if (paintInvert && config.brush == sculpt::Brush::RAISE) {
		config.brush = sculpt::Brush::LOWER;
	}
	else if (paintInvert && config.brush == sculpt::Brush::LOWER) {
		config.brush = sculpt::Brush::RAISE;
	}
	sculpt::Dab dab;
	dab.center = *hoverTexel;
	if (TowardsTarget(config.brush)) {
		config.strength = smoothRate;
		dab.amount = std::min(smoothRate * dt, 1.0f);
	}
	else {
		config.strength = raiseRate;
		dab.amount = raiseRate * dt / metresPerUnit;
	}
	dab.target = stroke->target;
	dab.seed = stroke->seed;
	//Draw gives the terrain culling the same bound; after the stroke the units follow the new highest point
	dab.limit = stroke->limit;
	sculptor.Apply(config, dab, view.LayerTexture(TerrainView::Layer::TERRAIN, map.GetWidth(), map.GetHeight()));
	stroke->painted = true;
}

void MapImportSys::FinishStroke()
{
	Stroke finished = std::move(*stroke);
	stroke.reset();
	if (!finished.painted) {
		return;
	}
	const std::string label = std::string("Sculpt: ") + sculpt::BrushName(brush.brush);
	try {
		HeightField metres = view.ReadLayer(TerrainView::Layer::TERRAIN);
		for (size_t i = 0; i < metres.Size(); i++) {
			metres[i] *= metresPerUnit;
		}
		ApplyEdit(std::move(metres), seaLevelMetres, { label, std::move(finished.before), seaLevelMetres });
		SetEditStatus(label, false);
	}
	catch (const std::bad_alloc&) {
		//The GPU shows the stroke, the CPU map does not have it: back to the map
		view.SetLayer(TerrainView::Layer::TERRAIN, map);
		SetEditStatus(label + " failed: not enough memory", true);
	}
}

void MapImportSys::PickUnderCursor(Camera& camera, LightSource& light)
{
	hoverTexel.reset();
	brushOutline.clear();
	const ImGuiIO& io = ImGui::GetIO();
	//Over a panel only while no stroke runs: a stroke dragged over a panel goes on (ImGui does not take a button pressed over the scene)
	if (!sculptOn || map.Empty() || camera.MouseControl() || !ImGui::IsMousePosValid() || (io.WantCaptureMouse && !stroke)) {
		return;
	}
	//Viewport in framebuffer pixels, origin at the bottom left; ImGui counts window points from the top left
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	const glm::vec2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
	const float framebufferHeight = io.DisplaySize.y * scale.y;
	const glm::vec2 cursor(io.MousePos.x * scale.x, framebufferHeight - io.MousePos.y * scale.y);
	const glm::ivec2 pixel(glm::floor(cursor));
	if (pixel.x < viewport[0] || pixel.y < viewport[1] || pixel.x >= viewport[0] + viewport[2] || pixel.y >= viewport[1] + viewport[3]) {
		return;
	}
	//One pixel of the terrain just drawn (waits for the GPU to draw it)
	float depth = 1.0f;
	glReadPixels(pixel.x, pixel.y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
	//The matrices the terrain was drawn with, also the top-down camera of the 2D map
	const FrameUniforms::Block block = TerrainView::FrameBlock(camera, light, renderSettings, view.GetFlatViewMin(), view.GetFlatViewMax());
	const glm::mat4 viewProjection = block.projection * block.view;
	const glm::ivec4 viewportRect(viewport[0], viewport[1], viewport[2], viewport[3]);
	const glm::ivec2 size(map.GetWidth(), map.GetHeight());
	hoverTexel = sculpt::PickTexel(glm::vec2(pixel) + 0.5f, depth, viewportRect, viewProjection, renderSettings.modelScale, size);
	if (!hoverTexel) {
		return;
	}

	//The outline on the terrain: heights of the map from before a running stroke, close enough for a guide
	for (int i = 0; i < outlinePoints; i++) {
		const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / outlinePoints;
		const glm::vec2 texel = *hoverTexel + brush.radius * glm::vec2(std::cos(angle), std::sin(angle));
		const glm::ivec2 nearest = glm::clamp(glm::ivec2(glm::round(texel)), glm::ivec2(0), size - 1);
		const glm::vec3 world = sculpt::TexelToWorld(texel, map.At(nearest.x, nearest.y), renderSettings.heightScale, renderSettings.modelScale, size);
		const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
		//Behind the camera
		if (clip.w <= 1e-4f) {
			continue;
		}
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		const float x = viewport[0] + (ndc.x * 0.5f + 0.5f) * viewport[2];
		const float y = framebufferHeight - (viewport[1] + (ndc.y * 0.5f + 0.5f) * viewport[3]);
		brushOutline.push_back(ImVec2(x / scale.x, y / scale.y));
	}
	outlineClipMin = ImVec2(viewport[0] / scale.x, (framebufferHeight - viewport[1] - viewport[3]) / scale.y);
	outlineClipMax = ImVec2((viewport[0] + viewport[2]) / scale.x, (framebufferHeight - viewport[1]) / scale.y);
}

void MapImportSys::SculptInputImGui()
{
	const ImGuiIO& io = ImGui::GetIO();
	//Kept for Update, which runs before the next ImGui frame. Alt + left button is left to the camera
	paintHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	paintPressed = paintPressed || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse && !io.KeyAlt);
	paintInvert = io.KeyCtrl;
	if (!sculptOn) {
		return;
	}
	if (!io.WantCaptureMouse && io.KeyShift && io.MouseWheel != 0.0f) {
		brush.radius = std::clamp(brush.radius * std::pow(1.1f, io.MouseWheel), 1.0f, largestRadius);
	}
	if (brushOutline.size() >= 2) {
		//The background list draws over the 3D scene and under the panels
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		drawList->PushClipRect(outlineClipMin, outlineClipMax, true);
		drawList->AddPolyline(brushOutline.data(), static_cast<int>(brushOutline.size()), IM_COL32(0, 0, 0, 140), ImDrawFlags_Closed, 3.5f);
		drawList->AddPolyline(brushOutline.data(), static_cast<int>(brushOutline.size()), OutlineColor(brush.brush), ImDrawFlags_Closed, 1.5f);
		drawList->PopClipRect();
	}
}

void MapImportSys::SculptImGui()
{
	ImGui::SeparatorText("Sculpt");
	ImGui::Checkbox("Sculpt with the mouse", &sculptOn);
	ImGui::SetItemTooltip("Left button paints on the terrain, in 3D and on the 2D map");
	if (!sculptOn) {
		return;
	}
	ImGui::BeginDisabled(Busy());
	const sculpt::Brush brushes[] = { sculpt::Brush::RAISE, sculpt::Brush::LOWER, sculpt::Brush::SMOOTH, sculpt::Brush::FLATTEN, sculpt::Brush::NOISE };
	for (size_t i = 0; i < std::size(brushes); i++) {
		if (i > 0) {
			ImGui::SameLine();
		}
		if (ImGui::RadioButton(sculpt::BrushName(brushes[i]), brush.brush == brushes[i])) {
			brush.brush = brushes[i];
		}
	}
	ImGui::SliderFloat("Radius (px)##sculpt", &brush.radius, 1.0f, largestRadius, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	if (TowardsTarget(brush.brush)) {
		ImGui::SliderFloat("Speed##sculpt", &smoothRate, 0.0f, 10.0f, "%.2f /s", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip(brush.brush == sculpt::Brush::FLATTEN
			? "Part of the way to the height where the stroke started, per second"
			: "Part of the way to the average of the neighbours, per second");
	}
	else {
		ImGui::SliderFloat("Strength (m/s)##sculpt", &raiseRate, 0.1f, 1000.0f, "%.1f m/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::SliderFloat("Hardness##sculpt", &brush.hardness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Full strength up to this part of the radius, then a smooth fall to the edge");
	if (brush.brush == sculpt::Brush::NOISE) {
		ImGui::SliderFloat("Noise scale (px)##sculpt", &brush.noiseScale, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled("Left button: paint, Ctrl: raise <-> lower, Shift + wheel: radius");
}
