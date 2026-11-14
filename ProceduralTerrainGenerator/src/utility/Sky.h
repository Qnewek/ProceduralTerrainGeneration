#pragma once

#include <memory>

#include <GL/glew.h>

#include "Camera.h"
#include "LightSource.h"
#include "Shader.h"
#include "VertexArray.h"

//The atmosphere and the background of the 3D view. Update computes the sky by scattering of the sunlight in the air (Rayleigh, Mie,
//ozone; res/shaders/AtmosphereShaders/) into the sky-view table on skyViewUnit, read by common/sky.glsl: the sky, the water
//reflections, the fog and the colors of the sunlight and of the ambient light of every lit shader. Draw paints the sky with the sun disc
//and stars, first, over the whole viewport and without depth; the 2D map view draws no sky but still needs Update (its lighting).
//Only for the thread owning the GL context
class Sky
{
public:
	//Texture unit of the sky-view table while drawing (layout(binding = 14) in common/sky.glsl); nothing else may use it
	static constexpr int skyViewUnit = 14;

	Sky() = default;
	~Sky();
	Sky(const Sky&) = delete;
	Sky& operator=(const Sky&) = delete;

	bool Initialize();
	void ReloadShader();
	//Computes the sky for the sun and the camera height; every frame, before anything is drawn
	void Update(const Camera& camera, const LightSource& sun);
	//@param clouds - the clouds of the sky are on Clouds::imageUnit (Clouds::RenderSky)
	void Draw(Camera& camera, const LightSource& sun, bool clouds = false);

private:
	void ComputeTransmittance();

	std::unique_ptr<Shader> shader;
	std::unique_ptr<Shader> transmittanceShader, skyViewShader;
	//Core profile needs a bound vertex array even without vertex data
	std::unique_ptr<VertexArray> vao;
	GLuint transmittance = 0, skyView = 0;
	bool transmittanceReady = false;
};
