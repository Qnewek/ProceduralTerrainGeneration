#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Renderer.h"
#include "Camera.h"
#include "Noise.h"
#include "Erosion.h"
#include "LightSource.h"
#include "TerrainGenerator.h"

#include "NoiseBasedGenerating.h"

class TerrainGenApp
{
public:
	TerrainGenApp();
	~TerrainGenApp();

	int Initialize();
	void Start();

private:
	void Draw();
	void ImGuiRender();
	
private:
	//SubSystems
	NoiseBasedGenerating noiseGen;

	//Time variables
	float deltaTime, lastFrame;

	//Map Size variables
	int width, height;
	float heightScale;
	
	//Window Layout variables
	int windowWidth, windowHeight;
	float rightPanelWidth, topPanelHeight, bottomPanelHeight, leftPanelWidth;
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