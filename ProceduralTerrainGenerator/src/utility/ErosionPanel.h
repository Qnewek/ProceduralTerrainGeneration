#pragma once

#include "WorldConfig.h"

//Method choice and the parameters of that method (no buttons), shared by the Erosion tabs of Noise heightmap and TerrainGen
void ErosionParametersImGui(erosion::ErosionConfig& e);
//Name of a method as the UI and the map labels show it
const char* ErosionMethodName(erosion::Method method);
