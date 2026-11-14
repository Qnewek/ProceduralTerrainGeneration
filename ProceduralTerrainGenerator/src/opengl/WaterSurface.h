#pragma once

#include <memory>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "FrameUniforms.h"

class Camera;
class ChunkWorld;
class LightSource;
class Shader;
struct TerrainRenderSettings;

//Water surface of the infinite world at the water level: one quad per ground chunk in range, blended over the terrain after it is drawn
//(depth test on, no depth writes). The depth of the water comes from the height map of the chunk, so nothing else is generated for it.
//In the 2D map it is seen from straight above. Only for the thread owning the GL context
class WaterSurface
{
public:
	WaterSurface();
	~WaterSurface();
	WaterSurface(const WaterSurface&) = delete;
	WaterSurface& operator=(const WaterSurface&) = delete;

	//@param waterLevel - height of the surface in world units (TerrainConfig::WaterHeight() * heightScale)
	void Draw(const ChunkWorld& chunks, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, float waterLevel);
	//Section "Water" of the Display tab
	void ImGuiDraw();
	bool ReloadShader();

private:
	bool enabled = true;
	float clarity = 3.0f;		//metres
	float waveStrength = 1.0f;
	glm::vec3 shallowColor = glm::vec3(0.10f, 0.42f, 0.42f);
	glm::vec3 deepColor = glm::vec3(0.02f, 0.09f, 0.16f);

	std::unique_ptr<Shader> shader;
	GLuint vao = 0;
	FrameUniforms frameUniforms;
	bool failed = false;
};
