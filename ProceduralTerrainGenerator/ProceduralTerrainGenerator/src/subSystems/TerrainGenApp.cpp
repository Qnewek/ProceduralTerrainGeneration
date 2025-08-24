#include "TerrainGenApp.h"
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "utilities.h"

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0), deltaTime(0.0f), lastFrame(0.0f),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(200.0f), leftPanelWidth(400.0f),
width(400), height(400), heightScale(255.0f),
camera(1920, 1080, glm::vec3(0.0f, heightScale / 2.0f, 0.0f), 30.0f, 500.0f), renderer(),
noiseGenSys(), currentMode(mode::NOISE_HEIGHTMAP), light(glm::vec3(0.0f, 0.0f, 0.0f), 20.0f)
{
    std::cout << "[LOG] Hub initialized" << std::endl;
}

TerrainGenApp::~TerrainGenApp()
{
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
	camera.SetScreenSize(windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
	noiseGenSys.Initialize(height, width, heightScale);
	terrainGenSys.Initialize(height, width, heightScale);
    light.Initialize();
    light.SetPosition(glm::vec3(width, heightScale, height));
   
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
        ImGuiRender();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}


void TerrainGenApp::Draw()
{
    glEnable(GL_SCISSOR_TEST);
    glViewport(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    glScissor(leftPanelWidth, bottomPanelHeight, windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    renderer.Clear(glm::vec3(0.0f, 0.0f, 0.0f));

    light.Draw(renderer, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
    if (currentMode == mode::NOISE_HEIGHTMAP)
    {
        noiseGenSys.Draw(renderer, camera, light);
    }
    else if (currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.Draw(renderer, camera, light);
    }

    glDisable(GL_SCISSOR_TEST);
}

void TerrainGenApp::ImGuiRender()
{
    ImGui_ImplGlfwGL3_NewFrame();
    ImGui::SetNextWindowPos(ImVec2(windowWidth - rightPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, windowHeight-60), ImGuiCond_Always);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
    rightPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;

	camera.ImGuiDraw();
	light.ImGuiDraw();
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiDraw();
    }
    else if(currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.ImGuiDraw();
	}

    ImGui::Separator();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, windowHeight-60), ImGuiCond_Always);
    ImGui::Begin("Modify", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_ResizeFromAnySide);
    leftPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;
   
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, topPanelHeight), ImGuiCond_Always);
	ImGui::Begin("Mode", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
	ImGui::Text("Mode: ");
    ImGui::SameLine();
    if (ImGui::Button("Noise heightmap"))
    {
		currentMode = mode::NOISE_HEIGHTMAP;
    }
    ImGui::SameLine();
    if (ImGui::Button("TerrainGen"))
    {
        currentMode = mode::TERRAIN_GEN;
    }    
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, windowHeight - bottomPanelHeight - 60), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, bottomPanelHeight), ImGuiCond_Always);
    ImGui::Begin("OutPut", nullptr, ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
	
    camera.ImGuiOutPut();
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiOutput();
    }
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;
    
    ImGui::End();

    ImGui::Render();
    ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
}

//void TerrainGenApp::ParametrizedImGui()
//{
    //if (ImGui::CollapsingHeader("Terrain Generation Settings")) {
    //    if (ImGui::Button("Display terrain")) {
    //        isTerrainDisplayed = !isTerrainDisplayed;
    //    }
    //    ImGui::Separator();
    //    ImGui::Text("Noise configuration");
    //    if (ImGui::Button("Continentalness")) {
    //        SwapNoise(&terrainGen.GetContinentalnessNoise());
    //        editedNoise = "Continentalness";
    //        editedType = 'c';
    //    }
    //    ImGui::SameLine();
    //    if (ImGui::Button("Mountainousness")) {
    //        SwapNoise(&terrainGen.GetMountainousNoise());
    //        editedNoise = "Mountainousness";
    //        editedType = 'm';
    //    }
    //    ImGui::SameLine();
    //    if (ImGui::Button("Peaks&Valeys")) {
    //        SwapNoise(&terrainGen.GetPVNoise());
    //        editedNoise = "Peaks&Valeys";
    //        editedType = 'p';
    //    }
    //    if (ImGui::Button("Temperature")) {
    //        SwapNoise(&terrainGen.GetTemperatureNoise());
    //        editedNoise = "Temperature";
    //        editedType = 't';
    //    }
    //    ImGui::SameLine();
    //    if (ImGui::Button("Humidity")) {
    //        SwapNoise(&terrainGen.GetHumidityNoise());
    //        editedNoise = "Humidity";
    //        editedType = 'h';
    //    }
    //    ImGui::Spacing();
    //    
    //    ImGui::Separator();
    //    ImGui::Text("Vegetation settings");
    //    if (ImGui::Button("Vegetation generation"))
    //    {
    //        if (!treesPositions) {
    //            PrepareTreesDraw();
    //        }
    //        drawTrees = true;
    //    }
    //    if (ImGui::Button("Biomes settings")) {
    //        biomeEdit = !biomeEdit;
    //    }
    //    if (ImGui::Button("Generate terrain"))
    //    {
    //        terGenPerform = true;
    //        camera.SetPosition(glm::vec3(0.0f, heightScale/2.0f, 0.0f));
    //    }
    //    /*
    //    if (ImGui::Button("erode")) {
    //        PerformErosion();
    //    }
    //    */
    //}
//}

//void TerrainGenApp::ParameterImgui()
//{
//    if (ImGui::CollapsingHeader("Edit spline settings")) {
//        int in = 0;
//        if (editedType == 'm')
//            in = 2;
//        if (editedType == 'p')
//            in = 4;
//
//        for (size_t i = 0; i < splines[in].size(); i++)
//        {
//            float arg = static_cast<float>(splines[in][i]);
//            float value = static_cast<float>(splines[in + 1][i]);
//            float min = i > 0 ? splines[in][i - 1] : -1.0f;
//            float max = i < splines[in].size() - 1 ? splines[in][i + 1] : 1.0f;
//
//            ImGui::Separator();
//            ImGui::Text("Point %d", i);
//            ImGui::SliderFloat(("X" + std::to_string(i)).c_str(), &arg, min, max);
//            ImGui::InputFloat(("Y" + std::to_string(i)).c_str(), &value, 0.01f, 0.1f);
//
//            splines[in][i] = static_cast<double>(arg);
//            splines[in + 1][i] = static_cast<double>(value);
//        }
//        ImGui::Separator();
//        if (ImGui::Button("+", ImVec2(50, 30))) {
//            splines[in].push_back(1.0f);
//            splines[in + 1].push_back(splines[in + 1].back());
//        }
//        if (ImGui::Button("Set splines")) {
//            terrainGen.SetSpline(editedType, { splines[in], splines[in + 1] });
//            std::cout << "[LOG] New spline set" << std::endl;
//        }
//    }
//    if (editedType != 'p' && ImGui::CollapsingHeader("Edit biome range for current noise")) {
//        int in = 0;
//        if (editedType == 'h')
//            in = 1;
//        if (editedType == 'c')
//            in = 2;
//        if (editedType == 'm')
//			in = 3;
//
//        for (int i = 0; i < ranges[in].size(); i++) {
//			ImGui::Separator();
//			ImGui::Text("Level %d", ranges[in][i].level);
//			ImGui::LabelText(("Min" + std::to_string(ranges[in][i].level)).c_str(), "%f", ranges[in][i].min);
//			ImGui::SliderFloat(("Max" + std::to_string(ranges[in][i].level)).c_str(), &ranges[in][i].max, -1.0f, 1.0f);
//
//            if (ranges[in][i].min > ranges[in][i].max)
//                ranges[in][i].max = ranges[in][i].min;
//			if (i < ranges[in].size() - 1 && ranges[in][i].max != ranges[in][i + 1].min) {
//				ranges[in][i + 1].min = ranges[in][i].max;
//			}
//			
//        }
//        ImGui::Separator();
//        if (ImGui::Button("+", ImVec2(50, 30))) {
//			ranges[in].push_back({ 1.0f, 1.0f, ranges[in].back().level+1});
//        }
//        if (ImGui::Button("Set ranges")) {
//			terrainGen.SetRange(editedType, ranges[in]);
//            std::cout << "[LOG] New ranges set" << std::endl;
//        }
//    }
//    

//void TerrainGenApp::BiomeImGui()
//{
    /*if (ImGui::CollapsingHeader("Biomes")) {
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
    }*/
    
//
//void TerrainGenApp::TreeTypesImGui()
//{
   /* if (ImGui::CollapsingHeader("Tree types")) {
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
    }*/
//}

//void TerrainGenApp::TerrainGenerationDraw(glm::mat4& model) {
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
//}




















//void TerrainGenApp::DrawTrees(glm::mat4& model)
//{
   /* treeShader->Bind();
    treeShader->SetMaterialUniforms(glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.6f, 0.6f, 0.6f), glm::vec3(0.1f, 0.1f, 0.1f), 1.0f);
    treeShader->SetLightUniforms(glm::vec3(0.5f * height, 255.0f, 0.5f * width), glm::vec3(0.2f, 0.2f, 0.2f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f));
    treeShader->SetViewPos(camera.GetPosition());
    treeShader->SetMVP(model, *(camera.GetViewMatrix()), *(camera.GetProjectionMatrix()));
    treeShader->SetUniform1f("scale", heightScale);

    glBindVertexArray(treeVAO);
    glDrawElementsInstanced(GL_TRIANGLES, treeIndicesCount, GL_UNSIGNED_INT, 0, terrainGen.GetTreeCount());
    glBindVertexArray(0);*/
//}

//-------------------------------------------------------------------------------------------------------
//--------------------------------------------VEGETATION-SECTION------------------------------------------
//-------------------------------------------------------------------------------------------------------
//void TerrainGenApp::PrepareTreesDraw()
//{
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
//}

