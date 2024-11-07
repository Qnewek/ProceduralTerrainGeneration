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
#include "Player.h"

#include <memory>

namespace test
{
	class TestNoiseMesh : public Test
	{
	public:
		TestNoiseMesh();
		~TestNoiseMesh();

		//Main functions
		void OnUpdate(float deltaTime) override;
		void OnRender(GLFWwindow& window, Renderer& renderer) override;
		void OnImGuiRender() override;

		//Auxiliary functions
		void CheckChange();
		void UpdatePrevCheckers();
		void DrawAdjacent(Renderer& renderer, glm::mat4& model);
		void PaintMesh(float* map, float* vertices);
		void DeactivateErosion();

		//Erosion functions
		void ErosionWindowRender();
		void PerformErosion();
		void PrintTrack(glm::mat4& model);

	private:
		//Vertices and indices arrays
		float* meshVertices, *erosionVertices, *traceVertices;
		unsigned int* meshIndices;
		
		//Mesh variables
		unsigned int width, height, stride;
		float m_Scaling_Factor;
		int seed;

		//Time variables
		float deltaTime, lastFrame;

		//Settings booleans
		bool testSymmetrical;

		//Erosion booleans
		bool erosionWindow, trackDraw, erosionDraw;

		//Entities
		Camera camera;
		Player player;
		LightSource lightSource;
		noise::SimplexNoiseClass noise;
		erosion::Erosion erosion;
		VertexBufferLayout layout;

		//OpenGL stuff
		std::unique_ptr<VertexArray> m_VAO;
		std::unique_ptr<VertexArray> m_TrackVAO;

		std::unique_ptr<VertexBuffer> m_VertexBuffer;
		std::unique_ptr<VertexBuffer> m_erosionBuffer;
		std::unique_ptr<VertexBuffer> m_TrackBuffer;

		std::unique_ptr<IndexBuffer> m_IndexBuffer;

		std::unique_ptr<Shader> m_Shader;
		std::unique_ptr<Shader> m_TrackShader;

		std::unique_ptr<Texture> m_Texture;

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

		enum color {
			MONO,
			PSEUDO_BIOME
		} meshColor;
	};
}