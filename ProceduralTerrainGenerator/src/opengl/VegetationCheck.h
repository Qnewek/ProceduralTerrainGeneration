#pragma once

#include <string>

//--vegetation-preview [folder]: loads all vegetation models (building their caches if needed) in a hidden window,
//prints their triangle counts and load times and saves one picture per model to the folder (default: vegetation_preview):
//its levels of detail side by side, shaded above and as wireframe below
//@return 0 if every downloaded model loaded
int RunVegetationPreview(const std::string& folder);
