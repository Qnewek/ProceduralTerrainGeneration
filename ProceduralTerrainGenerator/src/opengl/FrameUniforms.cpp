#include "FrameUniforms.h"

#include <chrono>

#include "Camera.h"
#include "LightSource.h"

namespace {
	const auto programStart = std::chrono::steady_clock::now();
}

FrameUniforms::~FrameUniforms()
{
	if (buffer) {
		glDeleteBuffers(1, &buffer);
	}
}

FrameUniforms::Block FrameUniforms::Make(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& viewPos, const LightSource& light,
	bool fogOn, float fogStart, float fogDistance)
{
	Block b;
	b.view = view;
	b.projection = projection;
	b.viewPos = viewPos;
	b.time = std::chrono::duration<float>(std::chrono::steady_clock::now() - programStart).count();
	b.lightDirection = glm::vec4(light.GetDirection(), 0.0f);
	b.lightAmbient = glm::vec4(light.GetAmbient(), 0.0f);
	b.lightDiffuse = glm::vec4(light.GetDiffuse(), 0.0f);
	b.lightOn = light.IsLightOn() ? 1 : 0;
	b.fogOn = fogOn ? 1 : 0;
	b.fogStart = fogStart;
	b.fogDistance = fogDistance;
	if (fogOn) {
		b.hazeDensity = light.GetHazeDensity();
		b.hazeFalloff = light.GetHazeFalloff();
	}
	b.cloudLayer = light.GetCloudLayer();
	b.cloudWind = light.GetCloudWind();
	return b;
}

FrameUniforms::Block FrameUniforms::Make(Camera& camera, const LightSource& light)
{
	return Make(*camera.GetViewMatrix(), *camera.GetProjectionMatrix(), camera.GetPosition(), light, light.IsFogOn(),
		light.GetFogStart(camera.GetRenderDistance()), camera.GetRenderDistance());
}

void FrameUniforms::Upload(const Block& block)
{
	if (!buffer) {
		glCreateBuffers(1, &buffer);
		glNamedBufferStorage(buffer, sizeof(Block), nullptr, GL_DYNAMIC_STORAGE_BIT);
	}
	glNamedBufferSubData(buffer, 0, sizeof(Block), &block);
	glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
}
