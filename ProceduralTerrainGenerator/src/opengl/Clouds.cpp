#include "Clouds.h"

#include <iostream>

#include "Camera.h"
#include "LightSource.h"
#include "Shader.h"

namespace {
	constexpr int shapeSize = 128, detailSize = 32, weatherSize = 512;
	constexpr int shapeNoiseUnit = 11, detailNoiseUnit = 12, depthUnit = 13;

	GLuint MakeTexture(GLenum target, GLenum format, int size, GLint wrap)
	{
		GLuint texture = 0;
		glCreateTextures(target, 1, &texture);
		if (target == GL_TEXTURE_3D) {
			glTextureStorage3D(texture, 1, format, size, size, size);
		}
		else {
			glTextureStorage2D(texture, 1, format, size, size);
		}
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_R, wrap);
		return texture;
	}

	//Texture of the size of the cloud image, sampled linearly by the composite and the reprojection
	GLuint MakeImage(GLenum format, const glm::ivec2& size)
	{
		GLuint texture = 0;
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);
		glTextureStorage2D(texture, 1, format, size.x, size.y);
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return texture;
	}
}

Clouds::~Clouds()
{
	const GLuint textures[] = { shape, detail, weather, depthCopy };
	glDeleteTextures(4, textures);
	Release(sky);
	Release(over);
	glDeleteVertexArrays(1, &vao);
}

void Clouds::Release(History& history)
{
	const GLuint textures[] = { history.raw, history.distance, history.color[0], history.color[1] };
	glDeleteTextures(4, textures);
	history = History();
}

void Clouds::Allocate(History& history, const glm::ivec2& size)
{
	Release(history);
	history.size = size;
	history.raw = MakeImage(GL_RGBA16F, size);
	history.distance = MakeImage(GL_R32F, size);
	history.color[0] = MakeImage(GL_RGBA16F, size);
	history.color[1] = MakeImage(GL_RGBA16F, size);
}

bool Clouds::Initialize()
{
	noiseShader = Shader::Compute("res/shaders/CloudShaders/cloud_noise_compute.shader");
	marchShader = Shader::Compute("res/shaders/CloudShaders/cloud_compute.shader");
	resolveShader = Shader::Compute("res/shaders/CloudShaders/cloud_resolve_compute.shader");
	compositeShader = std::make_unique<Shader>("res/shaders/sky_vertex.shader", "res/shaders/CloudShaders/cloud_composite_fragment.shader");
	if (!noiseShader || !noiseShader->IsValid() || !marchShader || !marchShader->IsValid() || !resolveShader || !resolveShader->IsValid()
		|| !compositeShader->IsValid()) {
		std::cout << "[ERROR] Clouds: the shaders could not be built" << std::endl;
		return false;
	}
	glCreateVertexArrays(1, &vao);
	return MakeNoise();
}

bool Clouds::ReloadShader()
{
	const bool noise = noiseShader->Reload();
	const bool ok = noise && marchShader->Reload() && resolveShader->Reload() && compositeShader->Reload();
	//The noise may have changed
	return ok && MakeNoise();
}

bool Clouds::MakeNoise()
{
	if (!shape) {
		shape = MakeTexture(GL_TEXTURE_3D, GL_RGBA8, shapeSize, GL_REPEAT);
		detail = MakeTexture(GL_TEXTURE_3D, GL_RGBA8, detailSize, GL_REPEAT);
		weather = MakeTexture(GL_TEXTURE_2D, GL_RGBA8, weatherSize, GL_REPEAT);
	}
	noiseShader->Bind();
	constexpr int local = 4;
	glBindImageTexture(0, shape, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	noiseShader->SetUniform1i("target", 0);
	glDispatchCompute(shapeSize / local, shapeSize / local, shapeSize / local);
	glBindImageTexture(0, detail, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	noiseShader->SetUniform1i("target", 1);
	glDispatchCompute(detailSize / local, detailSize / local, detailSize / local);
	glBindImageTexture(1, weather, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	noiseShader->SetUniform1i("target", 2);
	glDispatchCompute(weatherSize / local, weatherSize / local, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
	return true;
}

void Clouds::March(Camera& camera, const LightSource& light, bool limitByDepth, const glm::ivec4& viewport, History& history)
{
	const glm::ivec2 size((viewport.z + 1) / 2, (viewport.w + 1) / 2);
	if (size != history.size) {
		Allocate(history, size);
	}
	const FrameUniforms::Block block = FrameUniforms::Make(camera, light);
	frameUniforms.Upload(block);
	const glm::mat4 viewProjection = block.projection * block.view;
	const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
	marchShader->Bind();
	marchShader->SetUniformMat4f("inverseViewProjection", inverseViewProjection);
	marchShader->SetUniformMat4f("inverseProjection", glm::inverse(block.projection));
	marchShader->SetUniform1i("limitByDepth", limitByDepth ? 1 : 0);
	marchShader->SetUniform1i("frameIndex", frameIndex);
	glBindTextureUnit(shapeNoiseUnit, shape);
	glBindTextureUnit(detailNoiseUnit, detail);
	glBindTextureUnit(weatherUnit, weather);
	if (limitByDepth) {
		glBindTextureUnit(depthUnit, depthCopy);
	}
	glBindImageTexture(0, history.raw, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glBindImageTexture(1, history.distance, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
	constexpr int local = 8;
	const glm::ivec2 groups((size.x + local - 1) / local, (size.y + local - 1) / local);
	glDispatchCompute(groups.x, groups.y, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	GLuint result = history.raw;
	if (light.IsCloudSmoothingOn()) {
		const int next = 1 - history.current;
		resolveShader->Bind();
		resolveShader->SetUniformMat4f("inverseViewProjection", inverseViewProjection);
		resolveShader->SetUniformMat4f("previousViewProjection", history.previousViewProjection);
		resolveShader->SetUniform3fv("cameraPosition", block.viewPos);
		resolveShader->SetUniform1f("unitsPerMetre", light.GetCloudWind().z);
		//Only right after the previous frame of the same march (not after a gap, a resize or smoothing turned off)
		resolveShader->SetUniform1i("historyValid", history.lastFrame >= 0 && history.lastFrame == frameIndex - 1 ? 1 : 0);
		glBindImageTexture(0, history.color[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glBindImageTexture(1, history.raw, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, history.distance, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		glBindTextureUnit(shapeNoiseUnit, history.color[history.current]);
		glDispatchCompute(groups.x, groups.y, 1);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
		history.current = next;
		history.lastFrame = frameIndex;
		result = history.color[next];
	}
	else {
		history.lastFrame = -1;
	}
	history.previousViewProjection = viewProjection;
	glBindTextureUnit(imageUnit, result);
}

bool Clouds::RenderSky(Camera& camera, const LightSource& light)
{
	if (!marchShader || !light.AreCloudsOn()) {
		return false;
	}
	frameIndex++;
	glm::ivec4 viewport;
	glGetIntegerv(GL_VIEWPORT, &viewport.x);
	if (viewport.z <= 0 || viewport.w <= 0) {
		return false;
	}
	March(camera, light, false, viewport, sky);
	return true;
}

void Clouds::DrawOver(Camera& camera, const LightSource& light)
{
	if (!marchShader || !light.AreCloudsOn() || camera.GetPosition().y / light.GetCloudWind().z < light.GetCloudLayer().x) {
		return;
	}
	glm::ivec4 viewport;
	glGetIntegerv(GL_VIEWPORT, &viewport.x);
	if (viewport.z <= 0 || viewport.w <= 0) {
		return;
	}
	if (depthSize != glm::ivec2(viewport.z, viewport.w)) {
		glDeleteTextures(1, &depthCopy);
		depthSize = glm::ivec2(viewport.z, viewport.w);
		glCreateTextures(GL_TEXTURE_2D, 1, &depthCopy);
		glTextureStorage2D(depthCopy, 1, GL_DEPTH_COMPONENT24, depthSize.x, depthSize.y);
		glTextureParameteri(depthCopy, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(depthCopy, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(depthCopy, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(depthCopy, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glCopyTextureSubImage2D(depthCopy, 0, 0, 0, viewport.x, viewport.y, viewport.z, viewport.w);
	March(camera, light, true, viewport, over);

	//scene * transmittance + light, only over the scene (the sky has its clouds already)
	compositeShader->Bind();
	glBindVertexArray(vao);
	const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_SRC_ALPHA);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (!blend) {
		glDisable(GL_BLEND);
	}
	if (depthTest) {
		glEnable(GL_DEPTH_TEST);
	}
	glDepthMask(GL_TRUE);
}
