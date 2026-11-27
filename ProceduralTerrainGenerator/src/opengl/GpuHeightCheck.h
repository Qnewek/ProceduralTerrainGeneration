#pragma once

//Checks the compute shader generation using a hidden window: values finite and within the range of the configuration,
//chunks and levels of detail equal to the whole map bit for bit, surface map exact on maps with known derivatives; then measures it.
//Needs OpenGL 4.5: a GPU by hand (ProceduralTerrainGenerator.exe --gpu-check), Mesa llvmpipe on CI (--gpu-check --no-timings)
//@param timings - false skips the measurements (meaningless on a software renderer)
//@return 0 if all checks pass
int RunGpuHeightCheck(bool timings = true);
