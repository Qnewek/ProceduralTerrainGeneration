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
		float* vertices, *erosionVertices;
		float heightScale, modelScale, topoStep, topoBandWidth;
		unsigned int* meshIndices;
		unsigned int stride;
		int width, height, seed;
		bool wireFrame = false, erosionDraw = false, instantUpdate = true;
		utilities::heightMapMode displayMode = utilities::heightMapMode::GREYSCALE;
		
		//OpenGl objects
		VertexBufferLayout layout;
		std::unique_ptr<VertexArray> mainVAO;
		std::unique_ptr<VertexArray> erosionVAO;
		std::unique_ptr<VertexBuffer> mainVertexBuffer;
		std::unique_ptr<VertexBuffer> erosionVertexBuffer;
		std::unique_ptr<IndexBuffer> mainIndexBuffer;
		std::unique_ptr<Shader> mainShader;

		//Perlin Noise object
		noise::SimplexNoiseClass noise;
		erosion::Erosion erosion;
	public:
		NoiseBasedGenerationSys();
		~NoiseBasedGenerationSys();
		
		bool Initialize(int _height, int _width, float _heightScale);
		bool GenerateNoise();
		bool SimulateErosion();

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		void ImGuiDraw();
		void ErosionImGui();
		void ImGuiOutput();
};

