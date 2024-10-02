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
#include "Erosion.h"

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
		void ErosionWindowRender();

		void DrawAdjacent(Renderer& renderer, glm::mat4& model);
		void CheckChange();
		void PerformErosion();
		void UpdatePrevCheckers();
		void PrintTrack(glm::mat4& model);

	private:
		//Perlin Noise generation parameters
		float* meshVertices;
		unsigned int* meshIndices;
		unsigned int width, height, stride;
		int seed;

		float* traceVertices;

		float deltaTime;
		float lastFrame;

		bool erosionWindow;
		bool testSymmetrical;
		bool erosionPerform;
		bool trackDraw;

		Camera camera;
		LightSource lightSource;
		noise::SimplexNoiseClass noise;
		noise::SimplexNoiseClass biomeNoise;
		erosion::Erosion erosion;

		//OpenGL stuff
		std::unique_ptr<VertexArray> m_VAO;
		std::unique_ptr < IndexBuffer> m_IndexBuffer;
		std::unique_ptr < Shader> m_Shader;
		std::unique_ptr < Texture> m_Texture;
		std::unique_ptr < VertexBuffer> m_VertexBuffer;

		std::unique_ptr < VertexBuffer> m_TrackBuffer;
		std::unique_ptr < Shader> m_TrackShader;
		std::unique_ptr < VertexArray> m_TrackVAO;

		struct prevCheckers {
			noise::Options prevOpt;
			noise::IslandType prevIslandType;
			float prevCheckSum;
			bool prevRidge;
			bool prevIsland;
			bool symmetrical;
			int seed = 0;

			prevCheckers(noise::Options prevOpt = noise::Options::REVERT_NEGATIVES, noise::IslandType prevIslandType = noise::IslandType::CONE, float prevCheckSum = 0, bool prevRidge = false, bool prevIsland = false, bool symmetrical = false, int seed = 0)
				: prevOpt(prevOpt), prevCheckSum(prevCheckSum), prevRidge(prevRidge), prevIsland(prevIsland), prevIslandType(prevIslandType), seed(seed), symmetrical(symmetrical){}
		} prevCheck;
	};
}