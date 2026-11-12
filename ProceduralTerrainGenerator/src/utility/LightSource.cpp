#include "LightSource.h"

#include <cmath>

#include "imgui.h"

glm::vec3 LightSource::GetDirection() const
{
	const float a = glm::radians(azimuth), e = glm::radians(elevation);
	return glm::vec3(std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a));
}

void LightSource::ImGuiDraw() {
	ImGui::SliderFloat("Sun azimuth", &azimuth, 0.0f, 360.0f, "%.0f deg");
	//Past 90 degrees the sun goes over the zenith and down on the opposite side, like during a day
	ImGui::SliderFloat("Sun elevation", &elevation, 0.0f, 180.0f, "%.0f deg");
	//Both are tinted by the sun height in the shader (sky.glsl)
	ImGui::ColorEdit3("Ambient", &ambient.x);
	ImGui::ColorEdit3("Sunlight", &diffuse.x);
	ImGui::Checkbox("Lighting", &lightOn);
	ImGui::SameLine();
	ImGui::Checkbox("Fog", &fog);
	if (fog) {
		//Stored as a share, shown in percent of the camera render distance
		float percent = fogStart * 100.0f;
		if (ImGui::SliderFloat("Fog start", &percent, 0.0f, 99.0f, "%.0f%% of render distance")) {
			fogStart = percent / 100.0f;
		}
	}
}

void LightSource::SetLightUniforms(Shader& shader) const
{
	shader.SetUniform3fv("light.direction", GetDirection());
	shader.SetUniform3fv("light.ambient", ambient);
	shader.SetUniform3fv("light.diffuse", diffuse);
	shader.SetUniform1i("lightOn", lightOn);
}
