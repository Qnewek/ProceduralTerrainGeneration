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
#include "TerrainMaterials.h"
#include "PanelTabs.h"

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
	//Textures of the terrain materials, shared by all modes; released before the context like metrics
	std::unique_ptr<TerrainMaterials> materials;
	//Tabs of the left panel (view, camera, sun, files); the right panel's tabs belong to the mode's subsystem
	PanelTabs viewPanelTabs;

	//F5 state in the previous frame, so a held key reloads shaders once
	bool reloadKeyWasDown = false;

	//Map Size variables
	int width, height;
	//World units of height 1: heightScale for one map (Noise heightmap, Imported map), worldHeightScale for TerrainGen
	float heightScale;
	static constexpr float worldHeightScale = 1024.0f;
	
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