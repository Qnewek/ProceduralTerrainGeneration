#pragma once

#include <string>

//--vegetation-preview [folder]: loads all vegetation models (building their caches if needed) in a hidden window,
//prints their triangle counts and load times and saves one picture per model to the folder (default: vegetation_preview):
//its levels of detail side by side, shaded above and as wireframe below
//@return 0 if every downloaded model loaded
int RunVegetationPreview(const std::string& folder);

//--vegetation-bench [density] [eye height in m, default 30] [screenshot prefix, - = none] [view distance in m]: the default world with
//biomes and vegetation in a hidden 1920 x 1080 window, the camera above a forest; once everything in range is generated and placed, prints
//the instances and the CPU and GPU times of a frame, measured with far trees as LOD 2 meshes and then as impostors
//@param density - multiplies the density of every plant, as the Density slider
//@param eyeMetres - height of the camera above the ground
//@param screenshot - the last measured frame of each measurement goes to <screenshot>_meshes.png and <screenshot>_impostors.png; empty or - = none
//@param viewMetres - render distance and range of trees; 0 = the application's render distance and default range
//@param sunElevation - degrees above the horizon (LightSource::SetElevation); 0 = the default sun
//@return 0 if it ran
int RunVegetationBench(float density, float eyeMetres, const std::string& screenshot, float viewMetres, float sunElevation);
