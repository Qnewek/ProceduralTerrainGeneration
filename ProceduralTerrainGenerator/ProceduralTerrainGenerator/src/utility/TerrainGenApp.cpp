#include "TerrainGenApp.h"
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "utilities.h"

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(200.0f), leftPanelWidth(400.0f), 
width(1000), height(1000), heightScale(255.0f), prevHeight(200), prevWidth(200), tmpHeight(200), tmpWidth(200),
stride(8), seed(123), deltaTime(0.0f), lastFrame(0.0f), renderer(),
camera(1920, 1080, glm::vec3(0.0f, heightScale/2.0f, 0.0f), 30.0f, 500.0f), tMeshVertices(nullptr), tMeshIndices(nullptr), treesPositions(nullptr), erosionVertices(nullptr), traceVertices(nullptr),
erosionWindow(false), testSymmetrical(false), trackDraw(false), erosionDraw(false), isTerrainDisplayed(true),
erosion(width, height), terrainGen(),currentMode(mode::PERLIN), chunkResolution(20), prevChunkRes(20), tmpChunkRes(20), seeLevel(64.0f), noiseGen()
{
    std::cout << "[LOG] Initialized" << std::endl;
}

TerrainGenApp::~TerrainGenApp()
{
	delete[] traceVertices;
	delete[] tMeshVertices;
	delete[] tMeshIndices;
	delete[] treesPositions;
}


int TerrainGenApp::Initialize()
{
	//Window & OpenGL initialization
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

    glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);

    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, true);
    ImGui::StyleColorsDark();

    //Logic initialization
	noiseGen.Initialize(height, width, seed, heightScale);

    //UpdatePrevCheckers();
	//InitializeTerrainGeneration();
   
    return 0;
}

void TerrainGenApp::Start()
{
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

		camera.SteerCamera(window, deltaTime, true);
		Draw();

		//ImGuiRender();
        //PerformAction();

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
	ImGui::SliderFloat("Size", &heightScale, 1.0f, 255.0f);
	ImGui::SliderFloat("Camera speed", &camera.GetSpeedRef(), 0.1f, 100.0f);
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
            chunkResolution = tmpChunkRes;
            ResizeTerrainGeneration();
        }
        if (ImGui::Button("Save file")) {
            /*if (currentMode == mode::PARAMETRIZED_GEN)
                utilities::saveToObj("res/models/", "terrain", tMeshVertices, tMeshIndices, stride, (width - 1) * (height - 1) * 6, (width - 1) * (height - 1) * stride * 4, false);
            if (currentMode == mode::PERLIN)
                utilities::saveToObj("res/models/", "terrain", meshVertices, meshIndices, stride, (width - 1) * (height - 1) * 6, width * height * stride, false);*/
        }
    }
	ImGui::Separator();
    if (currentMode == mode::PERLIN)
        PerlinImgui();
	if (currentMode == mode::PARAMETRIZED_GEN)
        ParametrizedImGui();
    ImGui::Separator();
	if (erosionWindow)
		ErosionWindowRender();


    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, windowHeight-60), ImGuiCond_Always);
    ImGui::Begin("Modify", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
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
		/*currentMode = mode::PERLIN;
        noise = &basicPerlinNoise;
		noiseEdit = false;
		biomeEdit = false;
        seed = (*noise).GetConfigRef().seed;
		noise->Reseed();
        player.SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));
		player.SetSpeed(1.0f);
        PerformPerlin();*/
    }
    ImGui::SameLine();
    if (ImGui::Button("Parametrized generation"))
    {
		currentMode = mode::PARAMETRIZED_GEN;
		terGenPerform = true;
		noiseEdit = false;
        camera.SetPosition(glm::vec3(0.0f, heightScale/2.0f, 0.0f));
		camera.SetSpeed(40.0f);
    }    
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, windowHeight - bottomPanelHeight - 60), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, bottomPanelHeight), ImGuiCond_Always);
    ImGui::Begin("OutPut", nullptr, ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
	ImGui::Text("Press 'm' to enable rotation of the camera, press 'ESC' to release mouse");
	ImGui::Text("Press 'w', 'a', 's', 'd' to move camera, 'space' to move up, 'ctrl' to move down");
	ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
    ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
    if (noiseEdit) {
		ImGui::Text("Editing %s noise...", editedNoise.c_str());
    }
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;
    
    ImGui::End();


    ImGui::Render();
    ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
}

void TerrainGenApp::PerlinImgui() {
    ////Seed
    //if (ImGui::CollapsingHeader("Perlin Noise Settings")) {
    //    ImGui::InputInt("Seed", &seed);

    //    //Basic noise settings
    //    ImGui::SliderInt("Octaves", &noise->GetConfigRef().octaves, 1, 8);

    //    ImGui::SliderFloat("Offset x", &noise->GetConfigRef().xoffset, 0.0f, 5.0f);
    //    ImGui::SliderFloat("Offset y", &noise->GetConfigRef().yoffset, 0.0f, 5.0f);
    //    ImGui::SliderFloat("Scale", &noise->GetConfigRef().scale, 0.01f, 3.0f);
    //    ImGui::SliderFloat("Constrast", &noise->GetConfigRef().constrast, 0.1f, 2.0f);
    //    ImGui::SliderFloat("Redistribution", &noise->GetConfigRef().redistribution, 0.1f, 10.0f);
    //    ImGui::SliderFloat("Lacunarity", &noise->GetConfigRef().lacunarity, 0.1f, 10.0f);
    //    ImGui::SliderFloat("Persistance", &noise->GetConfigRef().persistance, 0.1f, 1.0f);

    //    //Dealing with negatives settings
    //    static const char* options[] = { "REFIT_ALL", "FLATTEN_NEGATIVES", "REVERT_NEGATIVES", "NOTHING" };
    //    static int current_option = static_cast<int>(noise->GetConfigRef().option);

    //    if (ImGui::BeginCombo("Negatives: ", options[current_option]))
    //    {
    //        for (int n = 0; n < IM_ARRAYSIZE(options); n++)
    //        {
    //            bool is_selected = (current_option == n);
    //            if (ImGui::Selectable(options[n], is_selected)) {
    //                current_option = n;
    //                noise->GetConfigRef().option = static_cast<noise::Options>(n);
    //            }
    //            if (is_selected)
    //                ImGui::SetItemDefaultFocus();
    //        }
    //        ImGui::EndCombo();
    //    }
    //    if (noise->GetConfigRef().option == noise::Options::REVERT_NEGATIVES)
    //        ImGui::SliderFloat("Revert Gain", &noise->GetConfigRef().revertGain, 0.1f, 1.0f);

    //    //Ridged noise settings
    //    ImGui::Checkbox("Ridge", &noise->GetConfigRef().Ridge);
    //    if (noise->GetConfigRef().Ridge)
    //    {
    //        ImGui::SliderFloat("Ridge Gain", &noise->GetConfigRef().RidgeGain, 0.1f, 10.0f);
    //        ImGui::SliderFloat("Ridge Offset", &noise->GetConfigRef().RidgeOffset, 0.1f, 10.0f);
    //    }

    //    //Island settings
    //    ImGui::Checkbox("Island", &noise->GetConfigRef().island);
    //    if (noise->GetConfigRef().island)
    //    {
    //        static const char* islandTypes[] = { "CONE", "DIAGONAL", "EUKLIDEAN_SQUARED",
    //                                             "SQUARE_BUMP","HYPERBOLOID", "SQUIRCLE",
    //                                             "TRIG" };
    //        static int current_island = static_cast<int>(noise->GetConfigRef().islandType);

    //        if (ImGui::BeginCombo("Island type: ", islandTypes[current_island]))
    //        {
    //            for (int n = 0; n < IM_ARRAYSIZE(islandTypes); n++)
    //            {
    //                bool is_selected = (current_island == n);
    //                if (ImGui::Selectable(islandTypes[n], is_selected)) {
    //                    current_island = n;
    //                    noise->GetConfigRef().islandType = static_cast<noise::IslandType>(n);
    //                }
    //                if (is_selected)
    //                    ImGui::SetItemDefaultFocus();
    //            }
    //            ImGui::EndCombo();
    //        }
    //        ImGui::SliderFloat("Mix Power", &noise->GetConfigRef().mixPower, 0.0f, 1.0f);
    //    }
    //    //Generating "symmetrical" noise and checkbox for miroring it on the four sides
    //    ImGui::Checkbox("Symmetrical", &noise->GetConfigRef().symmetrical);
    //    if (noise->GetConfigRef().symmetrical) {
    //        ImGui::Checkbox("Test Symmetrical", &testSymmetrical);
    //    }

    //    if (noiseEdit) {
    //        if (ImGui::Button("Save config")) {
    //            currentMode = mode::PARAMETRIZED_GEN;
    //            terGenPerform = true;
    //            noiseEdit = false;
    //            player.SetPosition(glm::vec3(0.0f, 80.0f, 0.0f));
    //            player.SetSpeed(40.0f);
    //        }
    //    }
    //    if (!noiseEdit) {
    //        if (ImGui::Button("Erode"))
    //        {
    //            erosionWindow = !erosionWindow;
    //            if (!erosionWindow)
    //                DeactivateErosion();
    //        }
    //    }
    //}
}

void TerrainGenApp::ParametrizedImGui()
{
    if (ImGui::CollapsingHeader("Terrain Generation Settings")) {
        if (ImGui::Button("Display terrain")) {
            isTerrainDisplayed = !isTerrainDisplayed;
        }
        ImGui::Separator();
        ImGui::Text("Noise configuration");
        if (ImGui::Button("Continentalness")) {
            SwapNoise(&terrainGen.GetContinentalnessNoise());
            editedNoise = "Continentalness";
            editedType = 'c';
        }
        ImGui::SameLine();
        if (ImGui::Button("Mountainousness")) {
            SwapNoise(&terrainGen.GetMountainousNoise());
            editedNoise = "Mountainousness";
            editedType = 'm';
        }
        ImGui::SameLine();
        if (ImGui::Button("Peaks&Valeys")) {
            SwapNoise(&terrainGen.GetPVNoise());
            editedNoise = "Peaks&Valeys";
            editedType = 'p';
        }
        if (ImGui::Button("Temperature")) {
            SwapNoise(&terrainGen.GetTemperatureNoise());
            editedNoise = "Temperature";
            editedType = 't';
        }
        ImGui::SameLine();
        if (ImGui::Button("Humidity")) {
            SwapNoise(&terrainGen.GetHumidityNoise());
            editedNoise = "Humidity";
            editedType = 'h';
        }
        ImGui::Spacing();
        
        ImGui::Separator();
        ImGui::Text("Vegetation settings");
        if (ImGui::Button("Vegetation generation"))
        {
            if (!treesPositions) {
                PrepareTreesDraw();
            }
            drawTrees = true;
        }
        if (ImGui::Button("Biomes settings")) {
            biomeEdit = !biomeEdit;
        }
        if (ImGui::Button("Generate terrain"))
        {
            terGenPerform = true;
            camera.SetPosition(glm::vec3(0.0f, heightScale/2.0f, 0.0f));
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
        ImGui::InputInt("Droplet count", &erosion.GetDropletCountRef());
        ImGui::InputInt("Droplet lifetime", &erosion.GetConfigRef().dropletLifetime);
        ImGui::InputFloat("Inertia", &erosion.GetConfigRef().inertia);
        ImGui::InputFloat("Droplet init capacity", &erosion.GetConfigRef().initialCapacity, 0.01f, 1.0f);
        ImGui::InputFloat("Droplet init velocity", &erosion.GetConfigRef().initialVelocity, 0.0f, 1.0f);
        ImGui::InputFloat("Droplet init water", &erosion.GetConfigRef().initialWater, 0.0f, 1.0f);
        ImGui::InputFloat("Erosion rate", &erosion.GetConfigRef().erosionRate, 0.0f, 1.0f);
        ImGui::InputFloat("Deposition rate", &erosion.GetConfigRef().depositionRate, 0.0f, 1.0f);
        ImGui::InputFloat("Evaporation rate", &erosion.GetConfigRef().evaporationRate, 0.0f, 1.0f);
        ImGui::InputFloat("Gravity", &erosion.GetConfigRef().gravity, 0.0f, 10.0f);
        ImGui::InputFloat("Min slope", &erosion.GetConfigRef().minSlope, 0.0f, 1.0f);
        ImGui::InputInt("Erosion radius", &erosion.GetConfigRef().erosionRadius);
        ImGui::InputFloat("Blur", &erosion.GetConfigRef().blur, 0.0f, 1.0f);

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
            terrainGen.SetSpline(editedType, { splines[in], splines[in + 1] });
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
			terrainGen.SetRange(editedType, ranges[in]);
            std::cout << "[LOG] New ranges set" << std::endl;
        }
    }
     
}

void TerrainGenApp::BiomeImGui()
{
    if (ImGui::CollapsingHeader("Biomes")) {
        for (auto& it : biomes) {
            if (ImGui::CollapsingHeader(it.GetName().c_str())) {
                ImGui::Separator();
                ImGui::Text("Biome %d", it.GetIdRef());;
                ImGui::PushItemWidth(100);
                ImGui::SliderInt("TextureOffset", &it.GetTexOffsetRef(), 0, 9);

                ImGui::Text("Ranges levels");
                ImGui::Text("Continentalness");
                ImGui::SliderInt(("Minc" + std::to_string(it.GetId())).c_str(), &it.GetContinentalnessLevelRef().x, 0, ranges[2].size() - 1);
                if (it.GetContinentalnessLevel().x > it.GetContinentalnessLevel().y)
                    it.GetContinentalnessLevelRef().y = it.GetContinentalnessLevel().x;

                ImGui::SameLine();
                ImGui::SliderInt(("Maxc" + std::to_string(it.GetId())).c_str(), &it.GetContinentalnessLevelRef().y, 0, ranges[2].size() - 1);
                if (it.GetContinentalnessLevel().x > it.GetContinentalnessLevel().y)
                    it.GetContinentalnessLevelRef().x = it.GetContinentalnessLevel().y;

                ImGui::Text("Mountainousness");
                ImGui::SliderInt(("Minm" + std::to_string(it.GetId())).c_str(), &it.GetHumidityLevelRef().x, 0, ranges[3].size() - 1);
                if (it.GetMountainousnessLevel().x > it.GetMountainousnessLevel().y)
                    it.GetMountainousnessLevelRef().y = it.GetMountainousnessLevel().x;
                ImGui::SameLine();
                ImGui::SliderInt(("Maxm" + std::to_string(it.GetId())).c_str(), &it.GetMountainousnessLevelRef().y, 0, ranges[3].size() - 1);
                if (it.GetMountainousnessLevel().x > it.GetMountainousnessLevel().y)
                    it.GetMountainousnessLevelRef().x = it.GetMountainousnessLevel().y;

                ImGui::Text("Temperature");
                ImGui::SliderInt(("Mint" + std::to_string(it.GetId())).c_str(), &it.GetTemperatureLevelRef().x, 0, ranges[0].size() - 1);
                if (it.GetTemperatureLevel().x > it.GetTemperatureLevel().y)
                    it.GetTemperatureLevelRef().y = it.GetTemperatureLevel().x;
                ImGui::SameLine();
                ImGui::SliderInt(("Maxt" + std::to_string(it.GetId())).c_str(), &it.GetTemperatureLevelRef().y, 0, ranges[0].size() - 1);
                if (it.GetTemperatureLevel().x > it.GetTemperatureLevel().y)
                    it.GetTemperatureLevelRef().x = it.GetTemperatureLevel().y;

                ImGui::Text("Humidity");
                ImGui::SliderInt(("Minh" + std::to_string(it.GetId())).c_str(), &it.GetHumidityLevelRef().x, 0, ranges[1].size() - 1);
                if (it.GetHumidityLevel().x > it.GetHumidityLevel().y)
                    it.GetHumidityLevelRef().y = it.GetHumidityLevel().x;
                ImGui::SameLine();
                ImGui::SliderInt(("Maxh" + std::to_string(it.GetId())).c_str(), &it.GetHumidityLevelRef().y, 0, ranges[1].size() - 1);
                if (it.GetHumidityLevel().x > it.GetHumidityLevel().y)
                    it.GetHumidityLevelRef().x = it.GetHumidityLevel().y;

                ImGui::Text("Vegetation density");
				ImGui::SliderInt("", &it.GetVegetationLevelRef(), 0, chunkResolution);

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
            biomes.push_back(biome::Biome(biomes.size(), "Default", {1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, chunkResolution * chunkResolution * 0.2f));
        }
		if (ImGui::Button("Save biomes")) {
			terrainGen.SetBiomes(biomes);
		}
    }
    
}

void TerrainGenApp::TreeTypesImGui()
{
    if (ImGui::CollapsingHeader("Tree types")) {
        for (int i = 0; i < biome->GetTreeTypesRef().size(); i++) {
            ImGui::Separator();
			ImGui::Text("Tree type %d", i);
			ImGui::SliderInt("Probability", &biome->GetTreeTypesRef()[i].y, 0, 100);
        }
        ImGui::Separator();
        if (ImGui::Button("+", ImVec2(50, 30))) {
            biome->GetTreeTypesRef().push_back({ 0, 0 });
        }
		if (ImGui::Button("Save tree types")) {
            biomeEdit = true;
			treeEdit = false;
		}
    }
}

void TerrainGenApp::SwapNoise(noise::SimplexNoiseClass* n)
{
    /*currentMode = mode::PERLIN;
    noiseEdit = true;
	biomeEdit = false;
    noise = n;
    seed = noise->GetConfigRef().seed;
	noise->Reseed();
    player.SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));
    player.SetSpeed(1.0f);
    PerlinChunked();*/
}

//-------------------------------------------------------------------------------------------------------
//--------------------------------------------DRAWING-SECTION--------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::Draw()
{
    glEnable(GL_SCISSOR_TEST);
    glViewport(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    glScissor(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
	renderer.Clear(glm::vec3(0.0f, 0.0f, 0.0f));
    glm::mat4 model = glm::mat4(1.0f);

    if (currentMode == mode::PERLIN)
    {
        noiseGen.Draw(model, renderer, camera);
    }
    else if (currentMode == mode::PARAMETRIZED_GEN) {
        TerrainGenerationDraw(model);
        if (drawTrees) {
			DrawTrees(model);
        }
    }
    
    glDisable(GL_SCISSOR_TEST);
}

void TerrainGenApp::TerrainGenerationDraw(glm::mat4& model) {
    /*terGenShader->Bind();
    terGenShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
    terGenShader->SetLightUniforms(glm::vec3(0.5f * height, 255.0f, 0.5f * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    terGenShader->SetViewPos((*player.GetCameraRef()).GetPosition());
    terGenShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));
    terGenShader->SetUniform1f("seeLevel", seeLevel * heightScale);
    terGenShader->SetUniform1i("u_Texture", 0);
	terGenShader->SetUniform1f("stretch", drawScale);
	terGenShader->SetUniform1f("scale", heightScale);

	terGenVAO->Bind();
    terGenVAO->AddBuffer(*terGenVertexBuffer, layout);
    terGenTexture->Bind();
    if (isTerrainDisplayed)
        renderer.Draw(*terGenVAO, *terGenIndexBuffer, *terGenShader);
    if (erosionDraw) {
        model = glm::translate(model, glm::vec3(drawScale * heightScale * (width/prevChunkRes), 0.0f, 0.0f));

        terGenVAO->AddBuffer(*erosionVertexBuffer, layout);
        terGenShader->SetMVP(model, *(player.GetCameraRef()->GetViewMatrix()), *(player.GetCameraRef()->GetProjectionMatrix()));

        renderer.Draw(*terGenVAO, *terGenIndexBuffer, *terGenShader);
        PrintTrack(model);
    }*/
}
void TerrainGenApp::DrawTrees(glm::mat4& model)
{
    treeShader->Bind();
    treeShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
    treeShader->SetLightUniforms(glm::vec3(0.5f * height, 255.0f, 0.5f * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    treeShader->SetViewPos(camera.GetPosition());
    treeShader->SetMVP(model, *(camera.GetViewMatrix()), *(camera.GetProjectionMatrix()));
    treeShader->SetUniform1f("scale", heightScale);

    glBindVertexArray(treeVAO);
    glDrawElementsInstanced(GL_TRIANGLES, treeIndicesCount, GL_UNSIGNED_INT, 0, terrainGen.GetTreeCount());
    glBindVertexArray(0);
}

//Function drawing tracks of droplets on the eroded terrain mesh
void TerrainGenApp::PrintTrack(glm::mat4& model) {
    if (trackDraw && traceVertices) {
        erosionTrackVAO->Bind();
        erosionTrackBuffer->UpdateData(traceVertices, (erosion.GetConfigRef().dropletLifetime + 1) * erosion.GetDropletCountRef() * 3 * sizeof(float));
        erosionTrackShader->SetMVP(model, *(camera.GetViewMatrix()), *(camera.GetProjectionMatrix()));

        erosionTrackShader->Bind();
        erosionTrackBuffer->Bind();

        GLCALL(glEnableVertexAttribArray(0));
        GLCALL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0));

        GLCALL(glPointSize(2.0f));
        GLCALL(glDrawArrays(GL_POINTS, 0, (erosion.GetConfigRef().dropletLifetime + 1) * erosion.GetDropletCountRef()));
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
        if(terGenPerform)
		    FullTerrainGeneration();
        break;
    default:
        break;
    }
}

void TerrainGenApp::UpdatePrevCheckers() {
   /* prevCheck.prevCheckSum = noise->GetConfigRef().getCheckSum();
    prevCheck.prevOpt = noise->GetConfigRef().option;
    prevCheck.prevRidge = noise->GetConfigRef().Ridge;
    prevCheck.prevIsland = noise->GetConfigRef().island;
    prevCheck.prevIslandType = noise->GetConfigRef().islandType;
    prevCheck.symmetrical = noise->GetConfigRef().symmetrical;
    prevCheck.seed = seed;*/
}

void TerrainGenApp::CheckChange() {
    /*if (prevCheck.prevOpt != noise->GetConfigRef().option ||
        prevCheck.prevCheckSum != noise->GetConfigRef().getCheckSum() ||
        prevCheck.prevRidge != noise->GetConfigRef().Ridge ||
        prevCheck.prevIsland != noise->GetConfigRef().island ||
        prevCheck.prevIslandType != noise->GetConfigRef().islandType ||
        prevCheck.symmetrical != noise->GetConfigRef().symmetrical ||
        prevCheck.seed != seed)
    {
        noise->SetSeed(seed);
		if(!noiseEdit)
            PerformPerlin();
        else
			PerlinChunked();
    }*/
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
 //   //If tracks of droplets are drawn we need to allocate memory for the trace vertices
 //   if (trackDraw)
 //   {
 //       delete[] traceVertices;
 //       traceVertices = new float[(erosion.GetConfigRef().dropletLifetime + 1) * erosion.GetDropletCountRef() * 3];

 //       for (int i = 0; i < (erosion.GetConfigRef().dropletLifetime + 1) * erosion.GetDropletCountRef() * 3; i++) {
 //           traceVertices[i] = 0.0f;
 //       }
 //       erosionTrackBuffer = std::make_unique<VertexBuffer>(traceVertices, (erosion.GetConfigRef().dropletLifetime + 1) * erosion.GetDropletCountRef() * 3 * sizeof(float));
 //   }

 //   //If erosion vertices are not allocated we need to allocate memory for them
 //   if (!erosionVertices) {
 //       if(currentMode == mode::PERLIN)
 //           erosionVertices = new float[width * height * stride];
 //       else if(currentMode == mode::PARAMETRIZED_GEN)
 //       erosionVertices = new float[(width - 1) * (height - 1) * stride * 4];
 //   }
 //   if (currentMode == mode::PERLIN) {
 //       erosion.SetMap(noise->GetMap());
 //       utilities::benchmarkVoid(utilities::PerformErosion, "PerformErosion", erosionVertices, meshIndices, heightScale, trackDraw ? std::optional<float*>(traceVertices) : std::nullopt, stride, 0, 3, erosion);
 //       utilities::PaintNotByTexture(erosionVertices, width, height, stride, 6);
	//}
 //   else if (currentMode == mode::PARAMETRIZED_GEN) {
 //       
 //       erosion.SetMap(terrainGen.GetHeightMap());
 //       erosion.Erode(trackDraw ? std::optional<float*>(traceVertices) : std::nullopt);
 //       /*
 //       utilities::CreateTiledVertices(erosionVertices, width, height, erosion.GetMap(), heightScale, stride, 0);
 //       utilities::InitializeNormals(erosionVertices, stride, 3, (height - 1) * (width - 1) * 4);
 //       utilities::CalculateNormals(erosionVertices, tMeshIndices, stride, 3, (width - 1) * (height - 1) * 6);
 //       utilities::NormalizeVector3f(erosionVertices, stride, 3, (height - 1) * (width - 1) * 4);
 //       utilities::AssignTexturesByBiomes(terrainGen, erosionVertices, width, height, 3, stride, 6);*/
 //   }

	//erosionVertexBuffer = std::make_unique<VertexBuffer>(erosionVertices, (height * width) * stride * sizeof(float));
 //   erosionDraw = true;
}

//-------------------------------------------------------------------------------------------------------
//---------------------------------------TERRAIN-GENERATION----------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::InitializeTerrainGeneration() {
 //   terrainGen.SetSize(width / chunkResolution, height / chunkResolution);
 //   terrainGen.SetChunkResolution(chunkResolution);

	//terrainGen.GetContinentalnessNoiseConfig().seed = 3;
 //   terrainGen.GetContinentalnessNoiseConfig().constrast = 1.5f;
 //   terrainGen.GetContinentalnessNoiseConfig().octaves = 7;
 //   terrainGen.GetContinentalnessNoiseConfig().scale = samplingScale;

 //   terrainGen.GetMountainousNoiseConfig().seed = 9;
 //   terrainGen.GetMountainousNoiseConfig().constrast = 1.5f;
 //   terrainGen.GetMountainousNoiseConfig().scale = samplingScale;

 //   terrainGen.GetPVNoiseConfig().seed = 456;
 //   terrainGen.GetPVNoiseConfig().constrast = 1.5f;
 //   terrainGen.GetPVNoiseConfig().RidgeGain = 3.0f;
 //   terrainGen.GetPVNoiseConfig().scale = samplingScale;

	//terrainGen.GetTemperatureNoiseConfig().seed = 123;
 //   terrainGen.GetHumidityNoiseConfig().seed = 62;

 //   terrainGen.InitializeMap();

 //   splines = { {-1.0, -0.7, -0.2, 0.03, 0.3, 1.0}, {0.0, 40.0 ,64.0, 66.0, 68.0, 70.0},	//Continentalness {X,Y}
 //               {-1.0, -0.78, -0.37, -0.2, 0.05, 0.45, 0.55, 1.0}, {0.0, 5.0, 10.0, 20.0, 30.0, 80.0, 100.0, 170.0},	//Mountainousness {X,Y}
 //               {-1.0, -0.85, -0.6, 0.2, 0.7, 1.0}, {1.0, 0.7, 0.4, 0.2, 0.05, 0} };

 //   biomes = {
 //       biome::Biome(0, "Grassplains",	{1, 2}, {1, 4}, {3, 5}, {0, 3}, 3, chunkResolution * chunkResolution * 0.2f),
 //       biome::Biome(1, "Desert",		{2, 4}, {0, 1}, {3, 5}, {0, 4}, 2, chunkResolution * chunkResolution * 0.01f),
 //       biome::Biome(2, "Snow",			{0, 1}, {0, 4}, {3, 5}, {0, 4}, 7, chunkResolution * chunkResolution * 0.03f),
 //       biome::Biome(3, "Sand",			{0, 4}, {0, 4}, {2, 3}, {0, 7}, 8, chunkResolution * chunkResolution * 0.01f),
 //       biome::Biome(4, "Mountain",		{0, 4}, {0, 4}, {4, 5}, {4, 7}, 0, chunkResolution * chunkResolution * 0.02f),
 //       biome::Biome(5, "Ocean",		{0, 4}, {0, 4}, {0, 2}, {0, 7}, 5, chunkResolution * chunkResolution * 0.0f)
 //   };

 //   ranges = {
 //       {{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
 //       {{-1.0f, -0.5f, 0},{-0.5f, 0.0f, 1},{0.0f, 0.5f, 2},{0.5f, 1.1f, 3}},
 //       {{-1.0f, -0.7f, 0},{-0.7f, -0.2f, 1},{ -0.2f, 0.03f, 2},{0.03f, 0.3f, 3},{0.3f, 1.1f, 4}},
 //       {{-1.0f, -0.78f, 0},{-0.78f, -0.37f, 1},{-0.37f, -0.2f, 2},{-0.2f, 0.05f, 3},{0.05f, 0.45f, 4},{0.45f, 0.55f, 5},{0.55f, 1.1f, 6}}
 //   };

 //   terrainGen.SetSplines(splines);
 //   terrainGen.SetBiomes(biomes);
 //   terrainGen.SetRanges(ranges);
}
void TerrainGenApp::ResizeTerrainGeneration()
{
    /*if (width != prevWidth || height != prevHeight || chunkResolution != prevChunkRes) {
        width = width + (chunkResolution - (width % chunkResolution));
		height = height + (chunkResolution - (height % chunkResolution));
        terrainGen.SetSize(width / chunkResolution , height / chunkResolution);
        terrainGen.SetChunkResolution(chunkResolution);
        terrainGen.InitializeMap();
        prevWidth = width;
        prevHeight = height;
		prevChunkRes = chunkResolution;
        delete[] tMeshVertices;
        delete[] tMeshIndices;
        tMeshIndices = nullptr;
        tMeshVertices = nullptr;
		terGenPerform = true;
        FullTerrainGeneration();
		std::cout << "[LOG] Terrain resized" << std::endl;
    }*/
}
void TerrainGenApp::FullTerrainGeneration()
{
    //terGenPerform = false;

    //if (!tMeshVertices || !tMeshIndices)
    //{
    //    tMeshVertices = new float[(width - 1) * (height - 1) * stride * 4];
    //    tMeshIndices = new unsigned int[(width - 1) * (height - 1) * 6];
    //}

    //TerrainGeneration();

    ////OpenGL setup for the mesh
    //terGenVAO = std::make_unique<VertexArray>();
    //terGenVertexBuffer = std::make_unique<VertexBuffer>(tMeshVertices, (width - 1) * (height - 1) * 4 * stride * sizeof(float));
    //terGenIndexBuffer = std::make_unique<IndexBuffer>(tMeshIndices, (width - 1) * (height - 1) * 6);
    //terGenShader = std::make_unique<Shader>("res/shaders/Lightning_final_vertex.shader", "res/shaders/Lightning_final_fragment.shader");
    //terGenTexture = std::make_unique<Texture>("res/textures/texture.png");

    //terGenVAO->AddBuffer(*terGenVertexBuffer, layout);
}

void TerrainGenApp::TerrainGeneration() {
    if (!terrainGen.PerformTerrainGeneration()) {
        std::cout << "[ERROR] Map couldnt be generated" << std::endl;
        return;
    }

    utilities::CreateTiledVertices(tMeshVertices, width, height, terrainGen.GetHeightMap(), heightScale, stride, 0);
    utilities::CreateIndicesTiledField(tMeshIndices, width, height);
    /*utilities::InitializeNormals(tMeshVertices, stride, 3, (height - 1) * (width - 1) * 4);
    utilities::CalculateNormals(tMeshVertices, tMeshIndices, stride, 3, (width - 1) * (height - 1) * 6);
    utilities::NormalizeVector3f(tMeshVertices, stride, 3, (height - 1) * (width - 1) * 4);
    utilities::AssignTexturesByBiomes(terrainGen, tMeshVertices, width, height, 3, stride, 6);*/
    std::cout << "[LOG] Terrain generation completed!" << std::endl;
}

//-------------------------------------------------------------------------------------------------------
//--------------------------------------------VEGETATION-SECTION------------------------------------------
//-------------------------------------------------------------------------------------------------------
void TerrainGenApp::PrepareTreesDraw()
{
 //   float treeVertices[] = {
 //       0.4f, 0.00f, 0.4f,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
 //       0.4f, 0.00f, 0.6f,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
 //       0.6f, 0.00f, 0.6f,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
 //       0.6f, 0.00f, 0.4f,   0.0f, -1.0f, 0.0f,  0.5f, 0.25f, 0.0f,
 //      
 //       0.4f, 0.25f, 0.4f,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
 //       0.4f, 0.25f, 0.6f,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
 //       0.6f, 0.25f, 0.6f,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,
 //       0.6f, 0.25f, 0.4f,   0.0f, 1.0f, 0.0f,   0.5f, 0.25f, 0.0f,

 //       0.0f, 0.25f, 0.0f,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
 //       1.0f, 0.25f, 0.0f,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
 //       1.0f, 0.25f, 1.0f,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
 //       0.0f, 0.25f, 1.0f,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f,
 //       0.5f, 1.00f, 0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 0.5f, 0.0f
 //   };

 //   unsigned int treeIndices[] = {
 //       //
 //       0, 1, 2,
 //       2, 3, 0,
 //       //
 //       4, 5, 6,
 //       6, 7, 4,
 //       //
 //       0, 1, 5,
 //       0, 5, 4,
 //       1, 2, 6,
 //       1, 6, 5,
 //       2, 3, 7,
 //       2, 7, 6,
 //       3, 0, 4,
 //       3, 4, 7,
 //       //
 //       8, 9, 10,
 //       8, 10, 11,
 //       8, 9, 12,
 //       9, 10, 12,
 //       10, 11, 12,
 //       11, 8, 12
 //   };

 //   treeIndicesCount = sizeof(treeIndices) / sizeof(treeIndices[0]);
	//terrainGen.VegetationGeneration();

 //   treesPositions = new float[3 * terrainGen.GetTreeCount()];
 //   std::cout << terrainGen.GetTreeCount() << std::endl;

 //   int index = 0;

 //   for (auto& it : terrainGen.GetVegetationPoints()) {
 //       for (auto& it2 : it) {
 //           treesPositions[index++] = it2.first;
 //           treesPositions[index++] = terrainGen.GetHeightAt(it2.first, it2.second);
 //           treesPositions[index++] = it2.second;
 //       }
 //   }

 //   glGenVertexArrays(1, &treeVAO);
 //   glGenBuffers(1, &treeVBO);
 //   glGenBuffers(1, &EBO);

 //   glBindVertexArray(treeVAO);

 //   glBindBuffer(GL_ARRAY_BUFFER, treeVBO);
 //   glBufferData(GL_ARRAY_BUFFER, sizeof(treeVertices), treeVertices, GL_STATIC_DRAW);

 //   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
 //   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(treeIndices), treeIndices, GL_STATIC_DRAW);

 //   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
 //   glEnableVertexAttribArray(0);
 //   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
 //   glEnableVertexAttribArray(1);
 //   glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
 //   glEnableVertexAttribArray(2);

 //   glGenBuffers(1, &instanceVBO);
 //   glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
 //   glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * terrainGen.GetTreeCount(), treesPositions, GL_STATIC_DRAW);

 //   glBindVertexArray(treeVAO);
 //   glEnableVertexAttribArray(3);
 //   glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
 //   glVertexAttribDivisor(3, 1);

 //   glBindVertexArray(0);
}

