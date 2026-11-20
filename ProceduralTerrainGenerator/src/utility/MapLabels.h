#pragma once

#include <string>
#include <utility>
#include <vector>

#include "glm/glm.hpp"
#include "imgui.h"

//Names of maps drawn side by side (a base map and its eroded copies along X), over the 3D scene
class MapLabels {
public:
	//Projects the middle of the top edge of every map to the screen; call after the maps are drawn, with the viewport of the scene bound
	//@param maps - offset along X from the base map (world units before modelScale) and the name
	//@param center - world XZ of the middle of the base map, halfDepth = half of its extent along Z
	//@param top - height of the labels (above the highest terrain, so that a mountain does not hide them in 3D)
	void Place(const glm::mat4& viewProjection, const std::vector<std::pair<float, std::string>>& maps, glm::vec2 center, float halfDepth, float top,
		float modelScale);
	//Draws the labels placed by the last Place; in the ImGui frame, after the panels
	void ImGuiDraw() const;
	void Clear() { labels.clear(); }

private:
	//At the top edge of each map in viewport pixels (ImGui coordinates)
	struct Label {
		ImVec2 position;
		std::string text;
	};
	std::vector<Label> labels;
	//Viewport of the last Place in ImGui coordinates; labels are clipped to it
	ImVec2 clipMin, clipMax;
};
