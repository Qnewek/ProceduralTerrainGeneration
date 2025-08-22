#pragma once
#include "Noise.h"
#include "Camera.h"

#include "VertexBufferLayout.h"

class NoiseBasedGenerating
{
	private:
		//Config
		float* vertices;
		float heightScale;
		unsigned int* meshIndices;
		unsigned int stride;
		int width, height, seed;
		bool wireFrame;
		
		//OpenGl objects
		VertexBufferLayout layout;
		std::unique_ptr<VertexArray> mainVAO;
		std::unique_ptr<VertexBuffer> mainVertexBuffer;
		std::unique_ptr<IndexBuffer> mainIndexBuffer;
		std::unique_ptr<Shader> mainShader;

		//Perlin Noise object
		noise::SimplexNoiseClass noise;
	public:
		NoiseBasedGenerating();
		~NoiseBasedGenerating();
		
		bool Initialize(int height, int width, int seed, float heightScale);
		bool Resize(int height, int width);
		void Draw(glm::mat4& model, Renderer& renderer, Camera& camera);
};

