#include "PanelTabs.h"

#include <algorithm>
#include <vector>

#include "imgui.h"

namespace {
	//Tab colors: dark navy, a lighter navy under the mouse, teal for the selected tab
	const ImVec4 tabColor(0.08f, 0.11f, 0.24f, 1.0f);
	const ImVec4 tabHoveredColor(0.14f, 0.20f, 0.40f, 1.0f);
	const ImVec4 tabSelectedColor(0.10f, 0.52f, 0.56f, 1.0f);
	const ImVec4 tabSelectedHoveredColor(0.14f, 0.60f, 0.64f, 1.0f);
}

int PanelTabs::Begin(const char* id, std::span<const char* const> labels)
{
	ImGui::PushID(id);
	int index = 0;
	for (int i = 0; i < static_cast<int>(labels.size()); i++) {
		if (selected == labels[i]) {
			index = i;
			break;
		}
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const float rowWidth = ImGui::GetContentRegionAvail().x;
	//Larger than ordinary buttons, so the tabs are easy to hit and stand out from the content
	const float tabPaddingX = style.FramePadding.x * 3.0f;
	const float tabHeight = ImGui::GetTextLineHeight() + style.FramePadding.y * 4.0f;
	//Tabs touch each other horizontally, rows are a little apart
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	float x = 0.0f;
	for (int i = 0; i < static_cast<int>(labels.size()); i++) {
		const float width = ImGui::CalcTextSize(labels[i], nullptr, true).x + tabPaddingX * 2.0f;
		//The next tab goes on the same row if it fits there
		if (i > 0 && x + 1.0f + width <= rowWidth) {
			ImGui::SameLine();
			x += 1.0f;
		}
		else {
			x = 0.0f;
		}
		x += width;

		const bool isSelected = i == index;
		ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? tabSelectedColor : tabColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSelected ? tabSelectedHoveredColor : tabHoveredColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, tabSelectedHoveredColor);
		if (ImGui::Button(labels[i], ImVec2(width, tabHeight))) {
			index = i;
		}
		ImGui::PopStyleColor(3);
	}
	ImGui::PopStyleVar(2);
	ImGui::Separator();

	changed = selected != labels[index];
	selected = labels[index];

	//Always shows the scrollbar: one that comes and goes changes the width, and content sized to the width
	//(e.g. the noise preview) then changes the height and toggles the scrollbar back - flickering every frame
	ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	return index;
}

void PanelTabs::End()
{
	ImGui::EndChild();
	ImGui::PopID();
}

void PanelTabs::Draw(const char* id, std::span<const Tab> tabs)
{
	std::vector<const char*> labels;
	labels.reserve(tabs.size());
	for (const Tab& tab : tabs) {
		labels.push_back(tab.label);
	}
	const int index = Begin(id, labels);
	tabs[index].draw();
	End();
}
