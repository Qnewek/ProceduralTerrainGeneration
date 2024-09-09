#include "TestPerlinDraw.h"

#include "Renderer.h"
#include "imgui/imgui.h"
#include "Noise.h"
#include "utilities.h"

#include <memory>

namespace test
{
	TestPerlinDraw::TestPerlinDraw() :height(500), width(500), 
		noise(nullptr), image(nullptr), octaves(8), prevOpt(noise::Options::REVERT_NEGATIVES),
		scale(400), constrast(1.2f), option(noise::Options::REVERT_NEGATIVES)
	{
		noise = new float[width * height];
		image = new unsigned char[height * width];

		utilities::benchmark_void(noise::getNoiseMap, "getNoiseMap", noise, height, width, scale, octaves, constrast, noise::Options::REVERT_NEGATIVES);
		utilities::benchmark_void(utilities::ConvertToGrayscaleImage, "ConvertToGreyScale", noise, image, height, width);

		float vertices[] = {
			-0.75f,  0.75f,  0.0f, 1.0f,
			-0.75f, -0.75f,  0.0f, 0.0f,
			 0.75f, -0.75f,  1.0f, 0.0f,
			 0.75f,  0.75f,  1.0f, 1.0f
		};
		unsigned int indices[] = {
			0, 1, 2,
			2, 3, 0
		};

		GLCALL(glEnable(GL_BLEND));
		GLCALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

		m_VAO = std::make_unique<VertexArray>();
		m_VertexBuffer = std::make_unique<VertexBuffer>(vertices, 4 * 4 * sizeof(float));
		m_IndexBuffer = std::make_unique<IndexBuffer>(indices, 6);

		VertexBufferLayout layout;
		layout.Push<float>(2);
		layout.Push<float>(2);

		m_VAO->AddBuffer(*m_VertexBuffer, layout);

		m_Shader = std::make_unique<Shader>("res/shaders/map_vertex.shader", "res/shaders/map_fragment.shader");
		m_Texture = std::make_unique<Texture>(height, width, image);
		m_Shader->SetUniform1i("u_Texture", 0);
	}

	TestPerlinDraw::~TestPerlinDraw()
	{
		delete[] noise;
		delete[] image;
	}

	void TestPerlinDraw::OnUpdate(float deltaTime)
	{
	}

	void TestPerlinDraw::OnRender()
	{
		GLCALL(glClearColor(0.37f, 0.77f, 1.0f, 1.0f));
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		Renderer renderer;

		if (prevOpt != option || checkSum - ((float)octaves+scale+constrast) != 0)
		{
			checkSum = (float)octaves + scale + constrast;
			prevOpt = option;
			utilities::benchmark_void(noise::getNoiseMap, "getNoiseMap", noise, height, width, scale, octaves, constrast, noise::Options::REVERT_NEGATIVES);
			utilities::benchmark_void(utilities::ConvertToGrayscaleImage, "ConvertToGreyScale", noise, image, height, width);
			m_Texture->SetNewImage(image);
		}

		m_Texture->Bind();
		renderer.Draw(*m_VAO, *m_IndexBuffer, *m_Shader);
	}

	void TestPerlinDraw::OnImGuiRender()
	{
		ImGui::SliderInt("Octaves", &octaves, 1, 10);
		ImGui::SliderFloat("Scale", &scale, 100.0f, 1000.0f);
		ImGui::SliderFloat("Constrast", &constrast, 0.1f, 2.0f);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
}
