#pragma once

#include "Test.h"
#include "VertexBuffer.h"
#include "VertexBufferLayout.h"
#include "Texture.h"
#include "Noise.h"

#include <memory>

namespace test
{
	class TestNoiseMesh : public Test
	{
	public:
		TestNoiseMesh();
		~TestNoiseMesh();

		void OnUpdate(float deltaTime) override;
		void OnRender() override;
		void OnImGuiRender() override;

	private:
		float* mesh;
		unsigned int* meshIndices;
		int width, height, octaves;
		float scale, constrast, checkSum = 0;
		noise::Options option, prevOpt;
		std::unique_ptr<VertexArray> m_VAO;
		std::unique_ptr < IndexBuffer> m_IndexBuffer;
		std::unique_ptr < Shader> m_Shader;
		std::unique_ptr < Texture> m_Texture;
		std::unique_ptr < VertexBuffer> m_VertexBuffer;
	};
}