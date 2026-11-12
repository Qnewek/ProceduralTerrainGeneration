#include "TerrainGenApp.h"
#include "GpuHeightCheck.h"
#include "VegetationCheck.h"

#include <cstring>
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[])
{
	//Shaders and textures are loaded from res/ relative to the working directory
	std::error_code error;
	if (!std::filesystem::exists("res", error)) {
		std::filesystem::current_path(PTG_RESOURCE_DIR, error);
		if (error) {
			std::cout << "[ERROR] Cannot find res/ - run from the ProceduralTerrainGenerator directory" << std::endl;
			return -1;
		}
	}

	//Checks of the GPU generation and its timings, without the application window
	if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
		return RunGpuHeightCheck();
	}
	//Levels of detail of the vegetation models as pictures, for checking how the simplification looks
	if (argc > 1 && std::strcmp(argv[1], "--vegetation-preview") == 0) {
		return RunVegetationPreview(argc > 2 ? argv[2] : "vegetation_preview");
	}

	TerrainGenApp* app = new TerrainGenApp();
	if (!app->Initialize()) {
		std::cout << "[ERROR] Application initialization failed" << std::endl;
		delete app;
		return -1;
	}
	app->Start();
	delete app;
    return 0;
}
