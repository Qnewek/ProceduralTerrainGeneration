#include "ShadowMap.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "glm/gtc/matrix_transform.hpp"
#include "imgui.h"

#include "Camera.h"
#include "LightSource.h"
#include "TerrainView.h"
#include "ChunkWorld.h"
#include "VegetationLayer.h"
#include "VegetationModels.h"
#include "VegetationPlacement.h"

namespace {
	//Largest absolute terrain height (heightBound in ChunkWorld.cpp) plus the tallest plants, in units of heightScale
	constexpr float casterHeightBound = 1.6f;
	//Below this sun height (sine of the elevation) there is no direct sunlight to shadow
	constexpr float minSunHeight = 0.01f;
	//Steepest sun ray the depth range is extended for: lower suns cast longer, but they are clipped at this length
	constexpr float minRaySine = 0.08f;
}

ShadowMap::~ShadowMap()
{
	if (framebuffer) {
		glDeleteFramebuffers(1, &framebuffer);
	}
	if (texture) {
		glDeleteTextures(1, &texture);
	}
}

bool ShadowMap::CreateTexture()
{
	if (texture && textureResolution == resolution) {
		return true;
	}
	if (texture) {
		glDeleteTextures(1, &texture);
	}
	if (!framebuffer) {
		glCreateFramebuffers(1, &framebuffer);
		glNamedFramebufferDrawBuffer(framebuffer, GL_NONE);
		glNamedFramebufferReadBuffer(framebuffer, GL_NONE);
	}
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texture);
	glTextureStorage3D(texture, 1, GL_DEPTH_COMPONENT32F, resolution, resolution, FrameUniforms::maxShadowCascades);
	//Filtered comparison (2 x 2 taps per lookup); outside the map counts as lit
	glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTextureParameteri(texture, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTextureParameterfv(texture, GL_TEXTURE_BORDER_COLOR, border);
	textureResolution = resolution;
	return true;
}

ShadowMap::Cascade ShadowMap::Fit(const glm::mat4& cameraWorld, glm::vec2 tanHalf, float nearDepth, float farDepth, glm::vec3 sun, float extra) const
{
	//Corners of the slice in world space
	glm::vec3 corners[8];
	int i = 0;
	for (const float z : { nearDepth, farDepth }) {
		for (int c = 0; c < 4; c++) {
			const glm::vec2 xy = glm::vec2((c & 1) ? 1.0f : -1.0f, (c & 2) ? 1.0f : -1.0f) * tanHalf * z;
			corners[i++] = glm::vec3(cameraWorld * glm::vec4(xy, -z, 1.0f));
		}
	}
	glm::vec3 center(0.0f);
	for (const glm::vec3& c : corners) {
		center += c;
	}
	center /= 8.0f;
	float radius = 0.0f;
	for (const glm::vec3& c : corners) {
		radius = std::max(radius, glm::length(c - center));
	}
	//A size that does not change while turning; rounded so tiny changes of the view do not change it either
	radius = std::ceil(radius * 16.0f) / 16.0f;

	//Orientation of the sun only, so moving the camera only moves the box; its centre snapped to whole texels
	const glm::vec3 up = std::abs(sun.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
	const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), -sun, up);
	const float texel = 2.0f * radius / static_cast<float>(resolution);
	glm::vec3 lightCenter = glm::vec3(view * glm::vec4(center, 1.0f));
	lightCenter.x = std::floor(lightCenter.x / texel) * texel;
	lightCenter.y = std::floor(lightCenter.y / texel) * texel;
	//The sun looks down -z of its view; casters lie up to extra further towards it (+z)
	const glm::mat4 projection = glm::ortho(lightCenter.x - radius, lightCenter.x + radius, lightCenter.y - radius, lightCenter.y + radius,
		-(lightCenter.z + radius + extra), -(lightCenter.z - radius));
	Cascade cascade;
	cascade.view = view;
	cascade.projection = projection;
	cascade.viewProjection = projection * view;
	cascade.farDepth = farDepth;
	cascade.texel = texel;
	return cascade;
}

bool ShadowMap::Prepare(Camera& camera, const LightSource& light, const TerrainRenderSettings& settings)
{
	active = false;
	const glm::vec3 sun = light.GetDirection();
	if (!enabled || settings.map2d || !light.IsLightOn() || sun.y < minSunHeight || !CreateTexture()) {
		return false;
	}
	const glm::mat4 projection = *camera.GetProjectionMatrix();
	const glm::mat4 cameraWorld = glm::inverse(*camera.GetViewMatrix());
	const glm::vec2 tanHalf(1.0f / projection[0][0], 1.0f / projection[1][1]);
	const float nearDepth = projection[3][2] / (projection[2][2] - 1.0f);
	const float farDepth = std::min(distanceMetres * vegetation::unitsPerMetre * settings.modelScale, camera.GetRenderDistance());
	if (farDepth <= nearDepth) {
		return false;
	}
	const float casterHeight = casterHeightBound * std::abs(settings.heightScale) * settings.modelScale;
	const float extra = 2.0f * casterHeight / std::max(sun.y, minRaySine);

	cascadeCount = std::clamp(cascadeCount, 1, FrameUniforms::maxShadowCascades);
	float start = nearDepth;
	for (int c = 0; c < cascadeCount; c++) {
		const float t = static_cast<float>(c + 1) / static_cast<float>(cascadeCount);
		const float logarithmic = nearDepth * std::pow(farDepth / nearDepth, t);
		const float linear = nearDepth + (farDepth - nearDepth) * t;
		const float end = c == cascadeCount - 1 ? farDepth : splitLambda * logarithmic + (1.0f - splitLambda) * linear;
		cascades[c] = Fit(cameraWorld, tanHalf, start, end, sun, extra);
		start = end;
	}
	active = true;
	return true;
}

FrameUniforms::Block ShadowMap::CascadeBlock(int cascade, Camera& camera, const LightSource& light) const
{
	FrameUniforms::Block block = FrameUniforms::Make(cascades[cascade].view, cascades[cascade].projection, camera.GetPosition(), light, false, 0.0f, 1.0f);
	block.depthPass = 1;
	return block;
}

void ShadowMap::BeginCascade(int cascade)
{
	if (!sceneSaved) {
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &sceneFramebuffer);
		glGetIntegerv(GL_VIEWPORT, sceneViewport);
		sceneSaved = true;
		//The map is the render target now, so it must not stay bound for sampling
		glBindTextureUnit(FrameUniforms::shadowUnit, 0);
	}
	glNamedFramebufferTextureLayer(framebuffer, GL_DEPTH_ATTACHMENT, texture, 0, cascade);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, resolution, resolution);
	glDepthMask(GL_TRUE);
	glClear(GL_DEPTH_BUFFER_BIT);
	//Casters nearer to the sun than the box are flattened onto its nearDepth plane instead of clipped
	glEnable(GL_DEPTH_CLAMP);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1.5f, 2.0f);
}

void ShadowMap::Finish()
{
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_DEPTH_CLAMP);
	if (sceneSaved) {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(sceneFramebuffer));
		glViewport(sceneViewport[0], sceneViewport[1], sceneViewport[2], sceneViewport[3]);
		sceneSaved = false;
	}
	if (active) {
		glBindTextureUnit(FrameUniforms::shadowUnit, texture);
	}
}

void ShadowMap::Apply(FrameUniforms::Block& block) const
{
	if (!active) {
		return;
	}
	block.shadowCascades = cascadeCount;
	block.shadowStrength = strength;
	for (int c = 0; c < cascadeCount; c++) {
		//Clip space [-1, 1] -> texture space [0, 1]
		const glm::mat4 bias = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
		block.shadowMatrices[c] = bias * cascades[c].viewProjection;
		block.shadowSplits[c] = cascades[c].farDepth;
		block.shadowTexels[c] = cascades[c].texel;
	}
}

void ShadowMap::ImGuiDraw()
{
	ImGui::SeparatorText("Shadows");
	ImGui::Checkbox("Sun shadows", &enabled);
	ImGui::SetItemTooltip("Shadows of the terrain and the plants in the 3D view of infinite generation");
	if (!enabled) {
		return;
	}
	ImGui::SliderFloat("Shadow distance", &distanceMetres, 50.0f, 2000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip("Shadows fade out at this distance; a shorter one makes them sharper");
	ImGui::SliderInt("Cascades", &cascadeCount, 1, FrameUniforms::maxShadowCascades);
	ImGui::SetItemTooltip("Shadow maps of growing size from the camera on: more cascades = sharper nearDepth shadows, more drawing");
	static const int resolutions[] = { 1024, 2048, 4096 };
	static const char* resolutionNames[] = { "1024", "2048", "4096" };
	int current = 0;
	for (int i = 0; i < 3; i++) {
		if (resolutions[i] == resolution) {
			current = i;
		}
	}
	if (ImGui::Combo("Shadow resolution", &current, resolutionNames, 3)) {
		resolution = resolutions[current];
	}
	ImGui::SliderFloat("Shadow strength", &strength, 0.0f, 1.0f, "%.2f");
	ImGui::SetItemTooltip("Share of the sunlight a shadow takes away (the sky light stays)");
	ImGui::SliderFloat("Cascade split", &splitLambda, 0.0f, 1.0f, "%.2f");
	ImGui::SetItemTooltip("0 = cascades of equal depth, 1 = each cascade a fixed multiple of the one before (sharper close up)");
}

bool ShadowMap::Render(Renderer& renderer, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, ChunkWorld& chunks,
	VegetationLayer& vegetation, VegetationModels* models)
{
	if (!Prepare(camera, light, settings)) {
		return false;
	}
	for (int c = 0; c < cascadeCount; c++) {
		BeginCascade(c);
		const FrameUniforms::Block block = CascadeBlock(c, camera, light);
		chunks.DrawShadow(renderer, block, cascades[c].viewProjection, settings);
		//Culled per cascade: the plants of the whole shadow range in every cascade cost several times more
		if (models && vegetation.CullShadows(camera, cascades[c].viewProjection, settings, models)) {
			vegetation.DrawShadow(block, camera, settings, *models);
		}
	}
	Finish();
	return true;
}
