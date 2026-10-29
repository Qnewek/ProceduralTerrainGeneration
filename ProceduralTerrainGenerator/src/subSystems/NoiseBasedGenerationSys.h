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
		//Map size, noise and erosion parameters edited by the UI; generators only read them
		NoiseMapConfig config;

		bool erosionDraw = false, instantUpdate = true, infiniteGeneration = false;
		double lastGenerationMs = 0.0;
		glm::vec3 oldCamPos = glm::vec3(0.0f, 0.0f, 0.0f);

		//Rendering
		TerrainView view;
		TerrainRenderSettings renderSettings;

		//Generators (hold results only)
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
		void ReloadShaders() { view.ReloadShader(); }
		void ImGuiRightPanel();
		void ImGuiLeftPanel();
		void ErosionImGui();
		void ImGuiOutput();
};

