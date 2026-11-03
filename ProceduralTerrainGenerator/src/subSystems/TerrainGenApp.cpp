#include "TerrainGenApp.h"
#include <iostream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "stb_image.h"

#include "utilities.h"

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0), deltaTime(0.0f), lastFrame(0.0),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(280.0f), leftPanelWidth(400.0f),
width(250), height(250), heightScale(256.0f),
camera(1920, 1080, glm::vec3(0.0f, heightScale, 0.0f), 100.0f, 1000.0f), renderer(),
noiseGenSys(), currentMode(mode::NOISE_HEIGHTMAP), light(glm::vec3(0.0f, 0.0f, 0.0f), 20.0f)
{
}

TerrainGenApp::~TerrainGenApp()
{
}


bool TerrainGenApp::Initialize()
{
	//Window & OpenGL initialization
    if (!glfwInit()) {
        std::cout << "[ERROR] Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    window = glfwCreateWindow(mode->width, mode->height, "Procedural terrain generator", NULL, NULL);

    if (!window)
    {
        std::cout << "[ERROR] Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    GLFWimage images[1];
    images[0].pixels = stbi_load("res/image/icon.png", &images[0].width, &images[0].height, nullptr, 4);
    if (images[0].pixels) {
        glfwSetWindowIcon(window, 1, images);
        stbi_image_free(images[0].pixels);
    }

    UpdateWindowSize();

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cout << "[ERROR] Failed to initialize GLEW" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
 //   glEnable(GL_CULL_FACE);
	//glCullFace(GL_CW);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
	ImPlot::CreateContext();
    
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 450");

    metrics = std::make_unique<FrameMetrics>();

    //Logic initialization
	renderer.SetPatches(4);
	camera.SetScreenSize(ViewportWidth(), ViewportHeight());
    if (!noiseGenSys.Initialize(width, height, heightScale)) {
        std::cout << "[ERROR] Failed to initialize NoiseBasedGenerationSys" << std::endl;
        return false;
    }
	if (!terrainGenSys.Initialize(width, height, heightScale)) {
		std::cout << "[ERROR] Failed to initialize TerrainGenerationSys" << std::endl;
		return false;
	}
	if (!mapImportSys.Initialize(width, height, heightScale)) {
		std::cout << "[ERROR] Failed to initialize MapImportSys" << std::endl;
		return false;
	}
    light.Initialize();
    light.SetPosition(glm::vec3(0.0f, 400.0f, 0.0f));

    std::cout << "[LOG] Hub initialized" << std::endl;
    return true;
}

void TerrainGenApp::Start()
{
    lastFrame = glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        double currentFrame = glfwGetTime();
        deltaTime = static_cast<float>(currentFrame - lastFrame);
        lastFrame = currentFrame;

        UpdateWindowSize();

		camera.CameraAnchor(false);
        camera.SteerCamera(window, deltaTime, true);

        bool reloadKeyDown = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        if (reloadKeyDown && !reloadKeyWasDown) {
            ReloadShaders();
        }
        reloadKeyWasDown = reloadKeyDown;

        //Install finished background generation and start the pending one (only for the active mode)
        if (currentMode == mode::NOISE_HEIGHTMAP) {
            noiseGenSys.Update();
        }
        else if (currentMode == mode::TERRAIN_GEN) {
            terrainGenSys.Update();
        }
        else if (currentMode == mode::MAP_IMPORT) {
            mapImportSys.Update();
        }

		Draw();
        ImGuiRender();
        metrics->EndFrame(deltaTime * 1000.0, (glfwGetTime() - currentFrame) * 1000.0);

        camera.SetScreenSize(ViewportWidth(), ViewportHeight());
        
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        glfwSwapBuffers(window);
    }

    metrics.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}


void TerrainGenApp::UpdateWindowSize()
{
    int framebufferWidth = 0, framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth > 0 && framebufferHeight > 0) {
        windowWidth = framebufferWidth;
        windowHeight = framebufferHeight;
    }
}

void TerrainGenApp::Draw()
{
    glEnable(GL_SCISSOR_TEST);
    glViewport(static_cast<int>(leftPanelWidth), static_cast<int>(bottomPanelHeight), ViewportWidth(), ViewportHeight());
    glScissor(static_cast<int>(leftPanelWidth), static_cast<int>(bottomPanelHeight), ViewportWidth(), ViewportHeight());
    renderer.Clear(glm::vec3(0.0f, 0.0f, 0.0f));

    metrics->BeginScene();
    light.Draw(renderer, *camera.GetViewMatrix(), *camera.GetProjectionMatrix());
    if (currentMode == mode::NOISE_HEIGHTMAP)
    {
        noiseGenSys.Draw(renderer, camera, light);
    }
    else if (currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.Draw(renderer, camera, light);
    }
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys.Draw(renderer, camera, light);
    }
    metrics->EndScene();

    glDisable(GL_SCISSOR_TEST);
}

void TerrainGenApp::ReloadShaders()
{
    noiseGenSys.ReloadShaders();
    terrainGenSys.ReloadShaders();
    mapImportSys.ReloadShaders();
    light.ReloadShader();
}

void TerrainGenApp::ImGuiRender()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    //Left panel
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, static_cast<float>(windowHeight)), ImGuiCond_Always);
    ImGui::Begin("Main tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_None);
    leftPanelWidth = ImGui::GetWindowWidth() <= windowWidth / 2 ? ImGui::GetWindowWidth() : windowWidth / 2;
    if (ImGui::CollapsingHeader("A few words...", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Hi Dear User!\n"
            "I am more than happy to be able to introduce my Procedural Generation App to you.\n"
            "It a project ive been working on for some time now because the field it covers extremely interests me and it was the topic of my thesis.\n"
            "I hope you will find it a bit interesting and fun to play with "
            "and feel free to contact me if you have any questions or suggestions.\n");
    }
    camera.ImGuiDraw();
    light.ImGuiDraw();
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiLeftPanel();
    }
    else if (currentMode == mode::TERRAIN_GEN) {
        terrainGenSys.ImGuiLeftPanel();
    }
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys.ImGuiLeftPanel();
    }

    ImGui::End();

	//Right panel
    ImGui::SetNextWindowPos(ImVec2(windowWidth - rightPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, static_cast<float>(windowHeight)), ImGuiCond_Always);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_None);
    rightPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;

    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiRightPanel();
    }
    else if(currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.ImGuiRightPanel();
	}
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys.ImGuiRightPanel();
    }
    ImGui::End();

	//Top panel
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
    ImGui::SameLine();
    if (ImGui::Button("Imported map"))
    {
        currentMode = mode::MAP_IMPORT;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload shaders (F5)"))
    {
        ReloadShaders();
    }
    ImGui::End();

	//Bottom panel
    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, windowHeight - bottomPanelHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, bottomPanelHeight), ImGuiCond_Always);
    ImGui::Begin("OutPut", nullptr, ImGuiWindowFlags_None | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::Checkbox("VSync (turn off to measure frame time)", &vsync)) {
        glfwSwapInterval(vsync ? 1 : 0);
    }
    metrics->ImGuiOutput();

    camera.ImGuiOutPut();
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiOutput();
    }
    else if (currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.ImGuiOutput(camera.GetPosition());
    }
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys.ImGuiOutput();
    }
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;
    
    ImGui::End();

    ImGui::Render();
    metrics->BeginUi();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    metrics->EndUi();
}