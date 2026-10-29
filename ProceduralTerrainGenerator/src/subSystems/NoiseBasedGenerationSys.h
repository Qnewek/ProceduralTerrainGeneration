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
		std::vector<float> vertices;
		float heightScale, modelScale, topoStep, topoBandWidth;
		unsigned int stride, mapResolution;
		int width, height;
		bool wireFrame = false, erosionDraw = false, instantUpdate = true, map2d = false, infiniteGeneration = false;
		double lastGenerationMs = 0.0;
		utilities::heightMapMode displayMode = utilities::heightMapMode::GREYSCALE;
		glm::vec3 oldCamPos = glm::vec3(0.0f, 0.0f, 0.0f);
		
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
		
		bool Initialize(int _width, int _height, float _heightScale);
		bool Resize();
		void BuildMesh();
		bool GenerateNoise(float originx, float originy);
		bool SimulateErosion();

		void Draw(Renderer& renderer, Camera& camera, LightSource& light);
		void ImGuiRightPanel();
		void ImGuiLeftPanel();
		void ErosionImGui();
		void ImGuiOutput();
};

