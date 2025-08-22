#pragma once
#include "Noise.h"
#include "Erosion.h"
#include "Camera.h"

#include "VertexBufferLayout.h"

class NoiseBasedGenerating
{
	private:
		//Config
		float* vertices, *erosionVertices;
		float heightScale;
		unsigned int* meshIndices;
		unsigned int stride;
		int width, height, seed;
		bool wireFrame = false, erosionDraw = false, newSize = true, instantUpdate = true;
		
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
		NoiseBasedGenerating();
		~NoiseBasedGenerating();
		
		bool Initialize(int height, int width, float heightScale);
		bool Resize();
		bool GenerateNoise();
		bool SimulateErosion();

		void Draw(glm::mat4& model, Renderer& renderer, Camera& camera);
		void ImGuiDraw();
		void ErosionImGui();
		void ImGuiOutput();
};

