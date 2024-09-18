#pragma once

#include "Test.h"
#include "VertexBuffer.h"
#include "VertexBufferLayout.h"
#include "Texture.h"
#include "Noise.h"
#include "Camera.h"
#include "LightSource.h"
#include "Noise.h"
#include "glm/glm.hpp"

#include <memory>

namespace test
{
	class TestNoiseMesh : public Test
	{
	public:
		TestNoiseMesh();
		~TestNoiseMesh();

		void OnUpdate(float deltaTime) override;
		void OnRender(GLFWwindow& window, Renderer& renderer) override;
		void OnImGuiRender() override;

	private:
		//Perlin Noise generation parameters
		float* mesh, *map, *textureSeed;
		unsigned int* meshIndices;
		int width, height, octaves;
		float scale, constrast, checkSum = 0;
		float redistribution, ridgeGain, ridgeOffset, lacunarity, persistance;
		noise::Options option, prevOpt;

		float deltaTime;
		float lastFrame;
		Camera camera;
		LightSource lightSource;
		noise::SimplexNoiseClass noise;

		//OpenGL stuff
		std::unique_ptr<VertexArray> m_VAO;
		std::unique_ptr < IndexBuffer> m_IndexBuffer;
		std::unique_ptr < Shader> m_Shader;
		std::unique_ptr < Texture> m_Texture;
		std::unique_ptr < VertexBuffer> m_VertexBuffer;
	};
}