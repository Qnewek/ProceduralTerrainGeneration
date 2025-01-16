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

	void UpdatePrevCheckers();
	void CheckChange();
	void ResizePerlin();
	void PerformPerlin();
	void PerlinChunked();
	
	void ImGuiRender();
	void perlinImgui();
	void parametrizedImGui();
	void ErosionWindowRender();
	void ParameterImgui();
	void BiomeImGui();
	void TreeTypesImGui();
	void SwapNoise(noise::SimplexNoiseClass* n);
	
	void Draw();
	void PerlinDraw(glm::mat4& model);
	void TerrainGenerationDraw(glm::mat4& model);
	void DrawTrees(glm::mat4& model);
	void PrintTrack(glm::mat4& model);

	void PerformAction();
	void DrawAdjacent(Renderer& renderer, glm::mat4& model);
	void DeactivateErosion();
	void PerformErosion();

	void initializeTerrainGeneration();
	void resizeTerrainGeneration();
	void FullTerrainGeneration();
	void TerrainGeneration();

	void PrepareTreesDraw();

private:
	//Time variables
	float deltaTime, lastFrame, drawScale;

	//Window Layout variables
	int windowWidth, windowHeight;
	float rightPanelWidth, topPanelHeight, bottomPanelHeight, leftPanelWidth;
	GLFWwindow* window;
	Renderer renderer;
	Player player;
	std::string editedNoise = "";

	//Map Size variables
	int width, height, stride, prevHeight, prevWidth, tmpHeight, tmpWidth;
	float m_Scaling_Factor;

	//Pure Perlin
	noise::SimplexNoiseClass basicPerlinNoise;
	noise::SimplexNoiseClass* noise;
	float* meshVertices;
	unsigned int* meshIndices;
	int seed;
	bool testSymmetrical;

	//Erosion
	erosion::Erosion erosion;
	float *erosionVertices, *traceVertices;
	bool erosionWindow, trackDraw, erosionDraw;

	//TerrainGenerator
	TerrainGenerator terrainGen;
	std::vector<std::vector<double>> splines;
	std::vector<std::vector<RangedLevel>> ranges;
	std::vector<biome::Biome> biomes;
	biome::Biome* biome = nullptr;

	char editedType = ' ';
	int m_ChunkResolution, prevChunkRes, tmpChunkRes;
	float m_ChunkScale, seeLevel, samplingScale = 0.05f;

	//Settings
	float* t_MeshVertices, * t_treesPositions;
	unsigned int* t_MeshIndices;
	int treeIndicesCount;
	bool isTerrainDisplayed, drawTrees = false, TerraGenPerform = false, noiseEdit = false, biomeEdit = false, treeEdit = false;

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
	//Tree openGl
	unsigned int treeVAO, treeVBO, instanceVBO, EBO;
	std::unique_ptr<Shader> m_TreeShader;
	
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