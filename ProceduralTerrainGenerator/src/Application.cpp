#include "TerrainGenApp.h"
#include "GpuHeightCheck.h"
#include "VegetationCheck.h"

#include <cstdlib>
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
	//Frame times of the vegetation above a forest of the default world
	if (argc > 1 && std::strcmp(argv[1], "--vegetation-bench") == 0) {
		return RunVegetationBench(argc > 2 ? static_cast<float>(std::atof(argv[2])) : 1.0f, argc > 3 ? static_cast<float>(std::atof(argv[3])) : 30.0f,
			argc > 4 ? argv[4] : "", argc > 5 ? static_cast<float>(std::atof(argv[5])) : 0.0f, argc > 6 ? static_cast<float>(std::atof(argv[6])) : 0.0f);
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
