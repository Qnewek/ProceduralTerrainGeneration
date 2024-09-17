#include "TestImage.h"

#include "Renderer.h"
#include "imgui/imgui.h"

#include <iostream>
#include <memory>

namespace test
{
	TestImage::TestImage()
	{
		float vertices[] = {
			// positions        // texture coords
			 0.5f,  0.5f, 0.0f,  1.0f, 1.0f, // top right
			 0.5f, -0.5f, 0.0f,  1.0f, 0.0f, // bottom right
			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f, // bottom left
			-0.5f,  0.5f, 0.0f,  0.0f, 1.0f  // top left 
		};
		unsigned int indices[] = {
	   0, 1, 3, // first triangle
	   1, 2, 3  // second triangle
		};
		GLCALL(glEnable(GL_BLEND));
		GLCALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

		//Mesh setup
		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(vertices, 4*5 *sizeof(float));
		m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 6);
		m_Shader = std::make_unique<Shader>("res/shaders/test_vertex.shader", "res/shaders/test_frag.shader");
		m_Texture = std::make_unique<Texture>("res/textures/Basic_biome_texture_palette.jpg");

		VertexBufferLayout layout;
		layout.Push<float>(3);
		layout.Push<float>(2);

		m_VAO->AddBuffer(*m_VertexBuffer, layout);
	}

	TestImage::~TestImage()
	{
	}

	void TestImage::OnUpdate(float deltaTime)
	{
	}

	void TestImage::OnRender(GLFWwindow& window, Renderer& renderer)
	{
		renderer.Clear();
		m_Texture->Bind();
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

	void TestImage::OnImGuiRender()
	{
		
	}
}