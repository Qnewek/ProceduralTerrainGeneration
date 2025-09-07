#pragma once
#include "Noise.h"
#include "Erosion.h"
#include "Camera.h"
#include "utilities.h"
#include "LightSource.h"

#include "VertexBufferLayout.h"

class NoiseBasedGenerationSys
{
	private:
		//Config
		float* vertices;
		float heightScale, modelScale, topoStep, topoBandWidth;
		unsigned int stride, mapResolution;
		int width, height;
		bool wireFrame = false, erosionDraw = false, instantUpdate = true, map2d = false;
		utilities::heightMapMode displayMode = utilities::heightMapMode::GREYSCALE;
		
		//OpenGl objects
		VertexBufferLayout layout;
		std::unique_ptr<VertexArray> mainVAO;
		std::unique_ptr<Shader> mainShader;
		std::unique_ptr<VertexBuffer> mainVertexBuffer;
		std::unique_ptr<TextureClass> terrainTexture;
		std::unique_ptr<TextureClass> erosionTexture;

		//Perlin Noise object
		noise::SimplexNoiseClass noise;
		erosion::Erosion erosion;
	public:
		NoiseBasedGenerationSys();
		~NoiseBasedGenerationSys();
		
		bool Initialize(int _height, int _width, float _heightScale);
		bool Resize();
		bool GenerateNoise();
		bool SimulateErosion();

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		void ImGuiRightPanel();
		void ImGuiLeftPanel();
		void ErosionImGui();
		void ImGuiOutput();
};

