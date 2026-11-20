#include "ErosionPanel.h"

#include <algorithm>

#include "imgui.h"

const char* ErosionMethodName(erosion::Method method)
{
	return method == erosion::Method::PIPES ? "Water flow (pipes)" : "Droplets";
}

void ErosionParametersImGui(erosion::ErosionConfig& e)
{
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
}
