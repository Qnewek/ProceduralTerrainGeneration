#include "TestMapGen.h"

#include "utilities.h"

#include "imgui/imgui.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

test::TestMapGen::TestMapGen() : m_Width(50), m_Height(50), m_ChunkResX(40), m_ChunkResY(40), m_ChunkScale(0.05f), realHeight(255.0f),
m_Stride(8), m_MeshVertices(nullptr), m_MeshIndices(nullptr), deltaTime(0.0f), lastFrame(0.0f), seeLevel(64.0f),
m_Player(800, 600, glm::vec3(0.0f, 0.0f, 0.0f), 0.0001f, 40.0f, false, m_Height * m_ChunkResY),
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
	m_MainShader = std::make_unique<Shader>("res/shaders/Lightning_final_vertex.shader", "res/shaders/Lightning_final_fragment.shader");

	//Layout of the vertex buffer
		//Succesively: 
		// 3 floats for position [x,y,z], 
		// 3 floats for normal vector indicating direction the vertex faces
		// 2 floats for texture coordinates based on height
	m_Layout.Push<float>(3);
	m_Layout.Push<float>(3);
	m_Layout.Push<float>(2);

	m_MainVAO->AddBuffer(*m_MainVertexBuffer, m_Layout);
	m_Player.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	m_LightSource.SetPosition(glm::vec3(2.0f * realHeight, 2.0f * realHeight, 2.0f * realHeight));
	//m_Player.SetPosition(glm::vec3(0.5f * m_Width / m_ChunkScale, 1.0f / m_ChunkScale, 0.5f * m_Height / m_ChunkScale));
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

	//Since we are using texture sampling we dont need to set ambient and diffuse color 
	//but there is only one function that needs them as a parameter
	m_MainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 8.0f);
	m_MainShader->SetLightUniforms(m_LightSource.GetPosition(), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
	m_MainShader->SetViewPos((*m_Player.GetCameraRef()).GetPosition());
	m_MainShader->SetMVP(model, *(m_Player.GetCameraRef()->GetViewMatrix()), *(m_Player.GetCameraRef()->GetProjectionMatrix()));
	m_MainShader->SetUniform1f("seeLevel", seeLevel);

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
	terrainGen.setSeed(124);

	terrainGen.getContinentalnessNoiseConfig().constrast = 1.5f;
	terrainGen.getContinentalnessNoiseConfig().octaves = 7;
	terrainGen.getContinentalnessNoiseConfig().scale = m_ChunkScale;

	terrainGen.getMountainousNoiseConfig().constrast = 1.5f;
	terrainGen.getMountainousNoiseConfig().scale = m_ChunkScale;


	terrainGen.getPVNoiseConfig().constrast = 1.5f;
	terrainGen.getPVNoiseConfig().ridgeGain = 3.0f;
	terrainGen.getPVNoiseConfig().scale = m_ChunkScale;


	terrainGen.initializeMap();
	terrainGen.setSplines({ {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 40.0 ,64.0, 66.0, 68.0, 70.0},	//Continentalness {X,Y}
							{-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 5.0, 10.0, 20.0, 30.0, 80.0, 100.0, 170.0},	//Mountainousness {X,Y}
							{-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {0.5, 0.6, 0.7, 0.8, 0.9, 1.0}}); //PV {X,Y}

	if (!terrainGen.generateHeightMap()) {
		std::cout << "[ERROR] Map couldnt be generated" << std::endl;
	}

	utilities::parseNoiseChunksIntoVertices(m_MeshVertices, m_Width, m_Height, m_ChunkResX, m_ChunkResX, terrainGen.getHeightMap(), m_ChunkResX * 1.5f, m_Stride, 0);
	utilities::SimpleMeshIndicies(m_MeshIndices, m_Width * m_ChunkResX, m_Height * m_ChunkResY);
	utilities::InitializeNormals(m_MeshVertices, m_Stride, 3, m_Height * m_ChunkResY * m_Width * m_ChunkResX);
	utilities::CalculateNormals(m_MeshVertices, m_MeshIndices, m_Stride, 3, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6);
	utilities::NormalizeVector3f(m_MeshVertices, m_Stride, 3, m_Width * m_ChunkResX * m_Height * m_ChunkResY);
}


