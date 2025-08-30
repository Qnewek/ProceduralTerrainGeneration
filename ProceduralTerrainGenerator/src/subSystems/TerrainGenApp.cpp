#include "TerrainGenApp.h"
#include <iostream>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "ImPlot/implot.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "stb_image/stb_image.h"

#include "utilities.h"

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0), deltaTime(0.0f), lastFrame(0.0f),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(200.0f), leftPanelWidth(400.0f),
width(400), height(400), heightScale(255.0f),
camera(1920, 1080, glm::vec3(0.0f, heightScale / 2.0f, 0.0f), 100.0f, 1000.0f), renderer(),
noiseGenSys(), currentMode(mode::NOISE_HEIGHTMAP), light(glm::vec3(0.0f, 0.0f, 0.0f), 20.0f)
{
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

    GLFWimage images[1];
    images[0].pixels = stbi_load("res/image/icon.png", &images[0].width, &images[0].height, nullptr, 4);
    if (images[0].pixels) {
        glfwSetWindowIcon(window, 1, images);
        stbi_image_free(images[0].pixels);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::cout << "error" << std::endl;
    }

    glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
	ImPlot::CreateContext();
    
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

    //Logic initialization
	camera.SetScreenSize(windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
    if (!noiseGenSys.Initialize(height, width, heightScale))
        return 0;
	if(!terrainGenSys.Initialize(height, width, heightScale))
		return 0;
    light.Initialize();
    light.SetPosition(glm::vec3(width, heightScale, height));
   
    std::cout << "[LOG] Hub initialized" << std::endl;
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

        camera.SetScreenSize(windowWidth - rightPanelWidth - leftPanelWidth, windowHeight - topPanelHeight - bottomPanelHeight);
        
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
	ImPlot::DestroyContext();

    glfwDestroyWindow(window);
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
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    //Left panel
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, windowHeight - 60), ImGuiCond_Always);
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

    ImGui::End();

	//Right panel
    ImGui::SetNextWindowPos(ImVec2(windowWidth - rightPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, windowHeight-60), ImGuiCond_Always);
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_None);
    rightPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;

    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiRightPanel();
    }
    else if(currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.ImGuiRightPanel();
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
    ImGui::End();

	//Bottom panel
    ImGui::SetNextWindowPos(ImVec2(leftPanelWidth, windowHeight - bottomPanelHeight - 60), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth - rightPanelWidth - leftPanelWidth, bottomPanelHeight), ImGuiCond_Always);
    ImGui::Begin("OutPut", nullptr, ImGuiWindowFlags_None | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextWrapped("FPS: %.1f", 1.0f / deltaTime);
	
    camera.ImGuiOutPut();
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys.ImGuiOutput();
    }
    else if (currentMode == mode::TERRAIN_GEN) {
		terrainGenSys.ImGuiOutput();
    }
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;
    
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}