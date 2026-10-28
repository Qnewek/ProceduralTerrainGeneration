#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Renderer.h"
#include "Camera.h"
#include "Noise.h"
#include "Erosion.h"
#include "LightSource.h"
#include "TerrainGenerator.h"

#include "NoiseBasedGenerationSys.h"
#include "TerrainGenerationSys.h"

class TerrainGenApp
{
public:
	TerrainGenApp();
	~TerrainGenApp();

	bool Initialize();
	void Start();

private:
	void Draw();
	void ImGuiRender();
	
private:
	//SubSystems
	NoiseBasedGenerationSys noiseGenSys;
	TerrainGenerationSys terrainGenSys;

	//Time variables
	float deltaTime, lastFrame;

	//Map Size variables
	int width, height;
	float heightScale;
	
	//Window Layout variables
	int windowWidth, windowHeight;
	float rightPanelWidth, topPanelHeight, bottomPanelHeight, leftPanelWidth;

	//Size of the 3D viewport between the ImGui panels, in pixels
	int ViewportWidth() const { return static_cast<int>(windowWidth - rightPanelWidth - leftPanelWidth); }
	int ViewportHeight() const { return static_cast<int>(windowHeight - topPanelHeight - bottomPanelHeight); }
	GLFWwindow* window;
	Renderer renderer;
	Camera camera;
	LightSource light;

	enum class mode
	{
		NOISE_HEIGHTMAP,
		TERRAIN_GEN
	} currentMode;
};