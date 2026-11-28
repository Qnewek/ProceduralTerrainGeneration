# Procedural Terrain Generator

Desktop application (C++20, OpenGL 4.5) for interactive **procedural terrain generation** and **real-world DEM editing** with real-time GPU compute generation, erosion, hydrology, biomes, vegetation, and engine export packs. UI built with **Dear ImGui** + **ImPlot**.

## Features

- **GPU compute generation**
  - **Noise heightmap mode**: Fractal [Simplex noise](https://thebookofshaders.com/11) (fBm, ridged, island masks, domain warping, slope-damped octaves, terraces).
  - **TerrainGen mode**: Multi-noise terrain (`continentalness`, `mountainousness`, `weirdness`) combined via **Linear**, **Spline**, **Layered**, or **Nested spline** (`C -> M -> W`) curves edited live in ImPlot.
  - **Chunked infinite world & partial maps**: Distance-sorted quadtree chunks (`128x128`) with LOD skirts and frustum culling (CPU + TCS), plus a movable partial finite map linked to the infinite world (`worldOrigin`).

- **Biomes, Hydrology & Environment**
  - **Continuous 6D biomes**: 18 default biomes over `(continentalness, mountainousness, weirdness, temperature, humidity, elevation)` with live parameter-space editor, Whittaker diagram, elevation/coastal climate, terrain modifiers, and up to 4 PBR surface materials per biome (triplanar sampling, height blending, anti-tiling).
  - **Rivers & lakes (`REGION` and `NOISE`)**: Priority-flood hydrology (`Hydrology`), D8 flow accumulation, meanders, waterfalls/foam, screen-space reflection water (`WaterSurface`), volumetric 3D clouds with temporal smoothing (`Clouds`), Rayleigh/Mie/ozone atmospheric sky LUT (`Sky`), and 4-cascade sun shadows (`ShadowMap`).
  - **Instanced vegetation & grass**: Deterministic metre-grid placement (`TREES`, `SHRUBS`, `ROCKS`) with GPU frustum/LOD indirect culling (`meshoptimizer` LODs + hemi-octahedral impostors) and per-blade grass compute pass.

- **GPU Hydraulic Erosion**
  - Two GPU compute solvers: **Droplet** particle erosion and **Virtual pipes + thermal erosion**, available in both procedural and imported-map workflows.

- **Imported map mode & Projects (`.ptgmap`)**
  - Import real-world heightmaps and DEMs: **16-bit PNG**, headerless **R16/RAW**, **GeoTIFF** (with UTM / ETRS89 / PUWG 1992 / PUWG 2000 / WGS84 georeferencing), **SRTM `.hgt`**, and **ESRI `.asc` + `.prj`**.
  - Interactive GPU sculpting brushes (`Raise`, `Lower`, `Smooth`, `Flatten`, `Noise`), biome painting brush, GeoTIFF land-cover classification (e.g. ESA WorldCover), automatic & hand-placed vegetation brushes, river overlay, undo/redo history, and compressed zlib `.ptgmap` project files.

- **Export Packs & Engine Importers**
  - Export packs to `exports/<name>/`: **PNG16**, **R16**, **32-bit float EXR** heightmaps, **RGBA8 splat** and per-material weight maps, biome ID/color maps, tiled multi-LOD **GLB/OBJ** meshes + water meshes, `<name>_vegetation.json` + GLB LOD models, and `<name>.json` manifests.
  - Bundled importers in [ProceduralTerrainGenerator/res/engine/](ProceduralTerrainGenerator/res/engine/README.md): **Unity Editor** (`Tools > PTG > Import pack...`) and **Godot 4.3+ EditorPlugin** (`Project > Tools > Import PTG pack...`).

## Optional Assets (CC0)

Downloaded textures and 3D plant/rock models are excluded from git and stored locally under `res/textures/terrain/` and `res/vegetation/models/`:

```bash
python tools/fetch_textures.py   # Terrain PBR textures (Poly Haven / ambientCG)
python tools/fetch_models.py     # Vegetation & rock glTF models + alpha maps (Poly Haven)
```

## Building

All third-party dependencies are managed via `vcpkg.json` (`cgltf`, `doctest`, `glm`, `lodepng`, `meshoptimizer`, `nlohmann-json`, `stb`, `tiff`, `zlib`; desktop app feature `app`: `glew`, `glfw3`, `imgui`, `implot`).

### Windows x64 (Full Desktop App + Tests)

Requirements: Windows x64, Visual Studio 2022 (*Desktop development with C++*, CMake, Ninja, vcpkg).

From a **Developer PowerShell for VS 2022**:

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
```

Build outputs are placed in `out/build/<preset>/` (presets: `x64-debug`, `x64-release`). Run `ProceduralTerrainGenerator.exe` directly — if `res/` is not in the working directory, it locates `ProceduralTerrainGenerator/` automatically.

### Linux (`ptg_core` + Unit Tests with ASan/UBSan)

Requirements: GCC/Clang (C++20), Ninja, `libtbb-dev`, `pkg-config`, `VCPKG_ROOT` set to a bootstrapped vcpkg checkout:

```bash
cmake --preset linux-debug-asan
cmake --build --preset linux-debug-asan
ctest --preset linux-debug-asan --output-on-failure
```

Also available: `linux-release`.

## Controls

| Input | Action |
|-------|--------|
| `W` `A` `S` `D` | Move camera horizontally (FPS flight or while holding right mouse button) |
| `Space` / `Left Shift` | Move up / down |
| `M` | Toggle FPS mouse look |
| `Esc` | Release mouse look / cancel active brush stroke |
| Right drag / `Alt` + Left drag / Middle drag | Editor camera look / orbit / pan |
| Mouse wheel | Move forward/back (or zoom to cursor in 2D map view) |
| `F` | Fit 2D map view |
| `Shift` + Mouse wheel | Adjust sculpt / paint / plant brush radius |
| `Ctrl + S` / `Ctrl + Z` / `Ctrl + Y` | Save project / Undo / Redo (`Imported map` mode) |
| `F5` | Hot-reload GLSL shaders |

## Testing & CLI Tools

- **Core unit tests (`doctest`, 390 test cases)**:
  ```powershell
  ctest --preset x64-release --output-on-failure
  ```
- **GPU compute parity & generation checks** (requires OpenGL 4.5, or Mesa `llvmpipe` as run in CI):
  ```powershell
  out\build\x64-release\ProceduralTerrainGenerator.exe --gpu-check --no-timings
  ```
- **Vegetation LOD preview & benchmark**:
  ```powershell
  out\build\x64-release\ProceduralTerrainGenerator.exe --vegetation-preview vegetation_preview
  out\build\x64-release\ProceduralTerrainGenerator.exe --vegetation-bench
  ```
- **Python asset-fetcher unit tests**:
  ```bash
  python -m unittest discover -s tools -p "test_*.py" -v
  ```
- **CI (`.github/workflows/build.yml`)**: builds Windows `x64-debug` & `x64-release`, runs all unit tests + `--gpu-check` on Mesa llvmpipe, builds `linux-debug-asan` with GCC (`-Wall -Wextra -Werror`, ASan + UBSan) + runs `ctest`, and runs Python syntax & unit checks.

## Roadmap & Notes

Detailed development history and architecture notes are documented in [docs/ROADMAP.md](docs/ROADMAP.md) (in Polish).
