#pragma once
#include "Noise.h"
#include "Erosion.h"
#include "Camera.h"
#include "utilities.h"
#include "LightSource.h"
#include "TerrainView.h"

class NoiseBasedGenerationSys
{
	private:
		//Config
		int width, height;
		bool erosionDraw = false, instantUpdate = true, infiniteGeneration = false;
		double lastGenerationMs = 0.0;
		glm::vec3 oldCamPos = glm::vec3(0.0f, 0.0f, 0.0f);

		//Rendering
		TerrainView view;
		TerrainRenderSettings renderSettings;

		//Perlin Noise object
		noise::SimplexNoiseClass noise;
		erosion::Erosion erosion;
	public:
		NoiseBasedGenerationSys();
		~NoiseBasedGenerationSys();
		
		bool Initialize(int _width, int _height, float _heightScale);
		bool Resize();
		bool GenerateNoise(float originx, float originy);
		bool SimulateErosion();

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		void ImGuiRightPanel();
		void ImGuiLeftPanel();
		void ErosionImGui();
		void ImGuiOutput();
};

