#pragma once

#include <memory>
#include <vector>

#include <GL/glew.h>
#include "glm/glm.hpp"

#include "FrameUniforms.h"

class Camera;
class ChunkWorld;
class LightSource;
class Shader;
class TextureClass;
struct TerrainRenderSettings;

//Water surface at the water level: one quad per ground chunk in range (infinite world) or one over the finished map, drawn after
//everything else with depth test on and no depth writes. The depth of the water comes from the height map under it (color, foam);
//the color and depth of the scene drawn before it are copied first: the ground under the water is that copy shifted by the waves
//(refraction), dimmed by the water along the view ray, and the reflection is traced through the copy (screen-space reflections),
//falling back to the sky where the ray leaves the screen or hits nothing. In the 2D map it is seen from straight above.
//Only for the thread owning the GL context
class WaterSurface
{
public:
	WaterSurface();
	~WaterSurface();
	WaterSurface(const WaterSurface&) = delete;
	WaterSurface& operator=(const WaterSurface&) = delete;

	//Infinite world
	//@param waterLevel - height of the surface in world units (TerrainConfig::WaterHeight() * heightScale)
	void Draw(const ChunkWorld& chunks, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, float waterLevel);
	//A finished map of size (width, height) world units centred on the origin (TerrainView), height map over all of it
	//@param flatMin, flatMax - area framed by the 2D map (TerrainView::GetFlatViewMin/Max)
	void DrawMap(const TextureClass& heightMap, glm::vec2 size, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings,
		float waterLevel, glm::vec2 flatMin, glm::vec2 flatMax);
	//Section "Water" of the Display tab
	void ImGuiDraw();
	bool ReloadShader();

private:
	struct Quad {
		const TextureClass* height = nullptr;
		glm::vec2 corner = glm::vec2(0.0f);		//world XZ before modelScale
		glm::vec2 size = glm::vec2(0.0f);
		glm::vec2 uvMin = glm::vec2(0.0f), uvMax = glm::vec2(1.0f);
	};
	void DrawQuads(const std::vector<Quad>& list, Camera& camera, const LightSource& light, const TerrainRenderSettings& settings, float waterLevel,
		glm::vec2 flatMin, glm::vec2 flatMax);
	//Copies the color and depth of the scene in the viewport to sceneColor / sceneDepth; false if there is no viewport
	bool CopyScene(glm::ivec4& viewport);

	bool enabled = true;
	bool reflections = true;
	float clarity = 3.0f;		//metres
	float waveStrength = 1.0f;
	float refraction = 1.0f;
	glm::vec3 shallowColor = glm::vec3(0.10f, 0.42f, 0.42f);
	glm::vec3 deepColor = glm::vec3(0.02f, 0.09f, 0.16f);

	std::unique_ptr<Shader> shader;
	GLuint vao = 0;
	GLuint sceneColor = 0, sceneDepth = 0;
	glm::ivec2 sceneSize = glm::ivec2(0);
	FrameUniforms frameUniforms;
	bool failed = false;
	std::vector<Quad> quads;
};
