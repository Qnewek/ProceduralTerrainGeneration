#include "MapLabels.h"

#include <cfloat>

#include <GL/glew.h>

void MapLabels::Place(const glm::mat4& viewProjection, const std::vector<std::pair<float, std::string>>& maps, glm::vec2 center, float halfDepth, float top,
	float modelScale)
{
	//Viewport of this frame in framebuffer pixels, origin at the bottom left; ImGui counts window points from the top left
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	const ImGuiIO& io = ImGui::GetIO();
	const float framebufferHeight = io.DisplaySize.y * io.DisplayFramebufferScale.y;

	for (const auto& [offsetX, text] : maps) {
		const glm::vec3 world = modelScale * glm::vec3(center.x + offsetX, top, center.y - halfDepth);
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
	//Kept for ImGuiDraw: the viewport rectangle in ImGui coordinates, labels outside of it are clipped
	clipMin = ImVec2(viewport[0] / io.DisplayFramebufferScale.x, (framebufferHeight - viewport[1] - viewport[3]) / io.DisplayFramebufferScale.y);
	clipMax = ImVec2((viewport[0] + viewport[2]) / io.DisplayFramebufferScale.x, (framebufferHeight - viewport[1]) / io.DisplayFramebufferScale.y);
}

void MapLabels::ImGuiDraw() const
{
	if (labels.empty()) {
		return;
	}
	//The background list draws over the 3D scene and under the panels
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float size = ImGui::GetFontSize() * 1.3f;
	const ImVec2 padding(6.0f, 3.0f);
	drawList->PushClipRect(clipMin, clipMax, true);
	for (const Label& label : labels) {
		const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.text.c_str());
		//Centered over the top edge of the map, just above it
		const ImVec2 textPos(label.position.x - textSize.x * 0.5f, label.position.y - textSize.y - padding.y * 2.0f - 4.0f);
		drawList->AddRectFilled(ImVec2(textPos.x - padding.x, textPos.y - padding.y), ImVec2(textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y),
			IM_COL32(0, 0, 0, 170), 4.0f);
		drawList->AddText(font, size, textPos, IM_COL32(255, 255, 255, 255), label.text.c_str());
	}
	drawList->PopClipRect();
}
