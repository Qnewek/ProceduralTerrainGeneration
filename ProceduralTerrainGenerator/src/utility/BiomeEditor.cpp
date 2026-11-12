#include "BiomeEditor.h"

#include <algorithm>
#include <string>
#include <utility>

#include "imgui.h"
#include "implot.h"
#include "glm/gtc/type_ptr.hpp"

namespace {
	//InputText on a std::string, growing it as the text grows (as imgui_stdlib does)
	int ResizeString(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
			std::string* text = static_cast<std::string*>(data->UserData);
			text->resize(static_cast<size_t>(data->BufTextLen));
			data->Buf = text->data();
		}
		return 0;
	}

	bool InputString(const char* label, std::string& text)
	{
		return ImGui::InputText(label, text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize, ResizeString, &text);
	}
}

void BiomeEditor::Reset()
{
	selected = 0;
}

bool BiomeEditor::ImGuiDraw(std::vector<biome::Biome>& table)
{
	if (table.empty()) {
		//Never happens: BiomeConfig and presets keep at least one biome
		ImGui::TextDisabled("[The biome table is empty]");
		return false;
	}
	const int count = static_cast<int>(table.size());
	selected = std::clamp(selected, 0, count - 1);
	bool changed = false;

	ImGui::TextDisabled("Each texel gets the nearest biome; on equal distances the one higher in the list wins");
	if (ImGui::BeginListBox("##biomes", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * std::min(count, 8) + ImGui::GetStyle().FramePadding.y * 2))) {
		for (int i = 0; i < count; i++) {
			ImGui::PushID(i);
			const glm::vec3& c = table[i].color;
			ImGui::ColorButton("##color", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
				ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
			ImGui::SameLine();
			//An empty name still needs a row to click
			const std::string label = (table[i].name.empty() ? std::string("(unnamed)") : table[i].name) + "##row";
			if (ImGui::Selectable(label.c_str(), selected == i)) {
				selected = i;
			}
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}

	//The copy of the selected biome changes nothing until edited: it covers the same ranges and the original wins ties
	ImGui::BeginDisabled(table.size() >= biome::maxBiomes);
	if (ImGui::Button("Add copy")) {
		biome::Biome copy = table[selected];
		copy.name += " copy";
		table.insert(table.begin() + selected + 1, std::move(copy));
		selected++;
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Copy of the selected biome below it (at most %d biomes)", static_cast<int>(biome::maxBiomes));
	ImGui::SameLine();
	ImGui::BeginDisabled(table.size() <= 1);
	if (ImGui::Button("Remove")) {
		table.erase(table.begin() + selected);
		selected = std::min(selected, static_cast<int>(table.size()) - 1);
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("The last biome cannot be removed");
	ImGui::SameLine();
	ImGui::BeginDisabled(selected == 0);
	if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
		std::swap(table[selected], table[selected - 1]);
		selected--;
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selected == static_cast<int>(table.size()) - 1);
	if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
		std::swap(table[selected], table[selected + 1]);
		selected++;
		changed = true;
	}
	ImGui::EndDisabled();

	changed |= WhittakerDiagram(table);

	biome::Biome& b = table[selected];
	ImGui::SeparatorText("Selected biome");
	//The name does not change the map
	InputString("Name", b.name);
	changed |= ImGui::ColorEdit3("Color", glm::value_ptr(b.color), ImGuiColorEditFlags_Float);
	ImGui::TextDisabled("Value ranges (min, max) of the parameters the biome covers");
	for (int i = 0; i < biome::PARAMETER_COUNT; i++) {
		biome::Range& r = b.ranges[i];
		//Clamped to the parameter range, min <= max kept by the widget
		changed |= ImGui::DragFloatRange2(biome::parameterNames[i], &r.min, &r.max, 0.005f, -1.0f, 1.0f, "%.3f", nullptr, ImGuiSliderFlags_AlwaysClamp);
		if (i == biome::ELEVATION) {
			ImGui::SetItemTooltip("Height relative to the water surface: 0 = the water surface, -1 = the deepest sea floor, 1 = the highest peak");
		}
	}
	if (ImGui::Button("Full range")) {
		for (biome::Range& r : b.ranges) {
			r = biome::Range{};
		}
		changed = true;
	}
	ImGui::SetItemTooltip("Set all ranges to [-1, 1]");

	ImGui::SeparatorText("Terrain modifier");
	biome::TerrainModifier& t = b.terrain;
	changed |= ImGui::SliderFloat("Amplitude", &t.amplitude, 0.0f, 3.0f);
	ImGui::SetItemTooltip("Scales the height above (and depth below) the water: < 1 flattens (swamps, plains), > 1 sharpens (mountains)");
	changed |= ImGui::SliderFloat("Offset", &t.offset, -0.3f, 0.3f, "%.3f");
	ImGui::SetItemTooltip("Raises or lowers the whole biome (plateaus, basins)");
	changed |= ImGui::SliderFloat("Detail height", &t.detailAmplitude, 0.0f, 0.1f, "%.4f");
	ImGui::SetItemTooltip("Height of an extra small noise: dunes, rough rock; 0 = none");
	ImGui::BeginDisabled(t.detailAmplitude == 0.0f);
	//Divides the position in the shader, so it stays above 0
	changed |= ImGui::SliderFloat("Detail size", &t.detailSize, 2.0f, 256.0f, "%.1f texels", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("Wavelength of the detail noise in map texels");
	changed |= ImGui::Checkbox("Sharp crests", &t.detailRidge);
	ImGui::SetItemTooltip("Ridged detail noise (1 - 2|n|): sharp crests like dunes or rock ridges");
	ImGui::EndDisabled();
	if (ImGui::Button("No modifier")) {
		t = biome::TerrainModifier{};
		changed = true;
	}
	ImGui::SetItemTooltip("The biome keeps the terrain as it is");

	changed |= MaterialLayers(b);
	return changed;
}

void BiomeEditor::SetMaterialNames(std::vector<std::string> names)
{
	materialNames = std::move(names);
}

bool BiomeEditor::MaterialLayers(biome::Biome& b)
{
	ImGui::SeparatorText("Surface materials");
	ImGui::TextDisabled("Mix of up to %d materials, seen in the TEXTURED display mode", biome::materialLayers);
	auto nameOf = [&](int material) {
		return material < static_cast<int>(materialNames.size()) ? materialNames[material] : "Material " + std::to_string(material);
	};
	float sum = 0.0f;
	for (const biome::MaterialLayer& layer : b.materials) {
		sum += layer.weight;
	}

	bool changed = false;
	//Without the material list (it could not be read) any index can still be chosen
	const int choices = materialNames.empty() ? biome::maxMaterials : std::min(static_cast<int>(materialNames.size()), biome::maxMaterials);
	for (int l = 0; l < biome::materialLayers; l++) {
		biome::MaterialLayer& layer = b.materials[l];
		ImGui::PushID(l);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
		if (ImGui::BeginCombo("##material", nameOf(layer.material).c_str())) {
			for (int m = 0; m < choices; m++) {
				if (ImGui::Selectable(nameOf(m).c_str(), m == layer.material)) {
					layer.material = m;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
		changed |= ImGui::SliderFloat("##weight", &layer.weight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SameLine();
		//Share after normalization, as the shader uses it
		if (layer.weight > 0.0f && sum > 0.0f) {
			ImGui::Text("%3.0f%%", 100.0f * layer.weight / sum);
		}
		else {
			ImGui::TextDisabled("unused");
		}
		ImGui::PopID();
	}
	//A biome needs at least one material
	float newSum = 0.0f;
	for (const biome::MaterialLayer& layer : b.materials) {
		newSum += layer.weight;
	}
	if (newSum <= 0.0f) {
		b.materials[0].weight = 1.0f;
	}
	return changed;
}

bool BiomeEditor::WhittakerDiagram(std::vector<biome::Biome>& table)
{
	using biome::TEMPERATURE;
	using biome::HUMIDITY;
	bool changed = false;
	ImGui::TextDisabled("Temperature x humidity: click a biome to select it, drag the selected one or its edges");
	if (!ImPlot::BeginPlot("##whittaker", ImVec2(-1, 300), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend | ImPlotFlags_NoBoxSelect)) {
		return false;
	}
	//A little more than the noise range, so edges at -1 and 1 can be grabbed
	ImPlot::SetupAxis(ImAxis_X1, "Temperature", ImPlotAxisFlags_Lock);
	ImPlot::SetupAxis(ImAxis_Y1, "Humidity", ImPlotAxisFlags_Lock);
	ImPlot::SetupAxisLimits(ImAxis_X1, -1.1, 1.1, ImGuiCond_Always);
	ImPlot::SetupAxisLimits(ImAxis_Y1, -1.1, 1.1, ImGuiCond_Always);
	ImPlot::SetupFinish();

	const int count = static_cast<int>(table.size());
	const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
	auto contains = [&](const biome::Biome& b) {
		return mouse.x >= b.ranges[TEMPERATURE].min && mouse.x <= b.ranges[TEMPERATURE].max
			&& mouse.y >= b.ranges[HUMIDITY].min && mouse.y <= b.ranges[HUMIDITY].max;
	};

	//Rectangles from the end of the table, so the biomes winning ties lie on top; the selected one last
	ImDrawList* draw = ImPlot::GetPlotDrawList();
	ImPlot::PushPlotClipRect();
	for (int pass = 0; pass < 2; pass++) {
		for (int i = count - 1; i >= 0; i--) {
			if ((i == selected) != (pass == 1)) {
				continue;
			}
			const biome::Biome& b = table[i];
			const ImVec2 a = ImPlot::PlotToPixels(b.ranges[TEMPERATURE].min, b.ranges[HUMIDITY].max);
			const ImVec2 c = ImPlot::PlotToPixels(b.ranges[TEMPERATURE].max, b.ranges[HUMIDITY].min);
			const ImVec4 color(b.color.r, b.color.g, b.color.b, 1.0f);
			draw->AddRectFilled(a, c, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, i == selected ? 0.55f : 0.2f)));
			draw->AddRect(a, c, ImGui::GetColorU32(color), 0.0f, 0, i == selected ? 2.0f : 1.0f);
		}
	}
	ImPlot::PopPlotClipRect();

	//The selected biome as a drag tool: the whole rectangle moves, the edges and corners resize it
	biome::Biome& s = table[selected];
	double x1 = s.ranges[TEMPERATURE].min, x2 = s.ranges[TEMPERATURE].max;
	double y1 = s.ranges[HUMIDITY].min, y2 = s.ranges[HUMIDITY].max;
	bool dragHovered = false, dragHeld = false;
	if (ImPlot::DragRect(0, &x1, &y1, &x2, &y2, ImVec4(1, 1, 1, 0.9f), ImPlotDragToolFlags_None, nullptr, &dragHovered, &dragHeld)) {
		//The ranges stay ordered (edges can be dragged past each other) and inside the noise range.
		//Moving the whole rectangle stops at the border instead of shrinking it
		auto apply = [&](biome::Range& r, double a, double b) {
			const double moveA = a - r.min, moveB = b - r.max;
			if (moveA == moveB) {
				const double move = std::clamp(moveA, -1.0 - r.min, 1.0 - r.max);
				a = r.min + move;
				b = r.max + move;
			}
			r.min = static_cast<float>(std::clamp(std::min(a, b), -1.0, 1.0));
			r.max = static_cast<float>(std::clamp(std::max(a, b), -1.0, 1.0));
		};
		apply(s.ranges[TEMPERATURE], x1, x2);
		apply(s.ranges[HUMIDITY], y1, y2);
		changed = true;
	}

	if (ImPlot::IsPlotHovered() && !dragHovered && !dragHeld) {
		//The smallest rectangle under the mouse, so biomes covering the whole diagram do not hide the others
		int under = -1;
		float underArea = 0.0f;
		std::string names;
		for (int i = 0; i < count; i++) {
			const biome::Biome& b = table[i];
			if (!contains(b)) {
				continue;
			}
			names += (names.empty() ? "" : ", ") + b.name;
			const float area = (b.ranges[TEMPERATURE].max - b.ranges[TEMPERATURE].min) * (b.ranges[HUMIDITY].max - b.ranges[HUMIDITY].min);
			if (under == -1 || area < underArea) {
				under = i;
				underArea = area;
			}
		}
		if (!names.empty()) {
			ImGui::SetTooltip("%s", names.c_str());
		}
		if (under != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			selected = under;
		}
	}
	ImPlot::EndPlot();
	return changed;
}
