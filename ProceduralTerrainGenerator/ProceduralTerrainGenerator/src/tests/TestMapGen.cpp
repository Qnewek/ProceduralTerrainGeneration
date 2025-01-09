#include "TestMapGen.h"

#include "utilities.h"

#include "imgui/imgui.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"	

test::TestMapGen::TestMapGen() : m_Width(20), m_Height(20), m_ChunkResX(20), m_ChunkResY(20), m_ChunkScale(0.05f), realHeight(255.0f),
m_Stride(8), m_MeshVertices(nullptr), m_MeshIndices(nullptr), deltaTime(0.0f), lastFrame(0.0f), seeLevel(64.0f),
m_Player(800, 600, glm::vec3(0.0f, 0.0f, 0.0f), 0.0001f, 10.0f, false, m_Height* m_ChunkResY), isTerrainDisplayed(true),
m_LightSource(glm::vec3(0.0f, 0.0f, 0.0f), 1.0f), noise(), terrainGen(), m_treesPositions(nullptr)//, obj(nullptr)
{
	//vertices times 4 cause we are using 4 unique vertices for each quad
	//indices times 6 cause we are using 6 indices for forming each quad
	m_MeshVertices = new float[(m_Width * m_ChunkResX-1) * (m_Height * m_ChunkResY - 1) * m_Stride * 4];
	m_MeshIndices = new unsigned int[(m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6];

	conditionalTerrainGeneration();

	//OpenGL setup for the mesh
	m_MainVAO = std::make_unique<VertexArray>();
	m_MainVertexBuffer = std::make_unique<VertexBuffer>(m_MeshVertices, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 4 * m_Stride * sizeof(float));
	m_MainIndexBuffer = std::make_unique<IndexBuffer>(m_MeshIndices, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6);
	m_MainShader = std::make_unique<Shader>("res/shaders/Lightning_final_vertex.shader", "res/shaders/Lightning_final_fragment.shader");
	m_MainTexture = std::make_unique<Texture>("res/textures/texture.png");

	//Layout of the vertex buffer
		//Successively: 
		// 3 floats for position [x,y,z], 
		// 3 floats for normal vector indicating direction the vertex faces
		// 2 floats for Biome texture coordinates
	m_Layout.Push<float>(3);
	m_Layout.Push<float>(3);
	m_Layout.Push<float>(2);

	//Set openGl buffers for trees
	setTreeVertices();
	m_TreeShader = std::make_unique<Shader>("res/shaders/test_vertex.shader", "res/shaders/test_frag.shader");

	m_MainVAO->AddBuffer(*m_MainVertexBuffer, m_Layout);
	//m_ObjectsVAO->AddBuffer(*m_ObjectsVertexBuffer, m_ObjectsLayout);
	m_Player.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	m_LightSource.SetPosition(glm::vec3(2.0f * realHeight, 2.0f * realHeight, 2.0f * realHeight));
	//m_Player.SetPosition(glm::vec3(0.5f * m_Width / m_ChunkScale, 1.0f / m_ChunkScale, 0.5f * m_Height / m_ChunkScale));

	utilities::saveToObj("res/models/", "terrain", m_MeshVertices, m_MeshIndices, m_Stride, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * m_Stride * 4, true);
}

test::TestMapGen::~TestMapGen()
{
	if (m_MeshVertices)
		delete[] m_MeshVertices;
	if (m_MeshIndices)
		delete[] m_MeshIndices;
	if (m_treesPositions)
		delete[] m_treesPositions;
}

void test::TestMapGen::OnUpdate(float deltaTime)
{
}

void test::TestMapGen::OnRender(GLFWwindow& window, Renderer& renderer)
{
	renderer.Clear(glm::vec3(0.6f, 0.6f, 0.6f));

	float currentFrame = static_cast<float>(glfwGetTime());
	deltaTime = currentFrame - lastFrame;
	lastFrame = currentFrame;

	m_Player.SteerPlayer(&window, m_MeshVertices, m_Stride, deltaTime);

	glm::mat4 model = glm::mat4(1.0f);

	//Since we are using texture sampling we dont need to set ambient and diffuse color 
	//but there is only one function that needs them as a parameter
	m_MainShader->Bind();
	m_MainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
	m_MainShader->SetLightUniforms(m_LightSource.GetPosition(), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
	m_MainShader->SetViewPos((*m_Player.GetCameraRef()).GetPosition());
	m_MainShader->SetMVP(model, *(m_Player.GetCameraRef()->GetViewMatrix()), *(m_Player.GetCameraRef()->GetProjectionMatrix()));
	m_MainShader->SetUniform1f("seeLevel", seeLevel);
	m_MainShader->SetUniform1i("u_Texture", 0);

	//Render lightning source cube
	m_LightSource.Draw(renderer, *(m_Player.GetCameraRef()->GetViewMatrix()), *(m_Player.GetCameraRef()->GetProjectionMatrix()));

	//Render terrain
	m_MainVAO->AddBuffer(*m_MainVertexBuffer, m_Layout);
	m_MainTexture->Bind();
	if (isTerrainDisplayed)
		renderer.DrawWithTexture(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);

	m_TreeShader->Bind();
	m_TreeShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
	m_TreeShader->SetLightUniforms(m_LightSource.GetPosition(), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
	m_TreeShader->SetViewPos((*m_Player.GetCameraRef()).GetPosition());
	m_TreeShader->SetMVP(model, *(m_Player.GetCameraRef()->GetViewMatrix()), *(m_Player.GetCameraRef()->GetProjectionMatrix()));


	glBindVertexArray(treeVAO);
	glDrawElementsInstanced(GL_TRIANGLES, treeIndicesCount, GL_UNSIGNED_INT, 0, terrainGen.getTreeCount());
	glBindVertexArray(0);
}

void test::TestMapGen::OnImGuiRender()
{
	ImVec2 minSize = ImVec2(500, 50);
	ImVec2 maxSize = ImVec2(800, 800);

	ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
	ImGui::Begin("Debug");

	glm::vec3 playerPos = m_Player.GetCameraRef()->GetPosition();
	ImGui::Text("Player pos: x = %.2f, y = %.2f, z = %.2f", playerPos.x, playerPos.y, playerPos.z);

	ImGui::Button("Display terrain");
	if (ImGui::IsItemClicked()) {
		isTerrainDisplayed = !isTerrainDisplayed;
	}

	ImGui::End();
}

void test::TestMapGen::conditionalTerrainGeneration()
{
	terrainGen.setSize(m_Width, m_Height);
	terrainGen.setChunkResolution(m_ChunkResX);
	terrainGen.setSeed(742);

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
							{-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} }); //PV {X,Y}

	std::vector<biome::Biome> biomes = {
		biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, m_ChunkResX * m_ChunkResY * 0.2f),
		biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, 2, m_ChunkResX * m_ChunkResY * 0.01f),
		biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, 7, m_ChunkResX * m_ChunkResY * 0.03f),
		biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, 8, m_ChunkResX * m_ChunkResY * 0.01f),
		biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, 0, m_ChunkResX * m_ChunkResY * 0.02f),
		biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, 5, m_ChunkResX * m_ChunkResY * 0.0f)
	};

	std::vector<std::vector<RangedLevel>> ranges = {
		{{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
		{{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
		{{-1.0f, -0.7f, 0},{-0.7f, -0.2f, 1},{ -0.2f, 0.03f, 2},{0.03f, 0.3f, 3},{0.3f, 1.1f, 4}},
		{{-1.0f, -0.78f, 0},{-0.78f, -0.37f, 1},{-0.37f, -0.2f, 2},{-0.2f, 0.05f, 3},{0.05f, 0.45f, 4},{0.45f, 0.55f, 5},{0.55f, 1.1f, 6}}
	};

	terrainGen.setBiomes(biomes);
	terrainGen.setRanges(ranges);


	if (!terrainGen.performTerrainGeneration()) {
		std::cout << "[ERROR] Map couldnt be generated" << std::endl;
		return;
	}

	seeLevel *= 0.2f;
	utilities::createTiledVertices(m_MeshVertices, m_Width * m_ChunkResX, m_Height * m_ChunkResY, terrainGen.getHeightMap(), 0.2f, m_Stride, 0);
	utilities::createIndicesTiledField(m_MeshIndices, m_Width * m_ChunkResX, m_Height * m_ChunkResY);
	utilities::InitializeNormals(m_MeshVertices, m_Stride, 3, (m_Height * m_ChunkResY - 1) * (m_Width * m_ChunkResX - 1) * 4);
	utilities::CalculateNormals(m_MeshVertices, m_MeshIndices, m_Stride, 3, (m_Width * m_ChunkResX - 1) * (m_Height * m_ChunkResY - 1) * 6);
	utilities::NormalizeVector3f(m_MeshVertices, m_Stride, 3, (m_Height * m_ChunkResY - 1) * (m_Width * m_ChunkResX - 1) * 4);
	utilities::AssignTexturesByBiomes(terrainGen, m_MeshVertices, m_Width * m_ChunkResX, m_Height * m_ChunkResY, 3, m_Stride, 6);
}

void test::TestMapGen::setTreeVertices()
{
	float scaleFactor = 0.2f;
	float treeVertices[] = {
		// Pien drzewa (cylinder)
		// Pozycje                          // Normale         // Kolor
		// Dolna podstawa
		0.4f * scaleFactor, 0.00f * scaleFactor, 0.4f * scaleFactor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
		0.4f * scaleFactor, 0.00f * scaleFactor, 0.6f * scaleFactor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
		0.6f * scaleFactor, 0.00f * scaleFactor, 0.6f * scaleFactor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
		0.6f * scaleFactor, 0.00f * scaleFactor, 0.4f * scaleFactor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
		// Gorna podstawa
		0.4f * scaleFactor, 0.25f * scaleFactor, 0.4f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
		0.4f * scaleFactor, 0.25f * scaleFactor, 0.6f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
		0.6f * scaleFactor, 0.25f * scaleFactor, 0.6f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
		0.6f * scaleFactor, 0.25f * scaleFactor, 0.4f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
		// Korona drzewa (sto¿ek)
		// Pozycje                          // Normale         // Kolor
		0.0f * scaleFactor, 0.25f * scaleFactor, 0.0f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
		1.0f * scaleFactor, 0.25f * scaleFactor, 0.0f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
		1.0f * scaleFactor, 0.25f * scaleFactor, 1.0f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
		0.0f * scaleFactor, 0.25f * scaleFactor, 1.0f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
		0.5f * scaleFactor, 1.00f * scaleFactor, 0.5f * scaleFactor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f
	};

	unsigned int treeIndices[] = {
		// Dolna podstawa
		0, 1, 2,
		2, 3, 0,
		// Gorna podstawa
		4, 5, 6,
		6, 7, 4,
		// Boki
		0, 1, 5,
		0, 5, 4,
		1, 2, 6,
		1, 6, 5,
		2, 3, 7,
		2, 7, 6,
		3, 0, 4,
		3, 4, 7,
		// Korona
		8, 9, 10,
		8, 10, 11,
		8, 9, 12,
		9, 10, 12,
		10, 11, 12,
		11, 8, 12
	};
	treeIndicesCount = sizeof(treeIndices) / sizeof(treeIndices[0]);

	m_treesPositions = new float[3 * terrainGen.getTreeCount()];
	std::cout << terrainGen.getTreeCount() << std::endl;

	int index = 0;

	for (auto& it : terrainGen.getVegetationPoints()) {
		for (auto& it2 : it) {
			m_treesPositions[index++] = it2.first * scaleFactor;
			m_treesPositions[index++] = terrainGen.getHeightAt(it2.first, it2.second) * scaleFactor;
			m_treesPositions[index++] = it2.second * scaleFactor;
		}
	}

	glGenVertexArrays(1, &treeVAO);
	glGenBuffers(1, &treeVBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(treeVAO);

	glBindBuffer(GL_ARRAY_BUFFER, treeVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(treeVertices), treeVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(treeIndices), treeIndices, GL_STATIC_DRAW);

	// Ustawienia atrybutów wierzcho³ków
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);
	
	glGenBuffers(1, &instanceVBO);
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * terrainGen.getTreeCount(), m_treesPositions, GL_STATIC_DRAW);

	glBindVertexArray(treeVAO);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glVertexAttribDivisor(3, 1);

	glBindVertexArray(0);
}


