#pragma once

#include <memory>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "FrameUniforms.h"

class Camera;
class LightSource;
class Shader;

//Volumetric clouds: a layer of clouds between two heights (LightSource: base, thickness, coverage, wind), ray marched in a compute shader
//at half the viewport resolution through tiling 3D noises (made once) and a weather map that says where clouds are. The weather map stays
//bound on weatherUnit, so the shadows of the clouds fall on everything lit (common/shadow.glsl -> clouds.glsl).
//Per frame: RenderSky before the sky (the sky composites the clouds over itself, so whatever is drawn later hides them, which is right
//while the camera is below the clouds: the terrain is lower than their base); DrawOver after the scene when the camera is at or above
//the base, with the rays ended at the copied depth of the scene. Only for the thread owning the GL context
class Clouds
{
public:
	//Texture units: the weather map, all the time (layout(binding = 16) in common/clouds.glsl; nothing else may use it), and the cloud
	//image while the sky or DrawOver composites it
	static constexpr int weatherUnit = 16;
	static constexpr int imageUnit = 13;

	Clouds() = default;
	~Clouds();
	Clouds(const Clouds&) = delete;
	Clouds& operator=(const Clouds&) = delete;

	bool Initialize();
	bool ReloadShader();
	//The clouds of the sky in the current viewport; true if there are any, then they are on imageUnit for Sky::Draw
	bool RenderSky(Camera& camera, const LightSource& light);
	//Over the drawn scene, only while the camera is at or above the cloud base
	void DrawOver(Camera& camera, const LightSource& light);

private:
	bool MakeNoise();
	//Marches the clouds into image for the current viewport
	void March(Camera& camera, const LightSource& light, bool limitByDepth, const glm::ivec4& viewport);

	std::unique_ptr<Shader> noiseShader, marchShader, compositeShader;
	GLuint vao = 0;
	GLuint shape = 0, detail = 0, weather = 0;
	GLuint image = 0, depthCopy = 0;
	glm::ivec2 imageSize = glm::ivec2(0), depthSize = glm::ivec2(0);
	FrameUniforms frameUniforms;
	int frameIndex = 0;
};
