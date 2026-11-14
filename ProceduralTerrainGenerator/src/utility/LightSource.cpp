#include "LightSource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

namespace {
	//World units per metre (vegetation::unitsPerMetre in TerrainGen); the haze is set in metres
	constexpr float unitsPerMetre = 4.0f;
	//Extinction that leaves 2% of the light over the visibility distance: -ln(0.02)
	constexpr float visibilityContrast = 3.912f;
	//The night passes this many times faster in the day cycle
	constexpr float nightSpeedUp = 3.0f;
}

glm::vec3 LightSource::GetDirection() const
{
	const float a = glm::radians(azimuth), e = glm::radians(elevation);
	return glm::vec3(std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a));
}

void LightSource::SetElevation(float degrees)
{
	elevation = std::fmod(degrees + 90.0f, 360.0f);
	elevation = (elevation < 0.0f ? elevation + 360.0f : elevation) - 90.0f;
}

void LightSource::Update(float seconds)
{
	const float wind = glm::radians(windAzimuth);
	//Wrapped well inside the float precision; the cloud noise repeats long before (Clouds: the weather map tile)
	windOffset = glm::mod(windOffset + glm::vec2(std::cos(wind), std::sin(wind)) * windSpeed * seconds, glm::vec2(240000.0f));
	if (!dayCycle || dayMinutes <= 0.0f) {
		return;
	}
	float degrees = seconds * 360.0f / (dayMinutes * 60.0f);
	if (GetDirection().y < -0.1f) {
		degrees *= nightSpeedUp;
	}
	SetElevation(elevation + degrees);
}

float LightSource::GetHazeDensity() const
{
	return hazeVisibility > 0.0f ? visibilityContrast / (hazeVisibility * 1000.0f * unitsPerMetre) : 0.0f;
}

glm::vec4 LightSource::GetCloudLayer() const
{
	if (!clouds) {
		return glm::vec4(0.0f);
	}
	return glm::vec4(cloudBase, cloudBase + cloudThickness, cloudCoverage, cloudShadows);
}

glm::vec4 LightSource::GetCloudWind() const
{
	return glm::vec4(windOffset, unitsPerMetre, cloudDensity);
}

float LightSource::GetHazeFalloff() const
{
	return 1.0f / (std::max(hazeHeight, 1.0f) * unitsPerMetre);
}

void LightSource::ImGuiDraw() {
	ImGui::SliderFloat("Sun azimuth", &azimuth, 0.0f, 360.0f, "%.0f deg");
	//Shown as a clock; the sun goes over the zenith at noon and under the ground at night
	float hours = GetTimeOfDay();
	const int minutes = static_cast<int>(hours * 60.0f + 0.5f) % (24 * 60);
	char clock[8];
	std::snprintf(clock, sizeof(clock), "%02d:%02d", minutes / 60, minutes % 60);
	if (ImGui::SliderFloat("Time of day", &hours, 0.0f, 24.0f, clock)) {
		SetElevation((hours - 6.0f) * 15.0f);
	}
	ImGui::Checkbox("Day cycle", &dayCycle);
	if (dayCycle) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		ImGui::SliderFloat("Day length", &dayMinutes, 1.0f, 60.0f, "%.0f min");
	}
	//Both are tinted by the light of the sky (sky.glsl): the sunlight by the air it passes, the ambient by the whole sky
	ImGui::ColorEdit3("Ambient", &ambient.x);
	ImGui::ColorEdit3("Sunlight", &diffuse.x);
	ImGui::Checkbox("Lighting", &lightOn);
	ImGui::SameLine();
	ImGui::Checkbox("Fog", &fog);
	if (fog) {
		ImGui::SliderFloat("Haze visibility", &hazeVisibility, 0.0f, 100.0f, hazeVisibility > 0.0f ? "%.1f km" : "no haze");
		ImGui::SliderFloat("Haze height", &hazeHeight, 10.0f, 2000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
		//Stored as a share, shown in percent of the camera render distance
		float percent = fogStart * 100.0f;
		if (ImGui::SliderFloat("Fog start", &percent, 0.0f, 99.0f, "%.0f%% of render distance")) {
			fogStart = percent / 100.0f;
		}
	}
	ImGui::SeparatorText("Clouds");
	ImGui::Checkbox("Clouds", &clouds);
	if (clouds) {
		ImGui::SliderFloat("Coverage", &cloudCoverage, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Cloud density", &cloudDensity, 0.1f, 3.0f, "%.2f");
		//Above the highest terrain (TerrainGen: 256 m), which Clouds relies on while the camera is below the clouds
		ImGui::SliderFloat("Cloud base", &cloudBase, 400.0f, 4000.0f, "%.0f m");
		ImGui::SliderFloat("Cloud thickness", &cloudThickness, 100.0f, 3000.0f, "%.0f m");
		ImGui::SliderFloat("Cloud shadows", &cloudShadows, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Wind speed", &windSpeed, 0.0f, 100.0f, "%.0f m/s");
		ImGui::SliderFloat("Wind direction", &windAzimuth, 0.0f, 360.0f, "%.0f deg");
	}
}
