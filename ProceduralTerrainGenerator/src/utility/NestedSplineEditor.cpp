#include "NestedSplineEditor.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "implot.h"

#include "NestedSpline.h"

namespace {
	const char* levelNames[] = { "C", "M", "W" };
	const char* levelAxes[] = { "Continentalness", "Mountainousness", "Weirdness" };

	int CountWCurves(const NestedCurve& curve, int depth)
	{
		int count = depth == 2 ? 1 : 0;
		for (const NestedPoint& p : curve.points) {
			for (const NestedCurve& sub : p.sub) {
				count += CountWCurves(sub, depth + 1);
			}
		}
		return count;
	}

	std::string Format(const char* format, double value)
	{
		char text[32];
		std::snprintf(text, sizeof(text), format, value);
		return text;
	}
}

bool NestedSplineEditor::Show(TerrainConfig& terrain)
{
	NestedTerrain& tree = terrain.nested;
	bool changed = false;
	if (tree.root.points.empty()) {
		tree = nested::FromLayered(terrain);
		changed = true;
	}

	//Actions that replace the open curve go first, so the curve below is the one shown
	ImGui::BeginDisabled(path.empty());
	if (ImGui::Button("Back##nested")) {
		path.pop_back();
		selected = dragged = -1;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Copy from Layered##nested")) {
		tree = nested::FromLayered(terrain);
		path.clear();
		selected = dragged = -1;
		changed = true;
	}
	ImGui::SetItemTooltip("Rebuild the tree from the curves and sliders of the Layered method (replaces the tree and the sea level)");
	changed |= ImGui::SliderFloat("Sea level##nested", &tree.seaLevel, 0.0f, 1.0f);
	ImGui::SetItemTooltip("Height of the water surface; the coast is where the terrain crosses it");

	if (!nested::ValidPath(tree.root, path)) {
		selected = dragged = -1;
	}
	NestedCurve* curve = &tree.root;
	std::string crumb = levelNames[0];
	for (size_t i = 0; i < path.size(); i++) {
		const NestedPoint& p = curve->points[path[i]];
		crumb += " > point " + std::to_string(path[i] + 1) + " (" + levelNames[i] + " = " + Format("%.2f", p.x) + ") > " + levelNames[i + 1];
		curve = &curve->points[path[i]].sub[0];
	}
	if (selected >= static_cast<int>(curve->points.size())) {
		selected = dragged = -1;
	}
	ImGui::TextUnformatted(crumb.c_str());
	changed |= ImGui::Checkbox("Smooth (cubic)##nested", &curve->cubic);
	ImGui::SetItemTooltip("Cubic: a smooth curve through the points, never outside their values.\nOff: straight segments between the points");
	ImGui::TextDisabled("Drag a point, double-click to add one, right-click to remove it.\n"
		"Diamonds are points holding a curve: open one with Open in the list below or a double-click.");

	changed |= CurvePlot(*curve, tree.seaLevel);
	changed |= PointList(tree.root, *curve);

	ImGui::SeparatorText("Height over C x M");
	heatStale |= ImGui::SliderFloat("W for the map and the curves##nested", &contextW, -1.0f, 1.0f);
	ImGui::SetItemTooltip("Weirdness at which the map below and the curves over C and M are drawn");
	heatStale |= ImGui::SliderFloat("M for the C curve##nested", &contextM, -1.0f, 1.0f);
	ImGui::SetItemTooltip("Mountainousness at which the curve over C is drawn (the horizontal line in the map)");
	heatStale |= changed;
	//The plot may have opened a curve this frame
	nested::ValidPath(tree.root, path);
	HeatMap(tree);
	return changed;
}

bool NestedSplineEditor::CurvePlot(NestedCurve& curve, float seaLevel)
{
	const int depth = static_cast<int>(path.size());
	auto& points = curve.points;
	if (points.size() < static_cast<size_t>(nested::minPoints)) {
		return false;
	}
	//Distance in pixels at which the mouse picks a point; smallest distance in x between neighbouring points
	const float pickRadius = 10.0f;
	const double minGap = 0.01;
	//Height at input t of this curve, the other noises at the context values
	const auto at = [&](const NestedCurve& c, int level, double t) {
		return nested::EvaluateAt(c, level, level == 0 ? t : 0.0, level == 1 ? t : contextM, level == 2 ? t : contextW);
	};
	//A point holding a curve is drawn at the value of its curve at the context
	const auto shownY = [&](const NestedPoint& p) { return p.sub.empty() ? p.y : nested::EvaluateAt(p.sub[0], depth + 1, 0.0, contextM, contextW); };

	bool changed = false;
	if (ImPlot::BeginPlot("##nestedCurve", ImVec2(-1, 300), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxis(ImAxis_X1, levelAxes[depth], ImPlotAxisFlags_Lock);
		ImPlot::SetupAxis(ImAxis_Y1, "Height", ImPlotAxisFlags_Lock);
		ImPlot::SetupAxisLimits(ImAxis_X1, -1.0, 1.0, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -0.1, 1.1, ImGuiCond_Always);

		constexpr int samples = 256;
		std::vector<double> xs(samples), ys(samples);
		for (int i = 0; i < samples; i++) {
			xs[i] = -1.0 + 2.0 * i / (samples - 1);
			ys[i] = at(curve, depth, xs[i]);
		}
		ImPlot::PlotLine("Curve", xs.data(), ys.data(), samples);
		const double sea = seaLevel;
		ImPlot::PlotInfLines("Sea level", &sea, 1, ImPlotSpec(ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal,
			ImPlotProp_LineColor, ImVec4(0.3f, 0.55f, 1.0f, 0.8f)));

		std::vector<double> plainX, plainY, subX, subY, pointY(points.size());
		for (size_t i = 0; i < points.size(); i++) {
			pointY[i] = shownY(points[i]);
			(points[i].sub.empty() ? plainX : subX).push_back(points[i].x);
			(points[i].sub.empty() ? plainY : subY).push_back(pointY[i]);
		}
		ImPlot::PlotScatter("Points", plainX.data(), plainY.data(), static_cast<int>(plainX.size()),
			ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 4.0f));
		ImPlot::PlotScatter("Curves", subX.data(), subY.data(), static_cast<int>(subX.size()),
			ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Diamond, ImPlotProp_MarkerSize, 7.0f, ImPlotProp_MarkerFillColor, ImVec4(1.0f, 0.6f, 0.2f, 1.0f)));
		if (selected >= 0) {
			const double sx = points[selected].x, sy = pointY[selected];
			ImPlot::PlotScatter("Selected", &sx, &sy, 1, ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 10.0f,
				ImPlotProp_MarkerFillColor, ImVec4(0, 0, 0, 0), ImPlotProp_MarkerLineColor, ImVec4(1, 1, 1, 1)));
		}

		const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
		const ImVec2 mousePixels = ImGui::GetMousePos();
		int hovered = -1;
		if (ImPlot::IsPlotHovered()) {
			for (int i = 0; i < static_cast<int>(points.size()); i++) {
				const ImVec2 p = ImPlot::PlotToPixels(points[i].x, pointY[i]);
				const float dx = p.x - mousePixels.x, dy = p.y - mousePixels.y;
				if (dx * dx + dy * dy <= pickRadius * pickRadius) {
					hovered = i;
					break;
				}
			}
		}
		const int last = static_cast<int>(points.size()) - 1;

		if (hovered != -1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !points[hovered].sub.empty()) {
			//Opens the curve of the point; the plot shows it from the next frame
			path.push_back(hovered);
			selected = dragged = -1;
		}
		else if (dragged == -1 && hovered != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			selected = dragged = hovered;
		}
		else if (dragged != -1) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && dragged <= last) {
				NestedPoint& p = points[dragged];
				//The end points stay at -1 and 1; a point holding a curve moves only along the axis
				if (dragged != 0 && dragged != last) {
					p.x = std::clamp(mouse.x, points[dragged - 1].x + minGap, points[dragged + 1].x - minGap);
				}
				if (p.sub.empty()) {
					p.y = std::clamp(mouse.y, 0.0, 1.0);
				}
				changed = true;
			}
			else {
				dragged = -1;
			}
		}
		else if (hovered != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			//The end points and the minimum number of points stay; a removed point takes its curve with it
			if (hovered != 0 && hovered != last && points.size() > static_cast<size_t>(nested::minPoints)) {
				points.erase(points.begin() + hovered);
				selected = selected == hovered ? -1 : selected > hovered ? selected - 1 : selected;
				changed = true;
			}
		}
		else if (hovered == -1 && ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
			&& points.size() < static_cast<size_t>(nested::maxPoints)) {
			const auto next = std::upper_bound(points.begin(), points.end(), mouse.x, [](double x, const NestedPoint& p) { return x < p.x; });
			if (next != points.begin() && next != points.end() && mouse.x - (next - 1)->x >= minGap && next->x - mouse.x >= minGap) {
				const int index = static_cast<int>(next - points.begin());
				points.insert(next, NestedPoint{ mouse.x, std::clamp(mouse.y, 0.0, 1.0), {} });
				selected = index;
				changed = true;
			}
		}

		if (dragged != -1 || hovered != -1) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		ImPlot::EndPlot();
	}
	return changed;
}

bool NestedSplineEditor::PointList(const NestedCurve& root, NestedCurve& curve)
{
	const int depth = static_cast<int>(path.size());
	const bool deepest = depth + 1 >= nested::maxDepth;
	const bool full = depth + 2 == nested::maxDepth && CountWCurves(root, 0) >= nested::maxWCurves;
	ImGui::SeparatorText(depth == 0 ? "Points of the curve over C" : depth == 1 ? "Points of the curve over M" : "Points of the curve over W");

	//Buttons only note what to do; the points change after the table, so the loop never sees a changed list
	enum class Action { NONE, OPEN, MAKE_CURVE, MAKE_CONSTANT };
	Action action = Action::NONE;
	int target = -1;
	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
	if (ImGui::BeginTable("##nestedPoints", 4, flags)) {
		ImGui::TableSetupColumn("Point");
		ImGui::TableSetupColumn(levelNames[depth]);
		ImGui::TableSetupColumn("Value");
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(curve.points.size()); i++) {
			const NestedPoint& p = curve.points[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			//The whole row selects the point, as a click on it in the plot does
			const std::string label = std::to_string(i + 1);
			if (ImGui::Selectable(label.c_str(), selected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
				selected = i;
			}
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", p.x);
			ImGui::TableNextColumn();
			if (p.sub.empty()) {
				ImGui::Text("%.3f", p.y);
			}
			else {
				//The value it has at the M and W sliders, as the plot draws it
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "curve (%.3f)", nested::EvaluateAt(p.sub[0], depth + 1, 0.0, contextM, contextW));
			}
			ImGui::TableNextColumn();
			if (p.sub.empty()) {
				if (!deepest) {
					ImGui::BeginDisabled(full);
					if (ImGui::SmallButton("Make sub-curve")) {
						action = Action::MAKE_CURVE;
						target = i;
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip(full ? "At most 64 curves over weirdness"
						: depth == 0 ? "The value of this point becomes a curve over mountainousness"
						: "The value of this point becomes a curve over weirdness");
				}
			}
			else {
				if (ImGui::SmallButton("Open")) {
					action = Action::OPEN;
					target = i;
				}
				ImGui::SetItemTooltip(depth == 0 ? "Edit the curve over mountainousness of this point" : "Edit the curve over weirdness of this point");
				ImGui::SameLine();
				if (ImGui::SmallButton("Make constant")) {
					action = Action::MAKE_CONSTANT;
					target = i;
				}
				ImGui::SetItemTooltip("Replace the curve of this point with its value at the M and W sliders below");
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (deepest) {
		ImGui::TextDisabled("Curves over weirdness hold only numbers");
	}

	if (action == Action::OPEN) {
		path.push_back(target);
		selected = dragged = -1;
		return false;
	}
	NestedPoint* p = target >= 0 ? &curve.points[target] : nullptr;
	if (action == Action::MAKE_CURVE) {
		//Flat at the value of the point, so the terrain does not change until it is edited
		NestedCurve sub;
		sub.cubic = curve.cubic;
		for (double x : { -1.0, 0.0, 1.0 }) {
			sub.points.push_back({ x, p->y, {} });
		}
		p->sub = { std::move(sub) };
		selected = target;
		return true;
	}
	if (action == Action::MAKE_CONSTANT) {
		//The value the point shows now: its curve at the context values
		p->y = std::clamp(nested::EvaluateAt(p->sub[0], depth + 1, 0.0, contextM, contextW), 0.0, 1.0);
		p->sub.clear();
		selected = target;
		return true;
	}
	return false;
}

void NestedSplineEditor::HeatMap(const NestedTerrain& tree)
{
	constexpr int cells = 96;
	if (heatStale || heat.size() != static_cast<size_t>(cells * cells) || shownSeaLevel != tree.seaLevel || !nested::Equal(shownRoot, tree.root)) {
		shownRoot = tree.root;
		shownSeaLevel = tree.seaLevel;
		heat.resize(cells * cells);
		//Row 0 is drawn at the top: M = 1
		for (int r = 0; r < cells; r++) {
			for (int col = 0; col < cells; col++) {
				heat[r * cells + col] = static_cast<float>(nested::Evaluate(tree.root, -1.0 + 2.0 * (col + 0.5) / cells, 1.0 - 2.0 * (r + 0.5) / cells, contextW));
			}
		}
		//Coast: between neighbouring cells on the two sides of the sea level
		coastX.clear();
		coastY.clear();
		const auto below = [&](int r, int col) { return heat[r * cells + col] < tree.seaLevel; };
		for (int r = 0; r < cells; r++) {
			for (int col = 0; col < cells; col++) {
				const float x = -1.0f + 2.0f * (col + 0.5f) / cells, y = 1.0f - 2.0f * (r + 0.5f) / cells;
				if (col + 1 < cells && below(r, col) != below(r, col + 1)) {
					coastX.push_back(x + 1.0f / cells);
					coastY.push_back(y);
				}
				if (r + 1 < cells && below(r, col) != below(r + 1, col)) {
					coastX.push_back(x);
					coastY.push_back(y - 1.0f / cells);
				}
			}
		}
		heatStale = false;
	}
	if (ImPlot::BeginPlot("##nestedHeat", ImVec2(-1, 300), ImPlotFlags_NoMenus | ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxis(ImAxis_X1, "Continentalness", ImPlotAxisFlags_Lock);
		ImPlot::SetupAxis(ImAxis_Y1, "Mountainousness", ImPlotAxisFlags_Lock);
		ImPlot::SetupAxisLimits(ImAxis_X1, -1.0, 1.0, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImGuiCond_Always);
		ImPlot::PushColormap(ImPlotColormap_Viridis);
		ImPlot::PlotHeatmap("Height", heat.data(), cells, cells, 0.0, 1.0, nullptr, ImPlotPoint(-1.0, -1.0), ImPlotPoint(1.0, 1.0));
		ImPlot::PopColormap();
		ImPlot::PlotScatter("Coast", coastX.data(), coastY.data(), static_cast<int>(coastX.size()),
			ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 1.5f, ImPlotProp_MarkerFillColor, ImVec4(1, 1, 1, 1),
				ImPlotProp_MarkerLineColor, ImVec4(1, 1, 1, 1)));
		const double m = contextM;
		ImPlot::PlotInfLines("M", &m, 1, ImPlotSpec(ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal, ImPlotProp_LineColor, ImVec4(1, 1, 1, 0.5f)));
		//Where the open curves lie: C of the open root point, M of the open point of its curve
		if (!path.empty()) {
			const double c = tree.root.points[path[0]].x;
			ImPlot::PlotInfLines("C", &c, 1, ImPlotSpec(ImPlotProp_LineColor, ImVec4(1.0f, 0.6f, 0.2f, 0.8f)));
		}
		if (path.size() > 1) {
			const double mOpen = tree.root.points[path[0]].sub[0].points[path[1]].x;
			ImPlot::PlotInfLines("M open", &mOpen, 1, ImPlotSpec(ImPlotProp_Flags, ImPlotInfLinesFlags_Horizontal,
				ImPlotProp_LineColor, ImVec4(1.0f, 0.6f, 0.2f, 0.8f)));
		}
		ImPlot::EndPlot();
	}
}
