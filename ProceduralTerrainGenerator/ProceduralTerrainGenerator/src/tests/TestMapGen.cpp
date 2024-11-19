#include "TestMapGen.h"

#include "utilities.h"

#include "imgui/imgui.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

test::TestMapGen::TestMapGen() : m_Width(20), m_Height(20), m_ChunkResX(10), m_ChunkResY(10), m_ChunkScale(0.05f), m_Stride(8),
m_MeshVertices(nullptr), m_MeshIndices(nullptr), deltaTime(0.0f), lastFrame(0.0f),
m_Player(800, 600, glm::vec3(0.0f, 0.0f, 0.0f), 0.0001f, 20.0f, false, m_Height * m_ChunkResY),
m_LightSource(glm::vec3(0.0f, 0.0f, 0.0f), 1.0f), noise(), terrainGen()
{
	// 6 indices per quad which is 2 triangles so there will be (width-1 * height-1 * 2) triangles
	m_MeshIndices = new unsigned int[(m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6];
	m_MeshVertices = new float[m_Width * m_ChunkResX * m_Height * m_ChunkResY * m_Stride];

	conditionalTerrainGeneration();

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
	model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f));

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
	ImVec2 minSize = ImVec2(500, 50);
	ImVec2 maxSize = ImVec2(800, 800);

	ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
	ImGui::Begin("Debug");

	glm::vec3 playerPos = m_Player.GetCameraRef()->GetPosition();
	ImGui::Text("Player pos: x = %.2f, y = %.2f, z = %.2f", playerPos.x, playerPos.y, playerPos.z);

	ImGui::End();
}

void test::TestMapGen::basicTerrainGeneration()
{
	noise.setMapSize(m_Width, m_Height);
	noise.setChunkSize(m_ChunkResX, m_ChunkResY);
	noise.setScale(m_ChunkScale);
	noise.getConfigRef().option = noise::Options::REFIT_ALL;
	utilities::benchmark_void(utilities::GenerateTerrainMap, "GenerateTerrainMap", noise, m_MeshVertices, m_MeshIndices, m_Stride);
	utilities::PaintNotByTexture(m_MeshVertices, m_Width * m_ChunkResX, m_Height * m_ChunkResY, m_Stride, 6);
}

void test::TestMapGen::conditionalTerrainGeneration()
{
	terrainGen.setSize(m_Width, m_Height);
	terrainGen.setChunkResolution(m_ChunkResX);
	terrainGen.setChunkScalingFactor(m_ChunkScale);
	terrainGen.setSeed(124);
	terrainGen.getContinentalnessNoiseConfig().constrast = 2.0f;
	terrainGen.getMountainousNoiseConfig().constrast = 2.0f;
	terrainGen.getPVNoiseConfig().constrast = 2.0f;
	terrainGen.initializeMap();
	terrainGen.setSplines({ {-1.0, -0.5, 0.0, 0.2, 0.6, 1.0}, {0.1, 0.3, 0.5, 0.6, 0.9, 1.0},	//Continentalness {X,Y}
							{-1.0, -0.5, 0.0, 0.2, 0.6, 1.0}, {0.1, 0.5, 1.0, 5.0, 15.0, 30.0},	//Mountainousness {X,Y}
							{-1.0, -0.5, 0.0, 0.2, 0.6, 1.0}, {1.0, 0.6, 0.3, 0.0, 0.2, 0.1}}); //PV {X,Y}

	if (!terrainGen.generateHeightMap()) {
		std::cout << "[ERROR] Map couldnt be generated" << std::endl;
	}

	utilities::parseNoiseChunksIntoVertices(m_MeshVertices, m_Width, m_Height, m_ChunkResX, m_ChunkResX, terrainGen.getHeightMap(), 1.0f / m_ChunkScale, m_Stride, 0);
	utilities::SimpleMeshIndicies(m_MeshIndices, m_Width * m_ChunkResX, m_Height * m_ChunkResY);
	utilities::InitializeNormals(m_MeshVertices, m_Stride, 3, m_Height * m_ChunkResY * m_Width * m_ChunkResX);
	utilities::CalculateNormals(m_MeshVertices, m_MeshIndices, m_Stride, 3, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6);
	utilities::NormalizeVector3f(m_MeshVertices, m_Stride, 3, m_Width * m_ChunkResX * m_Height * m_ChunkResY);
}


