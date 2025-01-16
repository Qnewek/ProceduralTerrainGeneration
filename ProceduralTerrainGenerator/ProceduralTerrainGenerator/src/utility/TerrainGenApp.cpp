#include "TerrainGenApp.h"
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "utilities.h"

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0), m_Scaling_Factor(0.4f), drawScale(1.0f),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(200.0f), leftPanelWidth(400.0f), 
width(800), height(800), prevHeight(800), prevWidth(800), tmpHeight(800), tmpWidth(800), stride(8), seed(123), deltaTime(0.0f), lastFrame(0.0f),
renderer(), player(1920, 1080, glm::vec3(0.0f, 0.0f, 0.0f), 0.0001f, 1.0f, false, height),
meshVertices(nullptr), meshIndices(nullptr), testSymmetrical(false), basicPerlinNoise(), noise(&basicPerlinNoise), layout(), currentMode(mode::PERLIN),
erosionWindow(false), trackDraw(false), erosionDraw(false), erosionVertices(nullptr), traceVertices(nullptr), erosion(width, height),
terrainGen(), t_MeshVertices(nullptr), t_MeshIndices(nullptr), t_treesPositions(nullptr), 
m_ChunkResolution(20), prevChunkRes(20), tmpChunkRes(20), seeLevel(64.0f), isTerrainDisplayed(true)
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
    noise->setMapSize(width, height);
	noise->setSeed(seed);
    noise->initMap();
    utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", *noise, meshVertices, meshIndices, m_Scaling_Factor, stride, true, true);
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
    player.SetPosition(glm::vec3(1.0f, 1.0f, 1.0f));

	initializeTerrainGeneration();

    m_TreeShader = std::make_unique<Shader>("res/shaders/test_vertex.shader", "res/shaders/test_frag.shader");

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

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------IMGUI-------------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::ImGuiRender()
{
    ImGui_ImplGlfwGL3_NewFrame();
    ImGui::SetNextWindowPos(ImVec2(windowWidth - rightPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, windowHeight-60), ImGuiCond_Always);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
    rightPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;
	ImGui::Text("Terrain settings");
	ImGui::SliderFloat("Size", &m_Scaling_Factor, 0.1f, 10.0f);
    ImGui::SliderFloat("Stretch", &drawScale, 0.1f, 100.0f);
	ImGui::SliderFloat("Camera speed", &player.GetCameraRef()->getSpeedRef(), 0.1f, 100.0f);
	if (!noiseEdit)
    {
        ImGui::Text("Resize map");
        ImGui::InputInt("Height", &tmpHeight);
        ImGui::InputInt("Width", &tmpWidth);
        if (currentMode == mode::PARAMETRIZED_GEN)
            ImGui::InputInt("Chunk Resolution", &tmpChunkRes);
        if (ImGui::Button("Resize"))
        {
            height = tmpHeight;
            width = tmpWidth;
            m_ChunkResolution = tmpChunkRes;
            ResizePerlin();
            resizeTerrainGeneration();
        }
        if (ImGui::Button("Save file")) {
            if (currentMode == mode::PARAMETRIZED_GEN)
                utilities::saveToObj("res/models/", "terrain", t_MeshVertices, t_MeshIndices, stride, (width - 1) * (height - 1) * 6, (width - 1) * (height - 1) * stride * 4, false);
            if (currentMode == mode::PERLIN)
                utilities::saveToObj("res/models/", "terrain", meshVertices, meshIndices, stride, (width - 1) * (height - 1) * 6, width * height * stride, false);
        }
    }
	ImGui::Separator();
    if (currentMode == mode::PERLIN)
        perlinImgui();
	if (currentMode == mode::PARAMETRIZED_GEN)
        parametrizedImGui();
    ImGui::Separator();
	if (erosionWindow)
		ErosionWindowRender();


    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, windowHeight-60), ImGuiCond_Always);
    ImGui::Begin("Parameters", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
    leftPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;
    if (noiseEdit) {
        ParameterImgui();
    }
    if (biomeEdit) {
        BiomeImGui();
    }
	if (treeEdit)
	{
		TreeTypesImGui();
	}
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, topPanelHeight), ImGuiCond_Always);
	ImGui::Begin("Mode", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
	ImGui::Text("Mode: ");
    ImGui::SameLine();
    if (ImGui::Button("Pure Perlin"))
    {
		currentMode = mode::PERLIN;
        noise = &basicPerlinNoise;
		noiseEdit = false;
		biomeEdit = false;
        seed = (*noise).getConfigRef().seed;
		noise->reseed();
        player.SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));
		player.setSpeed(1.0f);
        PerformPerlin();
    }
    ImGui::SameLine();
    if (ImGui::Button("Parametrized generation"))
    {
		currentMode = mode::PARAMETRIZED_GEN;
		TerraGenPerform = true;
		noiseEdit = false;
        player.SetPosition(glm::vec3(0.0f, 50.0f, 0.0f));
		player.setSpeed(40.0f);
    }    
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, windowHeight - bottomPanelHeight - 60), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, bottomPanelHeight), ImGuiCond_Always);
    ImGui::Begin("OutPut", nullptr, ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
	ImGui::Text("Press 'm' to enable rotation of the camera, press 'ESC' to release mouse");
    if (noiseEdit) {
		ImGui::Text("Editing %s noise...", editedNoise.c_str());
    }
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;
    
    ImGui::End();


    ImGui::Render();
    ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
}

void TerrainGenApp::perlinImgui() {
    //Seed
    if (ImGui::CollapsingHeader("Perlin Noise Settings")) {
        ImGui::InputInt("Seed", &seed);

        //Basic noise settings
        ImGui::SliderInt("Octaves", &noise->getConfigRef().octaves, 1, 8);

        ImGui::SliderFloat("Offset x", &noise->getConfigRef().xoffset, 0.0f, 5.0f);
        ImGui::SliderFloat("Offset y", &noise->getConfigRef().yoffset, 0.0f, 5.0f);
        ImGui::SliderFloat("Scale", &noise->getConfigRef().scale, 0.01f, 3.0f);
        ImGui::SliderFloat("Constrast", &noise->getConfigRef().constrast, 0.1f, 2.0f);
        ImGui::SliderFloat("Redistribution", &noise->getConfigRef().redistribution, 0.1f, 10.0f);
        ImGui::SliderFloat("Lacunarity", &noise->getConfigRef().lacunarity, 0.1f, 10.0f);
        ImGui::SliderFloat("Persistance", &noise->getConfigRef().persistance, 0.1f, 1.0f);

        //Dealing with negatives settings
        static const char* options[] = { "REFIT_ALL", "FLATTEN_NEGATIVES", "REVERT_NEGATIVES", "NOTHING" };
        static int current_option = static_cast<int>(noise->getConfigRef().option);

        if (ImGui::BeginCombo("Negatives: ", options[current_option]))
        {
            for (int n = 0; n < IM_ARRAYSIZE(options); n++)
            {
                bool is_selected = (current_option == n);
                if (ImGui::Selectable(options[n], is_selected)) {
                    current_option = n;
                    noise->getConfigRef().option = static_cast<noise::Options>(n);
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (noise->getConfigRef().option == noise::Options::REVERT_NEGATIVES)
            ImGui::SliderFloat("Revert Gain", &noise->getConfigRef().revertGain, 0.1f, 1.0f);

        //Ridged noise settings
        ImGui::Checkbox("Ridge", &noise->getConfigRef().ridge);
        if (noise->getConfigRef().ridge)
        {
            ImGui::SliderFloat("Ridge Gain", &noise->getConfigRef().ridgeGain, 0.1f, 10.0f);
            ImGui::SliderFloat("Ridge Offset", &noise->getConfigRef().ridgeOffset, 0.1f, 10.0f);
        }

        //Island settings
        ImGui::Checkbox("Island", &noise->getConfigRef().island);
        if (noise->getConfigRef().island)
        {
            static const char* islandTypes[] = { "CONE", "DIAGONAL", "EUKLIDEAN_SQUARED",
                                                 "SQUARE_BUMP","HYPERBOLOID", "SQUIRCLE",
                                                 "TRIG" };
            static int current_island = static_cast<int>(noise->getConfigRef().islandType);

            if (ImGui::BeginCombo("Island type: ", islandTypes[current_island]))
            {
                for (int n = 0; n < IM_ARRAYSIZE(islandTypes); n++)
                {
                    bool is_selected = (current_island == n);
                    if (ImGui::Selectable(islandTypes[n], is_selected)) {
                        current_island = n;
                        noise->getConfigRef().islandType = static_cast<noise::IslandType>(n);
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Mix Power", &noise->getConfigRef().mixPower, 0.0f, 1.0f);
        }
        //Generating "symmetrical" noise and checkbox for miroring it on the four sides
        ImGui::Checkbox("Symmetrical", &noise->getConfigRef().symmetrical);
        if (noise->getConfigRef().symmetrical) {
            ImGui::Checkbox("Test Symmetrical", &testSymmetrical);
        }

        if (noiseEdit) {
            if (ImGui::Button("Save config")) {
                currentMode = mode::PARAMETRIZED_GEN;
                TerraGenPerform = true;
                noiseEdit = false;
                player.SetPosition(glm::vec3(0.0f, 80.0f, 0.0f));
                player.setSpeed(40.0f);
            }
        }
        if (!noiseEdit) {
            if (ImGui::Button("Erode"))
            {
                erosionWindow = !erosionWindow;
                if (!erosionWindow)
                    DeactivateErosion();
            }
        }
    }
}

void TerrainGenApp::parametrizedImGui()
{
    if (ImGui::CollapsingHeader("Terrain Generation Settings")) {
        if (ImGui::Button("Display terrain")) {
            isTerrainDisplayed = !isTerrainDisplayed;
        }
        ImGui::Separator();
        ImGui::Text("Noise configuration");
        if (ImGui::Button("Continentalness")) {
            SwapNoise(&terrainGen.getContinentalnessNoise());
            editedNoise = "Continentalness";
            editedType = 'c';
        }
        ImGui::SameLine();
        if (ImGui::Button("Mountainousness")) {
            SwapNoise(&terrainGen.getMountainousNoise());
            editedNoise = "Mountainousness";
            editedType = 'm';
        }
        ImGui::SameLine();
        if (ImGui::Button("Peaks&Valeys")) {
            SwapNoise(&terrainGen.getPVNoise());
            editedNoise = "Peaks&Valeys";
            editedType = 'p';
        }
        if (ImGui::Button("Temperature")) {
            SwapNoise(&terrainGen.getTemperatureNoise());
            editedNoise = "Temperature";
            editedType = 't';
        }
        ImGui::SameLine();
        if (ImGui::Button("Humidity")) {
            SwapNoise(&terrainGen.getHumidityNoise());
            editedNoise = "Humidity";
            editedType = 'h';
        }
        ImGui::Spacing();
        
        ImGui::Separator();
        ImGui::Text("Vegetation settings");
        if (ImGui::Button("Vegetation generation"))
        {
            if (!t_treesPositions) {
                PrepareTreesDraw();
            }
            drawTrees = true;
        }
        if (ImGui::Button("Biomes settings")) {
            biomeEdit = !biomeEdit;
        }
        if (ImGui::Button("Generate terrain"))
        {
            TerraGenPerform = true;
            player.SetPosition(glm::vec3(0.0f, 80.0f, 0.0f));
        }
        /*
        if (ImGui::Button("erode")) {
            PerformErosion();
        }
        */
    }
}

void TerrainGenApp::ErosionWindowRender() {
    if(ImGui::CollapsingHeader("Erosion Settings"))
    {
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
}

void TerrainGenApp::ParameterImgui()
{
    if (ImGui::CollapsingHeader("Edit spline settings")) {
        int in = 0;
        if (editedType == 'm')
            in = 2;
        if (editedType == 'p')
            in = 4;

        for (size_t i = 0; i < splines[in].size(); i++)
        {
            float arg = static_cast<float>(splines[in][i]);
            float value = static_cast<float>(splines[in + 1][i]);
            float min = i > 0 ? splines[in][i - 1] : -1.0f;
            float max = i < splines[in].size() - 1 ? splines[in][i + 1] : 1.0f;

            ImGui::Separator();
            ImGui::Text("Point %d", i);
            ImGui::SliderFloat(("X" + std::to_string(i)).c_str(), &arg, min, max);
            ImGui::InputFloat(("Y" + std::to_string(i)).c_str(), &value, 0.01f, 0.1f);

            splines[in][i] = static_cast<double>(arg);
            splines[in + 1][i] = static_cast<double>(value);
        }
        ImGui::Separator();
        if (ImGui::Button("+", ImVec2(50, 30))) {
            splines[in].push_back(1.0f);
            splines[in + 1].push_back(splines[in + 1].back());
        }
        if (ImGui::Button("Set splines")) {
            terrainGen.setSpline(editedType, { splines[in], splines[in + 1] });
            std::cout << "[LOG] New spline set" << std::endl;
        }
    }
    if (editedType != 'p' && ImGui::CollapsingHeader("Edit biome range for current noise")) {
        int in = 0;
        if (editedType == 'h')
            in = 1;
        if (editedType == 'c')
            in = 2;
        if (editedType == 'm')
			in = 3;

        for (int i = 0; i < ranges[in].size(); i++) {
			ImGui::Separator();
			ImGui::Text("Level %d", ranges[in][i].level);
			ImGui::LabelText(("Min" + std::to_string(ranges[in][i].level)).c_str(), "%f", ranges[in][i].min);
			ImGui::SliderFloat(("Max" + std::to_string(ranges[in][i].level)).c_str(), &ranges[in][i].max, -1.0f, 1.0f);

            if (ranges[in][i].min > ranges[in][i].max)
                ranges[in][i].max = ranges[in][i].min;
			if (i < ranges[in].size() - 1 && ranges[in][i].max != ranges[in][i + 1].min) {
				ranges[in][i + 1].min = ranges[in][i].max;
			}
			
        }
        ImGui::Separator();
        if (ImGui::Button("+", ImVec2(50, 30))) {
			ranges[in].push_back({ 1.0f, 1.0f, ranges[in].back().level+1});
        }
        if (ImGui::Button("Set ranges")) {
			terrainGen.setRange(editedType, ranges[in]);
            std::cout << "[LOG] New ranges set" << std::endl;
        }
    }
     
}

void TerrainGenApp::BiomeImGui()
{
    if (ImGui::CollapsingHeader("Biomes")) {
        for (auto& it : biomes) {
            if (ImGui::CollapsingHeader(it.getName().c_str())) {
                ImGui::Separator();
                ImGui::Text("Biome %d", it.getIdRef());;
                ImGui::PushItemWidth(100);
                ImGui::SliderInt("TextureOffset", &it.getTexOffsetRef(), 0, 9);

                ImGui::Text("Ranges levels");
                ImGui::Text("Continentalness");
                ImGui::SliderInt(("Minc" + std::to_string(it.getId())).c_str(), &it.getContinentalnessLevelRef().x, 0, ranges[2].size() - 1);
                if (it.getContinentalnessLevel().x > it.getContinentalnessLevel().y)
                    it.getContinentalnessLevelRef().y = it.getContinentalnessLevel().x;

                ImGui::SameLine();
                ImGui::SliderInt(("Maxc" + std::to_string(it.getId())).c_str(), &it.getContinentalnessLevelRef().y, 0, ranges[2].size() - 1);
                if (it.getContinentalnessLevel().x > it.getContinentalnessLevel().y)
                    it.getContinentalnessLevelRef().x = it.getContinentalnessLevel().y;

                ImGui::Text("Mountainousness");
                ImGui::SliderInt(("Minm" + std::to_string(it.getId())).c_str(), &it.getHumidityLevelRef().x, 0, ranges[3].size() - 1);
                if (it.getMountainousnessLevel().x > it.getMountainousnessLevel().y)
                    it.getMountainousnessLevelRef().y = it.getMountainousnessLevel().x;
                ImGui::SameLine();
                ImGui::SliderInt(("Maxm" + std::to_string(it.getId())).c_str(), &it.getMountainousnessLevelRef().y, 0, ranges[3].size() - 1);
                if (it.getMountainousnessLevel().x > it.getMountainousnessLevel().y)
                    it.getMountainousnessLevelRef().x = it.getMountainousnessLevel().y;

                ImGui::Text("Temperature");
                ImGui::SliderInt(("Mint" + std::to_string(it.getId())).c_str(), &it.getTemperatureLevelRef().x, 0, ranges[0].size() - 1);
                if (it.getTemperatureLevel().x > it.getTemperatureLevel().y)
                    it.getTemperatureLevelRef().y = it.getTemperatureLevel().x;
                ImGui::SameLine();
                ImGui::SliderInt(("Maxt" + std::to_string(it.getId())).c_str(), &it.getTemperatureLevelRef().y, 0, ranges[0].size() - 1);
                if (it.getTemperatureLevel().x > it.getTemperatureLevel().y)
                    it.getTemperatureLevelRef().x = it.getTemperatureLevel().y;

                ImGui::Text("Humidity");
                ImGui::SliderInt(("Minh" + std::to_string(it.getId())).c_str(), &it.getHumidityLevelRef().x, 0, ranges[1].size() - 1);
                if (it.getHumidityLevel().x > it.getHumidityLevel().y)
                    it.getHumidityLevelRef().y = it.getHumidityLevel().x;
                ImGui::SameLine();
                ImGui::SliderInt(("Maxh" + std::to_string(it.getId())).c_str(), &it.getHumidityLevelRef().y, 0, ranges[1].size() - 1);
                if (it.getHumidityLevel().x > it.getHumidityLevel().y)
                    it.getHumidityLevelRef().x = it.getHumidityLevel().y;

                ImGui::Text("Vegetation density");
				ImGui::SliderInt("", &it.getVegetationLevelRef(), 0, m_ChunkResolution);

                ImGui::PopItemWidth();
                if(ImGui::Button("Configure tree types")) {
                    treeEdit = true;
					biomeEdit = false;
                    biome = &it;
                }
            }
        }
        ImGui::Separator();
        if (ImGui::Button("+", ImVec2(50, 30))) {
            biomes.push_back(biome::Biome(biomes.size(), "Default", {1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, m_ChunkResolution * m_ChunkResolution * 0.2f));
        }
		if (ImGui::Button("Save biomes")) {
			terrainGen.setBiomes(biomes);
		}
    }
    
}

void TerrainGenApp::TreeTypesImGui()
{
    if (ImGui::CollapsingHeader("Tree types")) {
        for (int i = 0; i < biome->getTreeTypesRef().size(); i++) {
            ImGui::Separator();
			ImGui::Text("Tree type %d", i);
			ImGui::SliderInt("Probability", &biome->getTreeTypesRef()[i].y, 0, 100);
        }
        ImGui::Separator();
        if (ImGui::Button("+", ImVec2(50, 30))) {
            biome->getTreeTypesRef().push_back({ 0, 0 });
        }
		if (ImGui::Button("Save tree types")) {
            biomeEdit = true;
			treeEdit = false;
		}
    }
}

void TerrainGenApp::SwapNoise(noise::SimplexNoiseClass* n)
{
    currentMode = mode::PERLIN;
    noiseEdit = true;
	biomeEdit = false;
    noise = n;
    seed = noise->getConfigRef().seed;
	noise->reseed();
    player.SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));
    player.setSpeed(1.0f);
    PerlinChunked();
}

//-------------------------------------------------------------------------------------------------------
//--------------------------------------------DRAWING-SECTION--------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::Draw()
{
    glEnable(GL_SCISSOR_TEST);
    glViewport(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    glScissor(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    renderer.Clear(glm::vec3(0.37f, 0.77f, 1.0f));
    glm::mat4 model = glm::mat4(1.0f);

    if (currentMode == mode::PERLIN)
    {
        model = glm::translate(model, glm::vec3(1.0f, 0.8f, 2.0f));
        PerlinDraw(model);
    }
    else if (currentMode == mode::PARAMETRIZED_GEN) {
		model = glm::translate(model, glm::vec3(-0.5f, -0.5f, 0.0f));
        TerrainGenerationDraw(model);
        if (drawTrees) {

			DrawTrees(model);
        }
    }
    
    glDisable(GL_SCISSOR_TEST);
}

void TerrainGenApp::PerlinDraw(glm::mat4& model) {
    m_MainShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 8.0f);
    m_MainShader->SetLightUniforms(glm::vec3(0.5f, 2.0f, 0.5f), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    m_MainShader->SetViewPos((*player.GetCameraRef()).GetPosition());
    m_MainShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));
	m_MainShader->SetUniform1f("stretch", drawScale);
	m_MainShader->SetUniform1f("scale", m_Scaling_Factor);

    m_MainTexture->Bind();
    m_MainVAO->AddBuffer(*m_MainVertexBuffer, layout);
    renderer.DrawWithTexture(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
    if (testSymmetrical)
        DrawAdjacent(renderer, model);

    if (erosionDraw) {
        model = glm::translate(model, glm::vec3(drawScale * m_Scaling_Factor + 0.1f, 0.0f, 0.0f));

        m_MainVAO->AddBuffer(*m_erosionBuffer, layout);
        m_MainShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

        renderer.Draw(*m_MainVAO, *m_MainIndexBuffer, *m_MainShader);
        PrintTrack(model);
    }
}

void TerrainGenApp::TerrainGenerationDraw(glm::mat4& model) {
    m_TerGenShader->Bind();
    m_TerGenShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
    m_TerGenShader->SetLightUniforms(glm::vec3(0.5f * height, 255.0f, 0.5f * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    m_TerGenShader->SetViewPos((*player.GetCameraRef()).GetPosition());
    m_TerGenShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));
    m_TerGenShader->SetUniform1f("seeLevel", seeLevel * m_Scaling_Factor);
    m_TerGenShader->SetUniform1i("u_Texture", 0);
	m_TerGenShader->SetUniform1f("stretch", drawScale);
	m_TerGenShader->SetUniform1f("scale", m_Scaling_Factor);

	m_TerGenVAO->Bind();
    m_TerGenVAO->AddBuffer(*m_TerGenVertexBuffer, layout);
    m_TerGenTexture->Bind();
    if (isTerrainDisplayed)
        renderer.DrawWithTexture(*m_TerGenVAO, *m_TerGenIndexBuffer, *m_TerGenShader);
    if (erosionDraw) {
        model = glm::translate(model, glm::vec3(drawScale * m_Scaling_Factor * (width/prevChunkRes), 0.0f, 0.0f));

        m_TerGenVAO->AddBuffer(*m_erosionBuffer, layout);
        m_TerGenShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

        renderer.Draw(*m_TerGenVAO, *m_TerGenIndexBuffer, *m_TerGenShader);
        PrintTrack(model);
    }
}
void TerrainGenApp::DrawTrees(glm::mat4& model)
{
    m_TreeShader->Bind();
    m_TreeShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
    m_TreeShader->SetLightUniforms(glm::vec3(0.5f * height, 255.0f, 0.5f * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    m_TreeShader->SetViewPos((*player.GetCameraRef()).GetPosition());
    m_TreeShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

    glBindVertexArray(treeVAO);

    glDrawElementsInstanced(GL_TRIANGLES, treeIndicesCount, GL_UNSIGNED_INT, 0, terrainGen.getTreeCount());
    glBindVertexArray(0);
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

//-------------------------------------------------------------------------------------------------------
//---------------------------------------MAIN-LOGIC-FUNCTIONS--------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::PerformAction()
{
    switch (currentMode)
    {
    case TerrainGenApp::mode::PERLIN:    
        CheckChange();
        break;
    case TerrainGenApp::mode::PARAMETRIZED_GEN:
        if(TerraGenPerform)
		    FullTerrainGeneration();
        break;
    default:
        break;
    }
}

void TerrainGenApp::UpdatePrevCheckers() {
    prevCheck.prevCheckSum = noise->getConfigRef().getCheckSum();
    prevCheck.prevOpt = noise->getConfigRef().option;
    prevCheck.prevRidge = noise->getConfigRef().ridge;
    prevCheck.prevIsland = noise->getConfigRef().island;
    prevCheck.prevIslandType = noise->getConfigRef().islandType;
    prevCheck.symmetrical = noise->getConfigRef().symmetrical;
    prevCheck.seed = seed;
}

void TerrainGenApp::CheckChange() {
    if (prevCheck.prevOpt != noise->getConfigRef().option ||
        prevCheck.prevCheckSum != noise->getConfigRef().getCheckSum() ||
        prevCheck.prevRidge != noise->getConfigRef().ridge ||
        prevCheck.prevIsland != noise->getConfigRef().island ||
        prevCheck.prevIslandType != noise->getConfigRef().islandType ||
        prevCheck.symmetrical != noise->getConfigRef().symmetrical ||
        prevCheck.seed != seed)
    {
        noise->setSeed(seed);
		if(!noiseEdit)
            PerformPerlin();
        else
			PerlinChunked();
    }
}

void TerrainGenApp::ResizePerlin()
{
    std::cout << "Resizing" << std::endl;
    delete[] meshVertices;
    delete[] meshIndices;
    meshVertices = new float[width * height * stride];
    meshIndices = new unsigned int[(width - 1) * (height - 1) * 6];

    if (erosionVertices) {
		DeactivateErosion();
		erosion.Resize(width, height);
    }
    
    noise->setMapSize(width, height);
	noise->initMap();
    PerformPerlin();
	m_MainIndexBuffer = std::make_unique<IndexBuffer>(meshIndices, (width - 1) * (height - 1) * 6);
	m_MainVAO->AddBuffer(*m_MainVertexBuffer, layout);
}

void TerrainGenApp::PerformPerlin()
{
    utilities::benchmark_void(utilities::CreateTerrainMesh, "CreateTerrainMesh", *noise, meshVertices, meshIndices, m_Scaling_Factor, stride, true, true);
    utilities::PaintNotByTexture(meshVertices, width, height, stride, 6);

    UpdatePrevCheckers();
    m_MainVertexBuffer->Bind();
    m_MainVertexBuffer->UpdateData(meshVertices, (height * width) * stride * sizeof(float));
}

void TerrainGenApp::PerlinChunked()
{
	utilities::benchmark_void(utilities::GenerateTerrainMap, "GenerateTerrainMap", *noise, meshVertices, meshIndices, stride);
	utilities::PaintNotByTexture(meshVertices, width, height, stride, 6);
	UpdatePrevCheckers();
	m_MainVertexBuffer->Bind();
	m_MainVertexBuffer->UpdateData(meshVertices, (height * width) * stride * sizeof(float));
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

//-------------------------------------------------------------------------------------------------------
//---------------------------------------------EROSION---------------------------------------------------
//-------------------------------------------------------------------------------------------------------
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
        if(currentMode == mode::PERLIN)
            erosionVertices = new float[width * height * stride];
        else if(currentMode == mode::PARAMETRIZED_GEN)
        erosionVertices = new float[(width - 1) * (height - 1) * stride * 4];
    }
    if (currentMode == mode::PERLIN) {
        erosion.SetMap(noise->getMap());
        utilities::benchmark_void(utilities::PerformErosion, "PerformErosion", erosionVertices, meshIndices, m_Scaling_Factor, trackDraw ? std::optional<float*>(traceVertices) : std::nullopt, stride, 0, 3, erosion);
        utilities::PaintNotByTexture(erosionVertices, width, height, stride, 6);
	}
    else if (currentMode == mode::PARAMETRIZED_GEN) {
        
        erosion.SetMap(terrainGen.getHeightMap());
        erosion.Erode(trackDraw ? std::optional<float*>(traceVertices) : std::nullopt);

        utilities::createTiledVertices(erosionVertices, width, height, erosion.getMap(), m_Scaling_Factor, stride, 0);
        utilities::InitializeNormals(erosionVertices, stride, 3, (height - 1) * (width - 1) * 4);
        utilities::CalculateNormals(erosionVertices, t_MeshIndices, stride, 3, (width - 1) * (height - 1) * 6);
        utilities::NormalizeVector3f(erosionVertices, stride, 3, (height - 1) * (width - 1) * 4);
        utilities::AssignTexturesByBiomes(terrainGen, erosionVertices, width, height, 3, stride, 6);
    }

	m_erosionBuffer = std::make_unique<VertexBuffer>(erosionVertices, (height * width) * stride * sizeof(float));
    erosionDraw = true;
}

//-------------------------------------------------------------------------------------------------------
//---------------------------------------TERRAIN-GENERATION----------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::initializeTerrainGeneration() {
    terrainGen.setSize(width / m_ChunkResolution, height / m_ChunkResolution);
    terrainGen.setChunkResolution(m_ChunkResolution);

	terrainGen.getContinentalnessNoiseConfig().seed = 3;
    terrainGen.getContinentalnessNoiseConfig().constrast = 1.5f;
    terrainGen.getContinentalnessNoiseConfig().octaves = 7;
    terrainGen.getContinentalnessNoiseConfig().scale = samplingScale;

    terrainGen.getMountainousNoiseConfig().seed = 9;
    terrainGen.getMountainousNoiseConfig().constrast = 1.5f;
    terrainGen.getMountainousNoiseConfig().scale = samplingScale;

    terrainGen.getPVNoiseConfig().seed = 456;
    terrainGen.getPVNoiseConfig().constrast = 1.5f;
    terrainGen.getPVNoiseConfig().ridgeGain = 3.0f;
    terrainGen.getPVNoiseConfig().scale = samplingScale;

	terrainGen.getTemperatureNoiseConfig().seed = 123;
    terrainGen.getHumidityNoiseConfig().seed = 62;

    terrainGen.initializeMap();

    splines = { {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 40.0 ,64.0, 66.0, 68.0, 70.0},	//Continentalness {X,Y}
                {-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 5.0, 10.0, 20.0, 30.0, 80.0, 100.0, 170.0},	//Mountainousness {X,Y}
                {-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} };

    biomes = {
        biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, m_ChunkResolution * m_ChunkResolution * 0.2f),
        biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, 2, m_ChunkResolution * m_ChunkResolution * 0.01f),
        biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, 7, m_ChunkResolution * m_ChunkResolution * 0.03f),
        biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, 8, m_ChunkResolution * m_ChunkResolution * 0.01f),
        biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, 0, m_ChunkResolution * m_ChunkResolution * 0.02f),
        biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, 5, m_ChunkResolution * m_ChunkResolution * 0.0f)
    };

    ranges = {
        {{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
        {{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
        {{-1.0f, -0.7f, 0},{-0.7f, -0.2f, 1},{ -0.2f, 0.03f, 2},{0.03f, 0.3f, 3},{0.3f, 1.1f, 4}},
        {{-1.0f, -0.78f, 0},{-0.78f, -0.37f, 1},{-0.37f, -0.2f, 2},{-0.2f, 0.05f, 3},{0.05f, 0.45f, 4},{0.45f, 0.55f, 5},{0.55f, 1.1f, 6}}
    };

    terrainGen.setSplines(splines);
    terrainGen.setBiomes(biomes);
    terrainGen.setRanges(ranges);
}
void TerrainGenApp::resizeTerrainGeneration()
{
    if (width != prevWidth || height != prevHeight || m_ChunkResolution != prevChunkRes) {
        width = width + (m_ChunkResolution - (width % m_ChunkResolution));
		height = height + (m_ChunkResolution - (height % m_ChunkResolution));
        terrainGen.setSize(width / m_ChunkResolution , height / m_ChunkResolution);
        terrainGen.setChunkResolution(m_ChunkResolution);
        terrainGen.initializeMap();
        prevWidth = width;
        prevHeight = height;
		prevChunkRes = m_ChunkResolution;
        delete[] t_MeshVertices;
        delete[] t_MeshIndices;
        t_MeshIndices = nullptr;
        t_MeshVertices = nullptr;
		TerraGenPerform = true;
        FullTerrainGeneration();
		std::cout << "[LOG] Terrain resized" << std::endl;
    }
}
void TerrainGenApp::FullTerrainGeneration()
{
    TerraGenPerform = false;

    if (!t_MeshVertices || !t_MeshIndices)
    {
        t_MeshVertices = new float[(width - 1) * (height - 1) * stride * 4];
        t_MeshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
    }

    TerrainGeneration();

    //OpenGL setup for the mesh
    m_TerGenVAO = std::make_unique<VertexArray>();
    m_TerGenVertexBuffer = std::make_unique<VertexBuffer>(t_MeshVertices, (width - 1) * (height - 1) * 4 * stride * sizeof(float));
    m_TerGenIndexBuffer = std::make_unique<IndexBuffer>(t_MeshIndices, (width - 1) * (height - 1) * 6);
    m_TerGenShader = std::make_unique<Shader>("res/shaders/Lightning_final_vertex.shader", "res/shaders/Lightning_final_fragment.shader");
    m_TerGenTexture = std::make_unique<Texture>("res/textures/texture.png");

    m_TerGenVAO->AddBuffer(*m_TerGenVertexBuffer, layout);
}

void TerrainGenApp::TerrainGeneration() {
    if (!terrainGen.performTerrainGeneration()) {
        std::cout << "[ERROR] Map couldnt be generated" << std::endl;
        return;
    }

    utilities::createTiledVertices(t_MeshVertices, width, height, terrainGen.getHeightMap(), m_Scaling_Factor, stride, 0);
    utilities::createIndicesTiledField(t_MeshIndices, width, height);
    utilities::InitializeNormals(t_MeshVertices, stride, 3, (height - 1) * (width - 1) * 4);
    utilities::CalculateNormals(t_MeshVertices, t_MeshIndices, stride, 3, (width - 1) * (height - 1) * 6);
    utilities::NormalizeVector3f(t_MeshVertices, stride, 3, (height - 1) * (width - 1) * 4);
    utilities::AssignTexturesByBiomes(terrainGen, t_MeshVertices, width, height, 3, stride, 6);
    std::cout << "[LOG] Terrain generation completed!" << std::endl;
}

//-------------------------------------------------------------------------------------------------------
//--------------------------------------------VEGETATION-SECTION------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::PrepareTreesDraw()
{
    float treeVertices[] = {
        // Core 
        // Positions                          // Normals         // Color
        // Bottom side
        0.4f * m_Scaling_Factor, 0.00f * m_Scaling_Factor, 0.4f * m_Scaling_Factor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
        0.4f * m_Scaling_Factor, 0.00f * m_Scaling_Factor, 0.6f * m_Scaling_Factor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
        0.6f * m_Scaling_Factor, 0.00f * m_Scaling_Factor, 0.6f * m_Scaling_Factor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
        0.6f * m_Scaling_Factor, 0.00f * m_Scaling_Factor, 0.4f * m_Scaling_Factor,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
        // top side
        0.4f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 0.4f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
        0.4f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 0.6f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
        0.6f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 0.6f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
        0.6f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 0.4f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
        // Tree crown
        0.0f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 0.0f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
        1.0f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 0.0f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
        1.0f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 1.0f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
        0.0f * m_Scaling_Factor, 0.25f * m_Scaling_Factor, 1.0f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
        0.5f * m_Scaling_Factor, 1.00f * m_Scaling_Factor, 0.5f * m_Scaling_Factor,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f
    };

    unsigned int treeIndices[] = {
        // Bottom 
        0, 1, 2,
        2, 3, 0,
        // Top 
        4, 5, 6,
        6, 7, 4,
        // Sides
        0, 1, 5,
        0, 5, 4,
        1, 2, 6,
        1, 6, 5,
        2, 3, 7,
        2, 7, 6,
        3, 0, 4,
        3, 4, 7,
        // Crown
        8, 9, 10,
        8, 10, 11,
        8, 9, 12,
        9, 10, 12,
        10, 11, 12,
        11, 8, 12
    };

    if (!terrainGen.vegetationGeneration())
        std::cout << "[LOG] Trees generation failed!" << std::endl;
    if (t_treesPositions) {
        delete[] t_treesPositions;
        t_treesPositions = nullptr;
    }
    t_treesPositions = new float[3 * terrainGen.getTreeCount()];
    std::cout << terrainGen.getTreeCount() << std::endl;

    int index = 0;

    for (auto& it : terrainGen.getVegetationPoints()) {
        for (auto& it2 : it) {
            t_treesPositions[index++] = it2.first * m_Scaling_Factor;
            t_treesPositions[index++] = terrainGen.getHeightAt(it2.first, it2.second) * m_Scaling_Factor;
            t_treesPositions[index++] = it2.second * m_Scaling_Factor;
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

