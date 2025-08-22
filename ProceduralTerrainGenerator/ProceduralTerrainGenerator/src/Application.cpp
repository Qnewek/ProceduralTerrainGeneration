#include "TerrainGenApp.h"

#include <iostream>

int main(void)
{
	TerrainGenApp* app = new TerrainGenApp();
	app->Initialize();
	app->Start();
	delete app;
    return 0;
}
