#pragma once

//Checks the compute shader generation using a hidden window: values finite and within the range of the configuration,
//chunks and levels of detail equal to the whole map bit for bit, surface map exact on maps with known derivatives; then measures it.
//Needs a GPU with OpenGL 4.5, so it is run by hand and not on CI: ProceduralTerrainGenerator.exe --gpu-check
//@return 0 if all checks pass
int RunGpuHeightCheck();
