#pragma once

#include "Test.h"
#include "Player.h"

#include "VertexBuffer.h"
#include "VertexBufferLayout.h"
#include "Texture.h"
#include "LightSource.h"
#include "Noise.h"
#include "TerrainGenerator.h"

namespace test {

	class TestMapGen : public Test
	{
	public:
		TestMapGen();
		~TestMapGen();

		void OnUpdate(float deltaTime) override;
		void OnRender(GLFWwindow& window, Renderer& renderer) override;
		void OnImGuiRender() override;

		void basicTerrainGeneration();
		void conditionalTerrainGeneration();

	private:
		//OpenGL variables
		std::unique_ptr<VertexArray> m_MainVAO;

		std::unique_ptr<VertexBuffer> m_MainVertexBuffer;

		std::unique_ptr<IndexBuffer> m_MainIndexBuffer;

		std::unique_ptr<Shader> m_MainShader;

		std::unique_ptr<Texture> m_MainTexture;

		//Objects
		Player m_Player;
		LightSource m_LightSource;
		noise::SimplexNoiseClass noise;
		TerrainGenerator terrainGen;

		//Variables
		//Map size in chunks
		unsigned int m_Width, m_Height;

		//Chunk resolution
		int m_ChunkResX, m_ChunkResY;
		float m_ChunkScale;

		//Time variables
		float deltaTime, lastFrame;

		//Layout
		VertexBufferLayout m_Layout;
		unsigned int m_Stride;

		//Settings
		float* m_MeshVertices;
		unsigned int* m_MeshIndices;


	};

}