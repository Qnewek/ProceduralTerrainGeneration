#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <memory>

#include "FrameMetrics.h"
#include "Renderer.h"
#include "Camera.h"
#include "Noise.h"
#include "Erosion.h"
#include "LightSource.h"
#include "Sky.h"
#include "TerrainGenerator.h"

#include "NoiseBasedGenerationSys.h"
#include "TerrainGenerationSys.h"
#include "MapImportSys.h"

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
	//Rebuilds all shaders from their source files (F5)
	void ReloadShaders();
	//Reads the current drawable area of the window; keeps the last size while minimized
	void UpdateWindowSize();

private:
	//SubSystems
	NoiseBasedGenerationSys noiseGenSys;
	TerrainGenerationSys terrainGenSys;
	MapImportSys mapImportSys;

	//Time variables
	float deltaTime;
	double lastFrame;

	//Performance panel data; holds OpenGL objects, so it is released before the context
	std::unique_ptr<FrameMetrics> metrics;
	bool vsync = true;

	//F5 state in the previous frame, so a held key reloads shaders once
	bool reloadKeyWasDown = false;

	//Map Size variables
	int width, height;
	float heightScale;
	
	//Window Layout variables
	int windowWidth, windowHeight;
	float rightPanelWidth, topPanelHeight, bottomPanelHeight, leftPanelWidth;

	//Size of the 3D viewport between the ImGui panels, in pixels
	int ViewportWidth() const { return std::max(1, static_cast<int>(windowWidth - rightPanelWidth - leftPanelWidth)); }
	int ViewportHeight() const { return std::max(1, static_cast<int>(windowHeight - topPanelHeight - bottomPanelHeight)); }
	GLFWwindow* window;
	Renderer renderer;
	Camera camera;
	//The sun
	LightSource light;
	Sky sky;
	//true if the active mode shows the 2D map, which has no sky
	bool FlatView() const;

	enum class mode
	{
		NOISE_HEIGHTMAP,
		TERRAIN_GEN,
		MAP_IMPORT
	} currentMode;
};