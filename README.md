# Procedural Terrain Generator

Desktop tool (C++20, OpenGL 4.5) for interactive **procedural terrain generation**: every parameter of the
algorithms can be changed in the UI (**Dear ImGui** + **ImPlot**) and the terrain is regenerated in the background
while the app stays responsive.

## Features

- **Noise heightmap mode**
  Fractal [Simplex noise](https://thebookofshaders.com/11) (fBm) shown as a 3D terrain. Every parameter is editable:
  seed, octaves, scale, contrast, redistribution, lacunarity, persistence, handling of negative values,
  ridged noise and island masks (cone, diagonal, squircle, ...).

- **Hydraulic erosion**
  Droplet-based erosion of the generated map, shown next to the original. Repeated runs accumulate.
  Based on [this work](http://www.firespark.de/resources/downloads/implementation%20of%20a%20methode%20for%20hydraulic%20erosion.pdf).

- **TerrainGen mode (multi-noise terrain)**
  Terrain built from three noises, similar to Minecraft 1.18+:
  - **Continentalness** - how far inland a point is,
  - **Mountainousness** - how much elevation there is,
  - **Weirdness** - sharper shapes such as ridges and valleys.

  The noises are combined either linearly or through spline functions edited in the UI (draggable control points).

- **Biomes**
  Biome assignment from temperature, humidity and the three terrain noises. Each parameter is split into levels
  whose bounds can be dragged in the UI; the terrain is painted with the biome colors.

- **Rendering**
  GPU tessellation (distance-based level of detail), world-space lighting, display modes: greyscale,
  topographical with isolines, monocolor, biomes; wireframe and a top-down 2D map view.

- **Performance**
  Map generation is parallelised over rows and runs on a background thread, so editing parameters never freezes
  the UI (512x512 terrain in ~40 ms on 6 threads).

## Screenshots

*Coming soon.*

## Building

Requirements: Windows x64, Visual Studio 2022 with the *Desktop development with C++* workload (it includes
CMake, Ninja and vcpkg). Dependencies (GLFW, GLEW, glm, ImGui, ImPlot, stb) are installed by vcpkg from
`vcpkg.json` on the first configure.

1. Clone the repository:
   ```bash
   git clone https://github.com/Qnewek/ProceduralTerrainGeneration.git
   ```
2. In Visual Studio: *File → Open → Folder...*, pick the repository and choose the `x64 Debug` or `x64 Release`
   configuration (from `CMakePresets.json`). From a *Developer PowerShell for VS 2022*:
   ```
   cmake --preset x64-release
   cmake --build --preset x64-release
   ```
   The executable is written to `out/build/<preset>/`. Outside the Visual Studio prompt set `VCPKG_ROOT` to a
   vcpkg checkout first.
3. Run `ProceduralTerrainGenerator.exe`. Shaders and textures are loaded from `ProceduralTerrainGenerator/res/`;
   if the working directory has no `res/` folder, the app switches to the source directory it was built from.

## Controls

| Key | Action |
|-----|--------|
| `W` `A` `S` `D` | move the camera |
| `Space` / `Left Ctrl` | move up / down |
| `M` | mouse look |
| `Esc` | release the mouse |
| `F5` | reload shaders |

The mode (Noise heightmap / TerrainGen) is selected in the top panel; generation parameters are in the right
panel, display settings in the left one.

## Benchmark

```
out\build\x64-release\ProceduralTerrainGenerator.exe --benchmark
```

Runs CPU generation without a window for 256², 512² and 1024² maps and prints median times together with
checksums of the generated maps (used to verify that refactors do not change the output). CI builds both
presets on every push and pull request and fails if the checksums change.

## Roadmap

Planned work - GPU generation with compute shaders, chunked infinite world with LOD, continuous biomes with
blending and texturing, instanced vegetation, GPU erosion, water and export - is described in
[docs/ROADMAP.md](docs/ROADMAP.md) (in Polish).
