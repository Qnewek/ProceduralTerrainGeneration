#include "TerrainGenApp.h"

#include <iostream>

int main(void)
{
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
