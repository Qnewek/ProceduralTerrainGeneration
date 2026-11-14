#include "Sky.h"

#include <algorithm>
#include <iostream>

namespace {
	//Must match common/atmosphere_model.glsl
	constexpr int transmittanceWidth = 256, transmittanceHeight = 64;
	constexpr int skyViewWidth = 192, skyViewRows = 109;		//SKY_VIEW_SIZE + the row of light colors
	constexpr float groundRadius = 6360.0f;						//km
	constexpr int localSize = 8;
	//World units per metre of the camera height (vegetation::unitsPerMetre in TerrainGen)
	constexpr float unitsPerMetre = 4.0f;

	GLuint MakeTable(int width, int height)
	{
		GLuint texture = 0;
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);
		glTextureStorage2D(texture, 1, GL_RGBA16F, width, height);
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return texture;
	}
}

Sky::~Sky()
{
	glDeleteTextures(1, &transmittance);
	glDeleteTextures(1, &skyView);
}

bool Sky::Initialize()
{
	shader = std::make_unique<Shader>("res/shaders/sky_vertex.shader", "res/shaders/sky_fragment.shader");
	transmittanceShader = Shader::Compute("res/shaders/AtmosphereShaders/transmittance_compute.shader");
	skyViewShader = Shader::Compute("res/shaders/AtmosphereShaders/skyview_compute.shader");
	if (!shader->IsValid() || !transmittanceShader || !transmittanceShader->IsValid() || !skyViewShader || !skyViewShader->IsValid()) {
		std::cout << "[ERROR] Sky: the shaders could not be built" << std::endl;
		return false;
	}
	vao = std::make_unique<VertexArray>();
	transmittance = MakeTable(transmittanceWidth, transmittanceHeight);
	skyView = MakeTable(skyViewWidth, skyViewRows);
	return true;
}

void Sky::ReloadShader()
{
	shader->Reload();
	transmittanceShader->Reload();
	skyViewShader->Reload();
	//The model of the atmosphere may have changed
	transmittanceReady = false;
}

void Sky::ComputeTransmittance()
{
	transmittanceShader->Bind();
	glBindImageTexture(0, transmittance, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((transmittanceWidth + localSize - 1) / localSize, (transmittanceHeight + localSize - 1) / localSize, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
	transmittanceReady = true;
}

void Sky::Update(const Camera& camera, const LightSource& sun)
{
	if (!skyViewShader) {
		return;
	}
	if (!transmittanceReady) {
		ComputeTransmittance();
	}
	//The sky barely changes with the camera height over the terrain, but a camera far up sees the haze below it
	const float heightKm = std::max(camera.GetPosition().y / unitsPerMetre / 1000.0f, 0.0f) + 0.002f;
	skyViewShader->Bind();
	skyViewShader->SetUniform1f("cameraRadius", groundRadius + heightKm);
	skyViewShader->SetUniform3fv("sunDirection", sun.GetDirection());
	glBindTextureUnit(skyViewUnit, transmittance);
	glBindImageTexture(0, skyView, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
	skyViewShader->SetUniform1i("pass", 0);
	glDispatchCompute((skyViewWidth + localSize - 1) / localSize, (skyViewRows + localSize - 1) / localSize, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	//The ambient light sums the sky of pass 0
	skyViewShader->SetUniform1i("pass", 1);
	glDispatchCompute(1, 1, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
	glBindTextureUnit(skyViewUnit, skyView);
}

void Sky::Draw(Camera& camera, const LightSource& sun, bool clouds)
{
	//Only the rotation of the camera matters for the sky
	const glm::mat4 rotation = glm::mat4(glm::mat3(*camera.GetViewMatrix()));
	shader->Bind();
	shader->SetUniformMat4f("inverseViewProjection", glm::inverse(*camera.GetProjectionMatrix() * rotation));
	shader->SetUniform3fv("sunDirection", sun.GetDirection());
	shader->SetUniform1i("cloudsOn", clouds ? 1 : 0);
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	shader->SetUniform4f("viewportRect", static_cast<float>(viewport[0]), static_cast<float>(viewport[1]),
		static_cast<float>(viewport[2]), static_cast<float>(viewport[3]));
	vao->Bind();
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glEnable(GL_DEPTH_TEST);
	shader->Unbind();
}
