#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Renderer.h"
#include "Camera.h"
#include "Player.h"
#include "LightSource.h"
#include "Noise.h"
#include "Erosion.h"
#include "TerrainGenerator.h"

class TerrainGenApp
{
public:
	TerrainGenApp();
	~TerrainGenApp();

	int Initialize();
	void Start();
	void ImGuiRender();

	void Draw();
	void PerformAction();
	void UpdatePrevCheckers();
	void CheckChange();
	void DrawAdjacent(Renderer& renderer, glm::mat4& model);

	void perlinImgui();
	void parametrizedImGui();
	void ErosionWindowRender();
	void DeactivateErosion();
	void PerformErosion();
	void PrintTrack(glm::mat4& model);
	void FullTerrainGeneration();
	void conditionalTerrainGeneration();
	void setTreeVertices();

	void PerlinDraw();
	void TerrainGenerationDraw();
private:
	//Time variables
	float deltaTime, lastFrame;

	//main variables
	int windowWidth, windowHeight;
	float rightPanelWidth, topPanelHeight, bottomPanelHeight, leftPanelWidth;

	//Basic vars
	int width, height, stride, prevHeight, prevWidth, tmpHeight, tmpWidth;
	float m_Scaling_Factor;

	GLFWwindow* window;
	Renderer renderer;
	Player player;

	//Pure Perlin
	float* meshVertices;
	unsigned int* meshIndices;
	int seed;
	bool testSymmetrical;
	noise::SimplexNoiseClass noise;

	//Erosion
	float *erosionVertices, *traceVertices;
	bool erosionWindow, trackDraw, erosionDraw;
	erosion::Erosion erosion;

	//OpenGL stuff
	VertexBufferLayout layout;
	std::unique_ptr<VertexArray> m_MainVAO;
	std::unique_ptr<VertexBuffer> m_MainVertexBuffer;
	std::unique_ptr<IndexBuffer> m_MainIndexBuffer;
	std::unique_ptr<Shader> m_MainShader;
	std::unique_ptr<Texture> m_MainTexture;
	//Erosion separate openGl
	std::unique_ptr<VertexArray> m_TrackVAO;
	std::unique_ptr<VertexBuffer> m_erosionBuffer;
	std::unique_ptr<VertexBuffer> m_TrackBuffer;
	std::unique_ptr<Shader> m_TrackShader;
	
	//TerrainGeneration openGl
	std::unique_ptr<VertexArray> m_TerGenVAO;
	std::unique_ptr<VertexBuffer> m_TerGenVertexBuffer;
	std::unique_ptr<IndexBuffer> m_TerGenIndexBuffer;
	std::unique_ptr<Shader> m_TerGenShader;
	std::unique_ptr<Texture> m_TerGenTexture;

	TerrainGenerator terrainGen;

	int m_ChunkResolution;
	float m_ChunkScale, seeLevel;
	float samplingScale = 0.05f;

	//Settings
	float* t_MeshVertices;
	unsigned int* t_MeshIndices;
	float* t_treesPositions;
	int treeIndicesCount;

	unsigned int treeVAO, treeVBO, instanceVBO, EBO;
	std::unique_ptr<Shader> m_TreeShader;

	//booleans
	bool isTerrainDisplayed;
	bool drawTrees = false;
	bool TerraGenPerform = false;

	enum class mode
	{
		PERLIN,
		PARAMETRIZED_GEN
	} currentMode;

	struct prevCheckers {
		noise::Options prevOpt;
		noise::IslandType prevIslandType;
		float prevCheckSum;
		bool prevRidge;
		bool prevIsland;
		bool symmetrical;
		int seed = 0;

		prevCheckers(noise::Options prevOpt = noise::Options::REVERT_NEGATIVES, noise::IslandType prevIslandType = noise::IslandType::CONE, float prevCheckSum = 0, bool prevRidge = false, bool prevIsland = false, bool symmetrical = false, int seed = 0)
			: prevOpt(prevOpt), prevCheckSum(prevCheckSum), prevRidge(prevRidge), prevIsland(prevIsland), prevIslandType(prevIslandType), seed(seed), symmetrical(symmetrical) {}
	} prevCheck;
};