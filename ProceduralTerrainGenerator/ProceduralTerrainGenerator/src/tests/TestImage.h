#pragma once

#include "Test.h"
#include "VertexBuffer.h"
#include "VertexBufferLayout.h"
#include "Texture.h"


namespace test
{
	class TestImage : public Test
	{
	public:
		TestImage();
		~TestImage();

		void OnUpdate(float deltaTime) override;
		void OnRender(GLFWwindow& window, Renderer& renderer) override;
		void OnImGuiRender() override;

	private:
		//OpenGL stuff
		std::unique_ptr<VertexArray> m_VAO;
		std::unique_ptr < IndexBuffer> m_IndexBuffer;
		std::unique_ptr < Shader> m_Shader;
		std::unique_ptr < Texture> m_Texture;
		std::unique_ptr < VertexBuffer> m_VertexBuffer;
	};
}