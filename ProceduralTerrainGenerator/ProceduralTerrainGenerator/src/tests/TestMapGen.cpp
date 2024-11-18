#include "TestMapGen.h"

#include "utilities.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

test::TestMapGen::TestMapGen() : m_Width(30), m_Height(30), m_ChunkResX(40), m_ChunkResY(40), m_ChunkScale(0.05f), m_Stride(8),
m_MeshVertices(nullptr), m_MeshIndices(nullptr), deltaTime(0.0f), lastFrame(0.0f),
m_Player(800, 600, glm::vec3(0.0f, 0.0f, 0.0f), 0.0001f, 20.0f, false, m_Height * m_ChunkResY),
m_LightSource(glm::vec3(0.0f, 0.0f, 0.0f), 1.0f), noise()
{
	// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
	m_MeshIndices = new unsigned int[(m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6];
	m_MeshVertices = new float[m_Width * m_ChunkResX * m_Height * m_ChunkResY * m_Stride];

	noise.setMapSize(m_Width, m_Height);
	noise.setMapSize(m_Width, m_Height);
	noise.setChunkSize(m_ChunkResX, m_ChunkResY);
	noise.setScale(m_ChunkScale);
	noise.getConfigRef().option = noise::Options::REFIT_ALL;
	utilities::benchmark_void(utilities::GenerateTerrainMap, "GenerateTerrainMap", noise, m_MeshVertices, m_MeshIndices, m_Stride);
	utilities::PaintNotByTexture(m_MeshVertices, m_Width * m_ChunkResX, m_Height * m_ChunkResY, m_Stride, 6);

	//OpenGL setup for the mesh
	m_MainVAO = std::make_unique<VertexArray>();
	m_MainVertexBuffer = std::make_unique<VertexBuffer>(m_MeshVertices, m_Width * m_ChunkResX * m_Height * m_ChunkResY * m_Stride * sizeof(float));
	m_MainIndexBuffer = std::make_unique<IndexBuffer>(m_MeshIndices, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6);
	m_MainShader = std::make_unique<Shader>("res/shaders/Lightning_vertex.shader", "res/shaders/Lightning_fragment.shader");

	//Layout of the vertex buffer
		//Succesively: 
		// 3 floats for position [x,y,z], 
		// 3 floats for normal vector indicating direction the vertex faces
		// 2 floats for texture coordinates based on height
	m_Layout.Push<float>(3);
	m_Layout.Push<float>(3);
	m_Layout.Push<float>(2);

	m_MainVAO->AddBuffer(*m_MainVertexBuffer, m_Layout);
	m_Player.SetPosition(glm::vec3(0.5f * m_Width / m_ChunkScale, 1.0f / m_ChunkScale, 0.5f * m_Height / m_ChunkScale));
}

test::TestMapGen::~TestMapGen()
{
	delete[] m_MeshVertices;
	delete[] m_MeshIndices;
}

void test::TestMapGen::OnUpdate(float deltaTime)
{
}

void test::TestMapGen::OnRender(GLFWwindow& window, Renderer& renderer)
{
	renderer.Clear();

	float currentFrame = static_cast<float>(glfwGetTime());
	deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;

	m_Player.SteerPlayer(&window, m_MeshVertices, m_Stride, deltaTime);

	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, glm::vec3(0.0f, -0.5f / m_ChunkScale, 0.0f));

	m_LightSource.SetPosition(glm::vec3(20.0f, 20.0f, 20.0f));

	//Since we are using texture sampling we dont need to set ambient and diffuse color 
	//but there is only one function that needs them as a parameter
	m_MainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 8.0f);
	m_MainShader->SetLightUniforms(m_LightSource.GetPosition(), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
	m_MainShader->SetViewPos((*m_Player.GetCameraRef()).GetPosition());
	m_MainShader->SetMVP(model, *(m_Player.GetCameraRef()->GetViewMatrix()), *(m_Player.GetCameraRef()->GetProjectionMatrix()));

	//Render lightning source cube
	m_LightSource.Draw(renderer, *(m_Player.GetCameraRef()->GetViewMatrix()), *(m_Player.GetCameraRef()->GetProjectionMatrix()));

	//Render terrain
	m_MainVAO->AddBuffer(*m_MainVertexBuffer, m_Layout);
	renderer.DrawWithTexture(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);

}

void test::TestMapGen::OnImGuiRender()
{
}
