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

#ifndef NDEBUG
namespace {
	const char* GlDebugTypeName(GLenum type)
	{
		switch (type) {
		case GL_DEBUG_TYPE_ERROR: return "ERROR";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UB";
		case GL_DEBUG_TYPE_PORTABILITY: return "PORTABILITY";
		case GL_DEBUG_TYPE_PERFORMANCE: return "PERF";
		default: return "OTHER";
		}
	}

	const char* GlDebugSeverityName(GLenum severity)
	{
		switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: return "HIGH";
		case GL_DEBUG_SEVERITY_MEDIUM: return "MEDIUM";
		case GL_DEBUG_SEVERITY_LOW: return "LOW";
		default: return "INFO";
		}
	}

	void APIENTRY GlDebugCallback(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*)
	{
		if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
			return;
		}
		std::cerr << "[OpenGL " << GlDebugTypeName(type) << " " << GlDebugSeverityName(severity) << " #" << id << "]: " << message << std::endl;
	}
}
#endif

TerrainGenApp::TerrainGenApp() : window(nullptr), windowWidth(0), windowHeight(0), deltaTime(0.0f), lastFrame(0.0),
rightPanelWidth(400.0f),topPanelHeight(30.0f), bottomPanelHeight(280.0f), leftPanelWidth(400.0f),
width(250), height(250), heightScale(256.0f),
camera(1920, 1080, glm::vec3(0.0f, heightScale, 0.0f), 100.0f, 5000.0f), renderer(),
currentMode(mode::NOISE_HEIGHTMAP)
{
}

TerrainGenApp::~TerrainGenApp()
{
	Shutdown();
}

void TerrainGenApp::Shutdown()
{
	if (!glfwInitialized && !window && !imguiInitialized) {
		return;
	}
	if (window) {
		glfwHideWindow(window);
	}
	if (terrainGenSys) {
		terrainGenSys->SetVegetationModels(nullptr);
		terrainGenSys->SetTerrainMaterials(nullptr);
	}
	if (mapImportSys) {
		mapImportSys->SetVegetationModels(nullptr);
		mapImportSys->SetTerrainMaterials(nullptr);
	}
	//Destroy all GL-owning objects before tearing down the OpenGL context
	mapImportSys.reset();
	terrainGenSys.reset();
	noiseGenSys.reset();
	clouds.reset();
	sky.reset();
	metrics.reset();
	materials.reset();
	vegetation.reset();

	if (imguiInitialized) {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		imguiInitialized = false;
	}
	if (window) {
		glfwDestroyWindow(window);
		window = nullptr;
	}
	if (glfwInitialized) {
		glfwTerminate();
		glfwInitialized = false;
	}
}


bool TerrainGenApp::Initialize()
{
	//Window & OpenGL initialization
    if (!glfwInit()) {
        std::cout << "[ERROR] Failed to initialize GLFW" << std::endl;
        return false;
    }
    glfwInitialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    
    int initWidth = 1920, initHeight = 1080;
    if (GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor()) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor)) {
            initWidth = mode->width;
            initHeight = mode->height;
        }
    }

    window = glfwCreateWindow(initWidth, initHeight, "Procedural terrain generator", NULL, NULL);

    if (!window)
    {
        std::cout << "[ERROR] Failed to create GLFW window" << std::endl;
        Shutdown();
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
        Shutdown();
        return false;
    }

#ifndef NDEBUG
    GLint contextFlags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &contextFlags);
    if (contextFlags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GlDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
    }
#endif

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
	imguiInitialized = true;

    metrics = std::make_unique<FrameMetrics>();
    //Without the list the TEXTURED mode has nothing to show, but the rest of the app works
    materials = std::make_unique<TerrainMaterials>();
    if (!materials->Initialize("res/textures/terrain_materials.json", "res/textures/terrain")) {
        std::cout << "[ERROR] Terrain materials not loaded" << std::endl;
    }
    //Optional as well: without it there is no vegetation
    vegetation = std::make_unique<VegetationModels>();
    if (!vegetation->Initialize("res/vegetation")) {
        std::cout << "[ERROR] Vegetation models not loaded" << std::endl;
    }

    //Logic initialization
	renderer.SetPatches(4);
	camera.SetScreenSize(ViewportWidth(), ViewportHeight());
	noiseGenSys = std::make_unique<NoiseBasedGenerationSys>();
    if (!noiseGenSys->Initialize(width, height, heightScale)) {
        std::cout << "[ERROR] Failed to initialize NoiseBasedGenerationSys" << std::endl;
        Shutdown();
        return false;
    }
	//TerrainGen builds continents thousands of texels wide, so its mountains need a larger scale than one small map
	terrainGenSys = std::make_unique<TerrainGenerationSys>();
	if (!terrainGenSys->Initialize(width, height, worldHeightScale)) {
		std::cout << "[ERROR] Failed to initialize TerrainGenerationSys" << std::endl;
		Shutdown();
		return false;
	}
	std::vector<std::string> materialNames;
	for (const TerrainMaterial& m : materials->GetMaterials()) {
		materialNames.push_back(m.name);
	}
	terrainGenSys->SetMaterialNames(materialNames);
	terrainGenSys->SetVegetationModels(vegetation.get());
	terrainGenSys->SetTerrainMaterials(materials.get());
	mapImportSys = std::make_unique<MapImportSys>();
	if (!mapImportSys->Initialize(width, height, heightScale)) {
		std::cout << "[ERROR] Failed to initialize MapImportSys" << std::endl;
		Shutdown();
		return false;
	}
	mapImportSys->SetMaterialNames(std::move(materialNames));
	mapImportSys->SetVegetationModels(vegetation.get());
	mapImportSys->SetTerrainMaterials(materials.get());
	sky = std::make_unique<Sky>();
	clouds = std::make_unique<Clouds>();
    if (!sky->Initialize() || !clouds->Initialize()) {
        Shutdown();
        return false;
    }

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

        editorCamera.Update(window, camera, ViewportRect(), FlatViewZoom());
        camera.SteerCamera(window, deltaTime, true);

        bool reloadKeyDown = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        if (reloadKeyDown && !reloadKeyWasDown) {
            ReloadShaders();
        }
        reloadKeyWasDown = reloadKeyDown;
        vegetation->Update();

        //Install finished background generation and start the pending one (only for the active mode)
        if (currentMode == mode::NOISE_HEIGHTMAP) {
            noiseGenSys->Update(camera);
        }
        else if (currentMode == mode::TERRAIN_GEN) {
            terrainGenSys->Update(camera);
        }
        //Every frame: a project save started before leaving the mode (or before closing the window) has to finish
        mapImportSys->Update(camera, currentMode == mode::MAP_IMPORT, deltaTime);
        //The sky and the light colors of this frame, read by every lit shader (also in the 2D map)
        light.Update(deltaTime);
        sky->Update(camera, light);

		Draw();
        //The depth of the scene is still there: the point a drag started this frame turns around
        editorCamera.AfterDraw(camera, ViewportRect());
        ImGuiRender();
        //"Send map" pressed in a Saving tab this frame: the map opens in the Imported map mode once it is installed (with unsaved
        //changes after the question, drawn in every mode; Cancel leaves the user where they were)
        std::optional<mapproject::Project> sent = noiseGenSys->TakeSentMap();
        if (!sent) {
            sent = terrainGenSys->TakeSentMap();
        }
        if (sent) {
            mapImportSys->Receive(std::move(*sent));
        }
        if (mapImportSys->TakeReceived()) {
            currentMode = mode::MAP_IMPORT;
        }
        const bool holeInView = (currentMode == mode::NOISE_HEIGHTMAP && noiseGenSys->HoleInView()) || (currentMode == mode::TERRAIN_GEN && terrainGenSys->HoleInView());
        metrics->EndFrame(deltaTime * 1000.0, (glfwGetTime() - currentFrame) * 1000.0, camera.GetPosition(), holeInView);

        camera.SetScreenSize(ViewportWidth(), ViewportHeight());
        
        glfwPollEvents();
        //Closing the window with an unsaved project asks first; the window closes once the changes are saved or discarded
        if (glfwWindowShouldClose(window) && mapImportSys->HasUnsavedChanges()) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            mapImportSys->RequestQuit();
            //Closed from the taskbar while minimized: the "Unsaved changes" question is drawn only in a visible window
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
                glfwRestoreWindow(window);
            }
        }
        if (mapImportSys->TakeQuit()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        glfwSwapBuffers(window);
    }

    Shutdown();
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
    materials->Bind();
    if (!FlatView()) {
        sky->Draw(camera, light, clouds->RenderSky(camera, light));
    }
    if (currentMode == mode::NOISE_HEIGHTMAP)
    {
        noiseGenSys->Draw(renderer, camera, light);
    }
    else if (currentMode == mode::TERRAIN_GEN) {
		terrainGenSys->Draw(renderer, camera, light);
    }
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys->Draw(renderer, camera, light);
    }
    if (!FlatView()) {
        clouds->DrawOver(camera, light);
    }
    metrics->EndScene();

    glDisable(GL_SCISSOR_TEST);
}

void TerrainGenApp::ReloadShaders()
{
    noiseGenSys->ReloadShaders();
    terrainGenSys->ReloadShaders();
    mapImportSys->ReloadShaders();
    sky->ReloadShader();
    clouds->ReloadShader();
    vegetation->ReloadShader();
}

bool TerrainGenApp::FlatView() const
{
    switch (currentMode) {
    case mode::NOISE_HEIGHTMAP: return noiseGenSys->FlatView();
    case mode::TERRAIN_GEN: return terrainGenSys->FlatView();
    case mode::MAP_IMPORT: return mapImportSys->FlatView();
    }
    return false;
}

cameramath::FlatZoom* TerrainGenApp::FlatViewZoom()
{
    switch (currentMode) {
    case mode::NOISE_HEIGHTMAP: return noiseGenSys->FlatViewZoom();
    case mode::TERRAIN_GEN: return terrainGenSys->FlatViewZoom();
    case mode::MAP_IMPORT: return mapImportSys->FlatViewZoom();
    }
    return nullptr;
}

void TerrainGenApp::ImGuiRender()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    editorCamera.ImGuiWheel(window, camera, ViewportRect(), FlatViewZoom());

    //Left panel
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, static_cast<float>(windowHeight)), ImGuiCond_Always);
    //Side panels hold tabs; the content of a tab scrolls on its own, so the panel itself does not
    ImGui::Begin("View", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    leftPanelWidth = ImGui::GetWindowWidth() <= windowWidth / 2 ? ImGui::GetWindowWidth() : windowWidth / 2;
    //Shared tabs first, so they keep their place when the mode changes
    std::vector<PanelTabs::Tab> viewTabs = {
        { "Display", [this] {
            if (currentMode == mode::NOISE_HEIGHTMAP) {
                noiseGenSys->ImGuiDisplay();
            }
            else if (currentMode == mode::TERRAIN_GEN) {
                terrainGenSys->ImGuiDisplay();
            }
            else {
                mapImportSys->ImGuiDisplay();
            }
        } },
        { "Camera", [this] { camera.ImGuiDraw(); editorCamera.ImGuiDraw(); } },
        { "Sun & sky", [this] { light.ImGuiDraw(); } },
        { "Materials", [this] { materials->ImGuiDraw(); } },
        { "Vegetation", [this] {
            if (currentMode == mode::TERRAIN_GEN) {
                ImGui::SeparatorText("Placement");
                terrainGenSys->ImGuiVegetation();
                ImGui::SeparatorText("Models");
            }
            vegetation->ImGuiDraw(light);
        } },
    };
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        viewTabs.push_back({ "Presets", [this] { noiseGenSys->ImGuiPresets(camera); } });
        viewTabs.push_back({ "Saving", [this] { noiseGenSys->ImGuiSaving(); } });
    }
    else if (currentMode == mode::TERRAIN_GEN) {
        viewTabs.push_back({ "Presets", [this] { terrainGenSys->ImGuiPresets(camera); } });
        viewTabs.push_back({ "Saving", [this] { terrainGenSys->ImGuiSaving(); } });
    }
    else if (currentMode == mode::MAP_IMPORT) {
        viewTabs.push_back({ "Saving", [this] { mapImportSys->ImGuiSaving(); } });
    }
    viewTabs.push_back({ "About", [] {
        ImGui::TextWrapped("Hi Dear User!\n"
            "I am more than happy to be able to introduce my Procedural Generation App to you.\n"
            "It a project ive been working on for some time now because the field it covers extremely interests me and it was the topic of my thesis.\n"
            "I hope you will find it a bit interesting and fun to play with "
            "and feel free to contact me if you have any questions or suggestions.\n");
    } });
    viewPanelTabs.Draw("view", viewTabs);
    ImGui::End();

	//Right panel
    ImGui::SetNextWindowPos(ImVec2(windowWidth - rightPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, static_cast<float>(windowHeight)), ImGuiCond_Always);
    ImGui::Begin("Generation", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    rightPanelWidth = ImGui::GetWindowWidth() <= windowWidth/2 ? ImGui::GetWindowWidth() : windowWidth/2;

    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys->ImGuiRightPanel();
    }
    else if(currentMode == mode::TERRAIN_GEN) {
		terrainGenSys->ImGuiRightPanel();
	}
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys->ImGuiRightPanel();
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
        noiseGenSys->ImGuiOutput();
    }
    else if (currentMode == mode::TERRAIN_GEN) {
		terrainGenSys->ImGuiOutput(camera.GetPosition());
    }
    else if (currentMode == mode::MAP_IMPORT) {
        mapImportSys->ImGuiOutput();
    }
    bottomPanelHeight = ImGui::GetWindowHeight() <= windowHeight / 3 ? ImGui::GetWindowHeight() : windowHeight / 3;

    ImGui::End();

    //Names of the maps shown side by side, over the scene drawn this frame
    if (currentMode == mode::NOISE_HEIGHTMAP) {
        noiseGenSys->ImGuiMapLabels();
    }
    else if (currentMode == mode::TERRAIN_GEN) {
        terrainGenSys->ImGuiMapLabels();
    }
    //In every mode: closing the window asks about an unsaved project too
    mapImportSys->ImGuiModals();

    ImGui::Render();
    metrics->BeginUi();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    metrics->EndUi();
}