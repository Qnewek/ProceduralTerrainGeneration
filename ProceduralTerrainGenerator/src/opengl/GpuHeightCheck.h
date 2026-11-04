#pragma once

//Compares the compute shader generation with the CPU generators for many configurations and measures both,
//using a hidden window. Needs a GPU with OpenGL 4.5, so it is run by hand and not on CI: ProceduralTerrainGenerator.exe --gpu-check
//@return 0 if all maps match within the tolerance
int RunGpuHeightCheck();
