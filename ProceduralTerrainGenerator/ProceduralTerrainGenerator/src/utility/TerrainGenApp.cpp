#include "TerrainGenApp.h"
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "utilities.h"

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(200.0f), leftPanelWidth(400.0f), 
width(200), height(200), prevHeight(200), prevWidth(200), tmpHeight(200), tmpWidth(200), stride(8), seed(123), m_Scaling_Factor(1.0f), deltaTime(0.0f), lastFrame(0.0f),
renderer(), player(800, 600, glm::vec3(0.0f, 0.0f, 0.0f), 0.0001f, 10.0f, false, height),
meshVertices(nullptr), meshIndices(nullptr), testSymmetrical(false), noise(), layout(), currentMode(mode::PERLIN),
erosionWindow(false), trackDraw(false), erosionDraw(false), erosionVertices(nullptr), traceVertices(nullptr), erosion(width, height),
terrainGen(), t_MeshVertices(nullptr), t_MeshIndices(nullptr), t_treesPositions(nullptr), m_ChunkResolution(20), seeLevel(64.0f), isTerrainDisplayed(true)
{
    std::cout << "Initialized" << std::endl;
}

TerrainGenApp::~TerrainGenApp()
{
	delete[] meshVertices;
	delete[] meshIndices;
	delete[] erosionVertices;
	delete[] traceVertices;
	delete[] t_MeshVertices;
	delete[] t_MeshIndices;
	delete[] t_treesPositions;
}


int TerrainGenApp::Initialize()
{
    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    windowWidth = mode->width;
    windowHeight = mode->height;
    window = glfwCreateWindow(windowWidth, windowHeight, "Procedural terrain generator", NULL, NULL);

    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cout << "error" << std::endl;
    }

    GLCALL(glEnable(GL_BLEND));
    GLCALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    glEnable(GL_DEPTH_TEST);

	//Initialising basic mode variables
    meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
    meshVertices = new float[width * height * stride];

    //Initial fractal noise generation in order to draw something on the start of the test
    noise.setMapSize(width, height);
    noise.initMap();
    utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, meshVertices, meshIndices, m_Scaling_Factor, stride, true, true);
    utilities::PaintNotByTexture(meshVertices, width, height, stride, 6);

	m_MainVAO = std::make_unique<VertexArray>();
	m_MainVertexBuffer = std::make_unique<VertexBuffer>(meshVertices, (height * width) * stride * sizeof(float));
	m_MainIndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
	m_MainShader = std::make_unique<Shader>("res/shaders/Lightning_vertex.shader", "res/shaders/Lightning_fragment.shader");
	m_MainTexture = std::make_unique<Texture>("res/textures/Basic_biome_texture_palette.jpg");

    m_erosionBuffer = std::make_unique<VertexBuffer>(erosionVertices, (height * width) * stride * sizeof(float));
    m_TrackShader = std::make_unique<Shader>("res/shaders/Trace_vertex.shader", "res/shaders/Trace_fragment.shader");
    m_TrackVAO = std::make_unique<VertexArray>();

    layout.Push<float>(3);
    layout.Push<float>(3);
    layout.Push<float>(2);

    UpdatePrevCheckers();
    player.SetPosition(glm::vec3(-1.0f * m_Scaling_Factor, 1.0f * m_Scaling_Factor, -1.0f * m_Scaling_Factor));

    return 0;
}

void TerrainGenApp::Start()
{
    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, true);
    ImGui::StyleColorsDark();
    while (!glfwWindowShouldClose(window))
    {
        renderer.Clear(glm::vec3(0.2f, 0.2f, 0.2f));
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        player.SteerPlayer(window, meshVertices, stride, deltaTime);

		Draw();
		ImGuiRender();
        PerformAction();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}

void TerrainGenApp::ImGuiRender()
{
    ImGui_ImplGlfwGL3_NewFrame();
    ImGui::SetNextWindowPos(ImVec2(windowWidth - rightPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, windowHeight), ImGuiCond_Always);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
    rightPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;
	ImGui::Text("Terrain settings");
    ImGui::InputInt("Height", &tmpHeight);
	ImGui::InputInt("Width", &tmpWidth);
    if (ImGui::Button("Resize"))
    {
		height = tmpHeight;
		width = tmpWidth;
    }
    if (ImGui::Button("Save file")) {
        if(currentMode==mode::PARAMETRIZED_GEN)
            utilities::saveToObj("res/models/", "terrain", t_MeshVertices, t_MeshIndices, stride, (width - 1) * (height - 1) * 6, (width - 1) * (height - 1) * stride * 4, false);
		if (currentMode == mode::PERLIN)
			utilities::saveToObj("res/models/", "terrain", meshVertices, meshIndices, stride, (width - 1) * (height - 1) * 6, width * height * stride, false);
    }
	ImGui::Separator();
    if (currentMode == mode::PERLIN)
        perlinImgui();
	if (currentMode == mode::PARAMETRIZED_GEN)
	{
        parametrizedImGui();
	}
    ImGui::Separator();
	if (erosionWindow)
		ErosionWindowRender();


    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, windowHeight), ImGuiCond_Always);
    ImGui::Begin("Nie wiem", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
    leftPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, topPanelHeight), ImGuiCond_Always);
	ImGui::Begin("Mode", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
	ImGui::Text("Mode: ");
    ImGui::SameLine();
    if (ImGui::Button("Pure Perlin"))
    {
		currentMode = mode::PERLIN;
        player.SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Parametrized generation"))
    {
		currentMode = mode::PARAMETRIZED_GEN;
		TerraGenPerform = true;
        player.SetPosition(glm::vec3(0.0f, 50.0f, 0.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Erode"))
    {
		erosionWindow = !erosionWindow;
        if (!erosionWindow)
            DeactivateErosion();
    }
    ImGui::SameLine();
    if (ImGui::Button("Vegetation generation"))
    {
		drawTrees = !drawTrees;
		if (drawTrees)
			setTreeVertices();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, windowHeight - bottomPanelHeight - 60), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, bottomPanelHeight), ImGuiCond_Always);
    ImGui::Begin("OutPut", nullptr, ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
	ImGui::Text("Press 'm' to steer the camera, Press 'ESC' to release mouse");
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;
    
    ImGui::End();


    ImGui::Render();
    ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
}

void TerrainGenApp::Draw()
{
    glEnable(GL_SCISSOR_TEST);
    glViewport(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    glScissor(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    renderer.Clear(glm::vec3(0.37f, 0.77f, 1.0f));

    if (currentMode == mode::PERLIN)
        PerlinDraw();
    else if (currentMode == mode::PARAMETRIZED_GEN)
        TerrainGenerationDraw();
    
    glDisable(GL_SCISSOR_TEST);
}

void TerrainGenApp::PerlinDraw() {
    glm::mat4 model = glm::mat4(1.0f);

    m_MainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 8.0f);
    m_MainShader->SetLightUniforms(glm::vec3(0.5f * m_Scaling_Factor, 2.0f * m_Scaling_Factor, 0.5f * m_Scaling_Factor), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    m_MainShader->SetViewPos((*player.GetCameraRef()).GetPosition());
    m_MainShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

    m_MainTexture->Bind();
    m_MainVAO->AddBuffer(*m_MainVertexBuffer, layout);
    renderer.DrawWithTexture(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
    if (testSymmetrical)
        DrawAdjacent(renderer, model);

    if (erosionDraw) {
        model = glm::translate(model, glm::vec3(m_Scaling_Factor + 0.2f, 0.0f, 0.0f));

        m_MainVAO->AddBuffer(*m_erosionBuffer, layout);
        m_MainShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

        renderer.Draw(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
        PrintTrack(model);
    }
}

void TerrainGenApp::TerrainGenerationDraw() {
    glm::mat4 model = glm::mat4(1.0f);

    //Since we are using texture sampling we dont need to set ambient and diffuse color 
    //but there is only one function that needs them as a parameter
    m_TerGenShader->Bind();
    m_TerGenShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
    m_TerGenShader->SetLightUniforms(glm::vec3(0.5f * m_Scaling_Factor * height, 1.0f * m_Scaling_Factor * 255.0f, 0.5f * m_Scaling_Factor * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    m_TerGenShader->SetViewPos((*player.GetCameraRef()).GetPosition());
    m_TerGenShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));
    m_TerGenShader->SetUniform1f("seeLevel", seeLevel);
    m_TerGenShader->SetUniform1i("u_Texture", 0);

    //Render terrain
	m_TerGenVAO->Bind();
    m_TerGenVAO->AddBuffer(*m_TerGenVertexBuffer, layout);
    m_TerGenTexture->Bind();
    if (isTerrainDisplayed)
        renderer.DrawWithTexture(*m_TerGenVAO, *m_TerGenIndexBuffer, *m_TerGenShader);
    if (drawTrees) {
        m_TreeShader->Bind();
        m_TreeShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
        m_TreeShader->SetLightUniforms(glm::vec3(0.0f, 1.0f * m_Scaling_Factor * 255.0f, 0.5f * m_Scaling_Factor * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
        m_TreeShader->SetViewPos((*player.GetCameraRef()).GetPosition());
        m_TreeShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));


        glBindVertexArray(treeVAO);
        glDrawElementsInstanced(GL_TRIANGLES, treeIndicesCount, GL_UNSIGNED_INT, 0, terrainGen.getTreeCount());
        glBindVertexArray(0);
    }
}

void TerrainGenApp::PerformAction()
{
    switch (currentMode)
    {
    case TerrainGenApp::mode::PERLIN:    
		if (!meshVertices || !meshIndices)
        {
            meshVertices = new float[width * height * stride];
            meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
        }

        CheckChange();
        break;
    case TerrainGenApp::mode::PARAMETRIZED_GEN:
        if (!meshVertices || !meshIndices)
        {
			delete[] meshVertices;
			delete[] meshIndices;
        }
        if(TerraGenPerform)
		    FullTerrainGeneration();
        break;
    default:
        break;
    }
}

void TerrainGenApp::UpdatePrevCheckers() {
    prevCheck.prevCheckSum = noise.getConfigRef().getCheckSum();
    prevCheck.prevOpt = noise.getConfigRef().option;
    prevCheck.prevRidge = noise.getConfigRef().ridge;
    prevCheck.prevIsland = noise.getConfigRef().island;
    prevCheck.prevIslandType = noise.getConfigRef().islandType;
    prevCheck.symmetrical = noise.getConfigRef().symmetrical;
    prevCheck.seed = seed;
}

void TerrainGenApp::CheckChange() {
    if (prevCheck.prevOpt != noise.getConfigRef().option ||
        prevCheck.prevCheckSum != noise.getConfigRef().getCheckSum() ||
        prevCheck.prevRidge != noise.getConfigRef().ridge ||
        prevCheck.prevIsland != noise.getConfigRef().island ||
        prevCheck.prevIslandType != noise.getConfigRef().islandType ||
        prevCheck.symmetrical != noise.getConfigRef().symmetrical ||
        prevCheck.seed != seed || width != prevWidth || height != prevHeight)
    {
        bool first = false;
		if (width != prevWidth || height != prevHeight)
		{
            std::cout << "Resizing" << std::endl;
            delete[] meshVertices;
            delete[] meshIndices;
            meshVertices = new float[width * height * stride];
            meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];

			prevWidth = width;
			prevHeight = height;
            first = true;
            noise.setMapSize(width, height);
			noise.initMap();

            m_MainIndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
		}

        noise.setSeed(seed);
        utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", noise, meshVertices, meshIndices, m_Scaling_Factor, 8, true, first);
        utilities::PaintNotByTexture(meshVertices, width, height, stride, 6);
        UpdatePrevCheckers();

        m_MainVertexBuffer->UpdateData(meshVertices, (height * width) * stride * sizeof(float));
    }
}

void TerrainGenApp::DrawAdjacent(Renderer& renderer, glm::mat4& model) {
    //West
    m_MainShader->SetModel(glm::translate(model, glm::vec3(-1.0f, 0.0f, 0.0f)));
    renderer.Draw(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
    //East
    m_MainShader->SetModel(glm::translate(model, glm::vec3(1.0f, 0.0f, 0.0f)));
    renderer.Draw(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
    //North
    m_MainShader->SetModel(glm::translate(model, glm::vec3(0.0f, 0.0f, 1.0f)));
    renderer.Draw(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
    //South
    m_MainShader->SetModel(glm::translate(model, glm::vec3(0.0f, 0.0f, -1.0f)));
    renderer.Draw(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
}

void TerrainGenApp::perlinImgui() {
    //Seed
    ImGui::InputInt("Seed", &seed);

    //Basic noise settings
    ImGui::SliderInt("Octaves", &noise.getConfigRef().octaves, 1, 8);

    ImGui::SliderFloat("Offset x", &noise.getConfigRef().xoffset, 0.0f, 5.0f);
    ImGui::SliderFloat("Offset y", &noise.getConfigRef().yoffset, 0.0f, 5.0f);
    ImGui::SliderFloat("Scale", &noise.getConfigRef().scale, 0.01f, 3.0f);
    ImGui::SliderFloat("Constrast", &noise.getConfigRef().constrast, 0.1f, 2.0f);
    ImGui::SliderFloat("Redistribution", &noise.getConfigRef().redistribution, 0.1f, 10.0f);
    ImGui::SliderFloat("Lacunarity", &noise.getConfigRef().lacunarity, 0.1f, 10.0f);
    ImGui::SliderFloat("Persistance", &noise.getConfigRef().persistance, 0.1f, 1.0f);

    //Dealing with negatives settings
    static const char* options[] = { "REFIT_ALL", "FLATTEN_NEGATIVES", "REVERT_NEGATIVES", "NOTHING" };
    static int current_option = static_cast<int>(noise.getConfigRef().option);

    if (ImGui::BeginCombo("Negatives: ", options[current_option]))
    {
        for (int n = 0; n < IM_ARRAYSIZE(options); n++)
        {
            bool is_selected = (current_option == n);
            if (ImGui::Selectable(options[n], is_selected)) {
                current_option = n;
                noise.getConfigRef().option = static_cast<noise::Options>(n);
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (noise.getConfigRef().option == noise::Options::REVERT_NEGATIVES)
        ImGui::SliderFloat("Revert Gain", &noise.getConfigRef().revertGain, 0.1f, 1.0f);

    //Ridged noise settings
    ImGui::Checkbox("Ridge", &noise.getConfigRef().ridge);
    if (noise.getConfigRef().ridge)
    {
        ImGui::SliderFloat("Ridge Gain", &noise.getConfigRef().ridgeGain, 0.1f, 10.0f);
        ImGui::SliderFloat("Ridge Offset", &noise.getConfigRef().ridgeOffset, 0.1f, 10.0f);
    }

    //Island settings
    ImGui::Checkbox("Island", &noise.getConfigRef().island);
    if (noise.getConfigRef().island)
    {
        static const char* islandTypes[] = { "CONE", "DIAGONAL", "EUKLIDEAN_SQUARED",
                                             "SQUARE_BUMP","HYPERBOLOID", "SQUIRCLE",
                                             "TRIG" };
        static int current_island = static_cast<int>(noise.getConfigRef().islandType);

        if (ImGui::BeginCombo("Island type: ", islandTypes[current_island]))
        {
            for (int n = 0; n < IM_ARRAYSIZE(islandTypes); n++)
            {
                bool is_selected = (current_island == n);
                if (ImGui::Selectable(islandTypes[n], is_selected)) {
                    current_island = n;
                    noise.getConfigRef().islandType = static_cast<noise::IslandType>(n);
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Mix Power", &noise.getConfigRef().mixPower, 0.0f, 1.0f);
    }
    //Generating "symmetrical" noise and checkbox for miroring it on the four sides
    ImGui::Checkbox("Symmetrical", &noise.getConfigRef().symmetrical);
    if (noise.getConfigRef().symmetrical) {
        ImGui::Checkbox("Test Symmetrical", &testSymmetrical);
    }
}

void TerrainGenApp::parametrizedImGui()
{
	ImGui::Text("Terrain Generation Settings");
    if (ImGui::Button("Display terrain")) {
		isTerrainDisplayed = !isTerrainDisplayed;
    }
}

//Erosion
void TerrainGenApp::ErosionWindowRender() {
    ImGui::Text("Erosion Settings");

    ImGui::InputInt("Droplet count", &erosion.getDropletCountRef());
    ImGui::InputInt("Droplet lifetime", &erosion.getConfigRef().dropletLifetime);
    ImGui::InputFloat("Inertia", &erosion.getConfigRef().inertia);
    ImGui::InputFloat("Droplet init capacity", &erosion.getConfigRef().initialCapacity, 0.01f, 1.0f);
    ImGui::InputFloat("Droplet init velocity", &erosion.getConfigRef().initialVelocity, 0.0f, 1.0f);
    ImGui::InputFloat("Droplet init water", &erosion.getConfigRef().initialWater, 0.0f, 1.0f);
    ImGui::InputFloat("Erosion rate", &erosion.getConfigRef().erosionRate, 0.0f, 1.0f);
    ImGui::InputFloat("Deposition rate", &erosion.getConfigRef().depositionRate, 0.0f, 1.0f);
    ImGui::InputFloat("Evaporation rate", &erosion.getConfigRef().evaporationRate, 0.0f, 1.0f);
    ImGui::InputFloat("Gravity", &erosion.getConfigRef().gravity, 0.0f, 10.0f);
    ImGui::InputFloat("Min slope", &erosion.getConfigRef().minSlope, 0.0f, 1.0f);
    ImGui::InputInt("Erosion radius", &erosion.getConfigRef().erosionRadius);
    ImGui::InputFloat("Blur", &erosion.getConfigRef().blur, 0.0f, 1.0f);

    ImGui::Checkbox("Show traces of droplets", &trackDraw);

    if (ImGui::Button("Erode map")) {
        PerformErosion();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        erosionDraw = false;
        trackDraw = false;
    }
}

void TerrainGenApp::DeactivateErosion() {
    if (traceVertices)
    {
        delete[] traceVertices;
        traceVertices = nullptr;
    }
    if (erosionVertices)
    {
        delete[] erosionVertices;
        erosionVertices = nullptr;
    }
    trackDraw = false;
    erosionDraw = false;
}

void TerrainGenApp::PerformErosion() {
    //If tracks of droplets are drawn we need to allocate memory for the trace vertices
    if (trackDraw)
    {
        delete[] traceVertices;
        traceVertices = new float[(erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3];

        for (int i = 0; i < (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3; i++) {
            traceVertices[i] = 0.0f;
        }
        m_TrackBuffer = std::make_unique<VertexBuffer>(traceVertices, (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3 * sizeof(float));
    }

    //If erosion vertices are not allocated we need to allocate memory for them
    if (!erosionVertices) {
        erosionVertices = new float[width * height * stride];
    }

    erosion.SetMap(noise.getMap());
    utilities::benchmark_void(utilities::PerformErosion, "PerformErosion", erosionVertices, meshIndices, m_Scaling_Factor, trackDraw ? std::optional<float*>(traceVertices) : std::nullopt, stride, 0, 3, erosion);
    utilities::PaintNotByTexture(erosionVertices, width, height, stride, 6);
    m_erosionBuffer->UpdateData(erosionVertices, (height * width) * stride * sizeof(float));
    erosionDraw = true;
}

//Function drawing tracks of droplets on the eroded terrain mesh
void TerrainGenApp::PrintTrack(glm::mat4& model) {
    if (trackDraw && traceVertices) {
        m_TrackVAO->Bind();
        m_TrackBuffer->UpdateData(traceVertices, (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef() * 3 * sizeof(float));
        m_TrackShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

        m_TrackShader->Bind();
        m_TrackBuffer->Bind();

        GLCALL(glEnableVertexAttribArray(0));
        GLCALL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0));

        GLCALL(glPointSize(2.0f));
        GLCALL(glDrawArrays(GL_POINTS, 0, (erosion.getConfigRef().dropletLifetime + 1) * erosion.getDropletCountRef()));
    }
}

//Full terrain gen
void TerrainGenApp::FullTerrainGeneration()
{
    TerraGenPerform = false;
	delete[] t_MeshVertices;
	delete[] t_MeshIndices;

    t_MeshVertices = new float[(width - 1) * (height - 1) * stride * 4];
    t_MeshIndices = new unsigned int[(width - 1) * (height - 1) * 6];

    conditionalTerrainGeneration();

    //OpenGL setup for the mesh
    m_TerGenVAO = std::make_unique<VertexArray>();
    m_TerGenVertexBuffer = std::make_unique<VertexBuffer>(t_MeshVertices, (width - 1) * (height - 1) * 4 * stride * sizeof(float));
    m_TerGenIndexBuffer = std::make_unique<IndexBuffer>(t_MeshIndices, (width - 1) * (height - 1) * 6);
    m_TerGenShader = std::make_unique<Shader>("res/shaders/Lightning_final_vertex.shader", "res/shaders/Lightning_final_fragment.shader");
    m_TerGenTexture = std::make_unique<Texture>("res/textures/texture.png");

    m_TerGenVAO->AddBuffer(*m_TerGenVertexBuffer, layout);
}

void TerrainGenApp::conditionalTerrainGeneration() {
    terrainGen.setSize(width/m_ChunkResolution, height/m_ChunkResolution);
    terrainGen.setChunkResolution(m_ChunkResolution);
    terrainGen.setSeed(742);

    terrainGen.getContinentalnessNoiseConfig().constrast = 1.5f;
    terrainGen.getContinentalnessNoiseConfig().octaves = 7;
    terrainGen.getContinentalnessNoiseConfig().scale = samplingScale;

    terrainGen.getMountainousNoiseConfig().constrast = 1.5f;
    terrainGen.getMountainousNoiseConfig().scale = samplingScale;

    terrainGen.getPVNoiseConfig().constrast = 1.5f;
    terrainGen.getPVNoiseConfig().ridgeGain = 3.0f;
    terrainGen.getPVNoiseConfig().scale = samplingScale;

    terrainGen.initializeMap();
    terrainGen.setSplines({ {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 40.0 ,64.0, 66.0, 68.0, 70.0},	//Continentalness {X,Y}
                            {-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 5.0, 10.0, 20.0, 30.0, 80.0, 100.0, 170.0},	//Mountainousness {X,Y}
                            {-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} }); //PV {X,Y}

    std::vector<biome::Biome> biomes = {
        biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, m_ChunkResolution * m_ChunkResolution * 0.2f),
        biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, 2, m_ChunkResolution * m_ChunkResolution * 0.01f),
        biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, 7, m_ChunkResolution * m_ChunkResolution * 0.03f),
        biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, 8, m_ChunkResolution * m_ChunkResolution * 0.01f),
        biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, 0, m_ChunkResolution * m_ChunkResolution * 0.02f),
        biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, 5, m_ChunkResolution * m_ChunkResolution * 0.0f)
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

    seeLevel *= 0.4f;
    utilities::createTiledVertices(t_MeshVertices, width, height, terrainGen.getHeightMap(), 0.4f, stride, 0);
    utilities::createIndicesTiledField(t_MeshIndices, width, height);
    utilities::InitializeNormals(t_MeshVertices, stride, 3, (height - 1) * (width - 1) * 4);
    utilities::CalculateNormals(t_MeshVertices, t_MeshIndices, stride, 3, (width - 1) * (height - 1) * 6);
    utilities::NormalizeVector3f(t_MeshVertices, stride, 3, (height - 1) * (width - 1) * 4);
    utilities::AssignTexturesByBiomes(terrainGen, t_MeshVertices, width, height, 3, stride, 6);
    std::cout << "fine" << std::endl;
}

void TerrainGenApp::setTreeVertices()
{
    m_TreeShader = std::make_unique<Shader>("res/shaders/test_vertex.shader", "res/shaders/test_frag.shader");

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

    t_treesPositions = new float[3 * terrainGen.getTreeCount()];
    std::cout << terrainGen.getTreeCount() << std::endl;

    int index = 0;

    for (auto& it : terrainGen.getVegetationPoints()) {
        for (auto& it2 : it) {
            t_treesPositions[index++] = it2.first * scaleFactor;
            t_treesPositions[index++] = terrainGen.getHeightAt(it2.first, it2.second) * scaleFactor;
            t_treesPositions[index++] = it2.second * scaleFactor;
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * terrainGen.getTreeCount(), t_treesPositions, GL_STATIC_DRAW);

    glBindVertexArray(treeVAO);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
}
