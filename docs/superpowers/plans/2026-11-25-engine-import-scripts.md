# Export step 5: Unity and Godot import scripts — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An export pack (`exports/<name>/`) can be opened in Unity or Godot with one menu command and gives the terrain as in the app: heights,
surface materials with their textures, water and vegetation, at true scale.

**Architecture:** The app keeps writing the pack it writes today and adds what the engines need: the material textures of the materials on the map
(`materials/<asset>/`), a fallback color and repeat size per material, the vegetation LOD distances, always an R16 copy of the main height map, and
the importer scripts themselves (`engine/`, copied from `res/engine/`). The Unity importer (C# editor script) builds Unity Terrain tiles, terrain
layers, Terrain Trees with LODGroup prefabs and water. The Godot importer (editor plugin, GDScript) builds a scene from the pack's glTF terrain tiles
with distance LODs, a splat shader, height-map collision, water and MultiMesh vegetation.

**Tech Stack:** C++20 (ptg_core, ImGui panel), doctest, nlohmann::json; Unity 2022.3 LTS / Unity 6 C# editor API (+ optional package
`com.unity.cloud.gltfast` for GLB); Godot 4.3+ GDScript editor plugin.

**Spec:** this plan. User decision in chat 2026-11-25: "chcę, aby założenie narzędzia, które pozwoli na opracowanie terenu i wykorzystanie go
w zewnętrznym silniku, było spełnione — możesz rozpocząć prace" (go-ahead without further questions). My calls announced in chat: Unity Terrain
(tiles, resampled to 2^n + 1), splat layers with copied textures, Terrain Trees with LODGroup prefabs, glTFast for GLB; Godot plugin with mesh
tiles + splat shader + MultiMesh; new "Material textures" and "Engine import scripts" options in Saving; LOD distances in `_vegetation.json`;
Godot checked headless with a portable Godot, Unity checked by the user. No design doc file (user preference).

## Global Constraints

- Branch `feat/engine-import` from `origin/master` (`377258c`). Edits only with Edit/Write tools; **no commits** — the user commits after checking.
- Existing pack files and JSON fields keep their names and meaning; new fields are additions, `manifestVersion` stays 1.
- All existing tests pass, `/W4 /WX`; generation is untouched (no `--gpu-check` needed unless a shader changes — none should).
- Coordinates of the pack (manifest `grid.axes`): right-handed, Y up, x = column · metresPerTexel, z = row · metresPerTexel, row 0 = top of the 2D map.
  - **Godot** (right-handed, Y up): position = pack position; yaw = pack yaw (`Basis(Vector3.UP, yaw)`).
  - **Unity** (left-handed): `unity = (x, y, sizeZ − z)` with `sizeZ = (height − 1) · metresPerTexel`, so the map is not mirrored and row 0 is at +Z;
    texture rows from `Texture2D.LoadImage` are bottom-up, so terrain index `[zi, xi]` = PNG row `height − 1 − zi`. Tree yaw = `π − yaw` (glTFast
    negates X when importing glTF; derivation: Q = M_z · R(yaw) · M_x = R_Unity(π − yaw)).
- Unity limits: heightmap resolution 2^n + 1 ≤ 4097, alphamap resolution power of two 16–4096; a map wider than 4097 texels becomes several
  Terrain tiles (`SetNeighbors`).
- Material repeat in metres = `tileSize · textureScale / 4 · metresPerTexel` (`TerrainView::worldUnitsPerTexel` = 4; materials repeat in texels on
  imported maps, see CLAUDE.md, D1).
- Scripts must not need anything the user has to type paths into: Unity menu `Tools/PTG/Import pack...`, Godot menu `Project → Tools → Import PTG pack...`,
  both open a file dialog for `<name>.json`.
- Out of scope: Unreal script, terrain holes, impostors in engines, grass, clouds/sky, re-applying edits made in the engine back to the app.

## Review Focus

1. Imported map pack: non-square map, `metresPerTexel` = 30, no vegetation → terrain size in metres right, collision matches the mesh → Task 3
   sample pack "dem" + `godot_import_check.gd` assertions on AABB and collision size.
2. Minimal pack (Noise mode: height only, no materials, biomes, meshes, water, vegetation) → Unity builds a grey terrain, Godot reports "needs the
   glTF terrain mesh" and builds nothing, no script error → Task 3 sample pack "minimal" + check expects the message.
3. Materials whose textures are not downloaded → layer/array slice with the manifest `fallbackColor`, no missing-file error → Task 1 test
   `"Material textures: missing files keep the fallback color"`, Task 3 sample pack "full" has one such material.
4. Importing the same pack twice → the scene/assets are replaced, not duplicated, no error → Task 3 check runs the build twice and counts nodes.
5. Vegetation exported without model files ("Vegetation models" off) → vegetation skipped with one warning, terrain still built → Task 3 sample
   pack "dem" has a vegetation file without models.

---

### Task 1: Pack data for engines (ptg_core)

**Files:**
- Create: `ProceduralTerrainGenerator/src/terrainGeneration/EngineExport.h`, `.cpp` (add to `ptg_core` in `CMakeLists.txt`)
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/ExportManifest.h/.cpp` (Material fields + JSON)
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/VegetationExport.h/.cpp` (LOD distances)
- Test: `ProceduralTerrainGenerator/tests/EngineExportTests.cpp` (new, add to `ptg_tests`), `ExportManifestTests.cpp`, `VegetationExportTests.cpp`

**Interfaces:**
- Produces:
  - `exportpack::Material` gains `float fallbackColor[3]` (0–1), `std::string albedo, normal, roughness` (paths relative to the manifest, empty = none),
    `float tileMetres = 0` (0 = unknown). JSON per material: `"fallbackColor": [r,g,b]`, `"textures": {"albedo": ..., "normal": ..., "roughness": ...,
    "tileMetres": ...}` or `"textures": null` when albedo is empty. `normal` is OpenGL convention (+Y) — say so in `materials.texturesNote`.
  - `namespace engineexport`:
    - `struct MaterialTextures { std::filesystem::path folder; float tileMetres = 0; float fallbackColor[3]; };` — one per material id
      (`folder` = `res/textures/terrain/<asset>`, may not exist).
    - `bool CopyMaterialTextures(const std::vector<MaterialTextures>& sources, std::vector<exportpack::Material>& materials, const std::filesystem::path& packFolder, std::vector<exportpack::File>& files, std::string& error)`
      — for every material with `coverage > 0` copies `albedo.*`, `normal.*`, `roughness.*` (any extension, first match) from its folder to
      `materials/<folder name>/`, sets the paths; always sets `fallbackColor` and `tileMetres`; adds `File{path, "material_albedo" | "material_normal" | "material_roughness", "<ext>"}`.
      A missing folder or file is not an error (path stays empty); a failed copy is (`error`, return false).
    - `bool CopyEngineScripts(const std::filesystem::path& source, const std::filesystem::path& packFolder, std::vector<exportpack::File>& files, std::string& error)`
      — recursive copy of `source` (= `res/engine`) to `<pack>/engine/`, one `File{path, "engine_script", "text"}` per file; missing source = error
      `"Import scripts not found: <path>"`.
  - `vegexport::LodRange { float lod1 = 0, lod2 = 0, max = 0; }` (metres: LOD0 below lod1, LOD1 below lod2, last mesh LOD to max);
    `vegexport::Input::lodRanges` = `std::array<LodRange, vegetation::layerCount>` defaulting to the app's defaults
    `{40, 100, 3000}, {15, 40, 120}, {20, 60, 200}`; `InstancesJson` gets the ranges as a new parameter and writes
    `"lodRanges": {"<layer name>": {"lod1": , "lod2": , "max": }}` (layer names = `vegetation::layerNames`), and each model's `"layer"` already names its range.

- [ ] **Step 1: Write the failing tests**
  - `EngineExportTests.cpp`:
    - `"Material textures: copied for materials on the map"` — temp folders, material 0 coverage 0.5 with `albedo.jpg`, `normal.png`, `roughness.jpg`,
      material 1 coverage 0 with the same files → material 0 paths = `materials/<asset>/albedo.jpg` etc. and files exist; material 1 paths empty and
      no `materials/<asset1>` folder; `files.size() == 3`; `tileMetres` and `fallbackColor` set on both.
    - `"Material textures: missing files keep the fallback color"` — material with coverage > 0 and a folder that does not exist → returns true,
      paths empty, `fallbackColor` = source values.
    - `"Engine scripts: copied recursively"` — source with `unity/Editor/A.cs` and `godot/addons/ptg_import/plugin.cfg` → both under
      `<pack>/engine/...`, 2 files with content `"engine_script"`; missing source → false and error contains `"Import scripts not found"`.
  - `ExportManifestTests.cpp`: `"Manifest writes material textures and fallback color"` — parse `ToJson` output: `materials.list[0].textures.albedo`,
    `.tileMetres`, `fallbackColor` size 3; a material with empty albedo → `textures` is null.
  - `VegetationExportTests.cpp`: `"Instances JSON has the LOD ranges"` — `lodRanges["Trees"]["lod1"] == 40`, `lodRanges["Shrubs"]["max"] == 120`
    with default input (keys = `vegetation::layerNames`: "Trees", "Shrubs", "Rocks").
- [ ] **Step 2: Build and run — expect compile failures / FAIL**
  Run (in a `.bat` with `VsDevCmd.bat -arch=x64`, see memory local-build-environment): `cmake --build --preset x64-release` then
  `out\build\x64-release\ptg_tests.exe -tc="*Material textures*,*Engine scripts*,*LOD ranges*,*material textures and fallback*"`.
- [ ] **Step 3: Implement** the Interfaces above. `std::filesystem::copy_file(..., overwrite_existing)` with `std::error_code`; texture match =
  first file in the folder whose stem is exactly `albedo` / `normal` / `roughness`.
- [ ] **Step 4: Run the new tests and the whole suite** — `ptg_tests.exe` all pass (previous count + 5).

### Task 2: Saving panel — textures, LOD distances, scripts, R16 copy

**Files:**
- Modify: `src/utility/ExportPanel.h/.cpp`, `src/opengl/TerrainMaterials.h/.cpp`, `src/opengl/VegetationLayer.h/.cpp`,
  `src/subSystems/TerrainGenerationSys.cpp`, `src/subSystems/MapImportSys.h/.cpp` (+ `MapImportVegetation.cpp` if it builds `vegexport::Input`),
  `src/subSystems/TerrainGenApp.cpp`
- Create: `ProceduralTerrainGenerator/res/engine/README.md` (pack-level instructions, copied with the scripts)

**Interfaces:**
- Consumes: Task 1 `engineexport::*`, `vegexport::LodRange`, `Input::lodRanges`.
- Produces:
  - `ExportPanel::MaterialSource::textures` = `std::function<std::vector<engineexport::MaterialTextures>(float metresPerTexel)>` (empty function = none).
  - `std::vector<engineexport::MaterialTextures> TerrainMaterials::ExportTextures(float metresPerTexel) const` — folder
    `res/textures/terrain/<asset>`, tileMetres per Global Constraints, fallbackColor.
  - `std::array<vegexport::LodRange, vegetation::layerCount> VegetationLayer::LodRanges() const` — from `ranges[l]` as `{lod1, lod2, max}`.
  - `MapImportSys::SetTerrainMaterials(const TerrainMaterials*)`, called in `TerrainGenApp` next to the TerrainGen one.

- [ ] **Step 1: Panel options.** New members `exportTextures = true`, `exportScripts = true`. Checkboxes:
  - "Material textures" right under "Material maps" (disabled unless material maps are on and `pack.materials->textures` is set);
    tooltip: "materials/<asset>/: albedo, normal (OpenGL) and roughness of every material on the map, for the engine importers".
  - "Engine import scripts" after "Vegetation models"; tooltip lists `engine/unity` (Unity Terrain, needs glTFast for models), `engine/godot`
    (needs Terrain mesh = glTF), `engine/README.md`. When ticked and `meshFormat != 1` show `ImGui::TextDisabled("Godot needs Terrain mesh = glTF")`.
  - The file list line gets ", material textures" / ", engine scripts".
- [ ] **Step 2: ExportAll.** After the height maps: if `exportScripts` and `format != R16`, also write the main map as R16
  (`<name>_height.r16`, `HeightFile(..., "height", R16)`) with the same range. After `ExportMaterials`: if textures on →
  `CopyMaterialTextures(pack.materials->textures(pack.metresPerTexel), manifest.materials, ...)`; if scripts on → `CopyEngineScripts("res/engine", ...)`
  (working directory is the resource dir, see CLAUDE.md). Errors via `SetError` like the other files.
- [ ] **Step 3: Wiring.** TerrainGen and Imported map set `MaterialSource::textures` from `TerrainMaterials::ExportTextures`; every place that builds a
  `vegexport::Input` for export or the Imported map plants sets `lodRanges` from its `VegetationLayer::LodRanges()` (Imported map: `vegetationLayer`
  of the automatic plants). Write `res/engine/README.md`: what each folder is, how to run each importer (menus above), the glTFast install line
  (`Window → Package Manager → + → Add package by name → com.unity.cloud.gltfast`), Godot plugin enabling (copy `engine/godot/addons` into the
  project, Project Settings → Plugins → enable "PTG pack import").
- [ ] **Step 4: Build, `ptg_tests.exe`, and run the app once** (`ProceduralTerrainGenerator.exe`, TerrainGen default, biomes on, Saving with every
  option, mesh glTF, vegetation + models) — pack folder has `materials/`, `engine/`, `_height.r16`, manifest has `textures`, `_vegetation.json` has
  `lodRanges`. Tell the user if the app could not be driven from here; then the check moves to their manual list.

### Task 3: Godot importer + sample packs + headless check

**Files:**
- Create in `ProceduralTerrainGenerator/res/engine/godot/addons/ptg_import/`: `plugin.cfg`, `plugin.gd` (EditorPlugin: menu item, `EditorFileDialog`,
  copy → scan → build → save → open), `ptg_pack.gd` (class_name `PtgPack`: parse manifest, R16 reader, file lookup by content/format, copy of the
  pack into `res://ptg/<name>/`), `ptg_builder.gd` (class_name `PtgBuilder`: `static func build(pack: PtgPack) -> Node3D`), `ptg_terrain.gdshader`,
  `ptg_water.gdshader`
- Create: `ProceduralTerrainGenerator/tests/EnginePackTests.cpp` (skipped case writing sample packs; add to `ptg_tests`),
  `ProceduralTerrainGenerator/tests/engine/godot_import_check.gd`, `tools/check_godot_import.bat`

**Interfaces:**
- Consumes: pack files and manifest fields of Tasks 1–2.
- Produces: the plugin; `PtgBuilder.build` returns a root `Node3D` named after the pack with children `Terrain`, `Collision` (StaticBody3D),
  `Water` (if any), `Vegetation` (if any); errors as `push_error` + return `null` with `PtgBuilder.last_error` set.

- [ ] **Step 1: Sample packs.** `TEST_CASE("Engine sample packs" * doctest::skip())` writes to `out/engine_samples/` (path from `__FILE__`) three packs
  through the real writers (`heightmap::Export`, `splatmap` PNGs, `meshexport::Write` GLB with 2 LODs and 128-texel tiles, `vegexport::Write`,
  `exportpack::ToJson`, `engineexport::*`):
  - `full`: 257×193 texels, 1 m, sea level, 3 materials (one with a texture folder from `res/textures/terrain/Grass004` if present, one missing),
    water, vegetation with models from `res/vegetation` (skip the vegetation part with a MESSAGE if not downloaded);
  - `dem`: 301×211 texels, 30 m per texel, no sea, materials without textures, vegetation JSON without model files;
  - `minimal`: 129×129, height R16 only.
  Run: `ptg_tests.exe --no-skip -tc="Engine sample packs"` → three folders with manifests.
- [ ] **Step 2: Portable Godot.** Download the latest Godot 4.x stable win64 zip from github.com/godotengine/godot/releases into
  `%LOCALAPPDATA%\ptg-tools\godot\` (outside the repo, untrusted-download rules: own empty folder). `tools/check_godot_import.bat <godot.exe>`:
  makes a temp project (`project.godot` + `addons/ptg_import` copied from `res/engine/godot`), runs
  `godot --headless --path <proj> --script res://godot_import_check.gd -- <sample pack> copy`, then `godot --headless --path <proj> --import`,
  then `... --script res://godot_import_check.gd -- <sample pack> build`, for each sample pack; exits non-zero on any failure.
- [ ] **Step 3: Write `godot_import_check.gd`** (extends `SceneTree`): `copy` mode = `PtgPack.load(manifest).copy_to_project()`; `build` mode = build
  twice, save to `res://ptg/<name>/<name>.scn`, reload it and assert:
  - `full`: `Terrain` has one MeshInstance3D per tile per LOD with `visibility_range_begin` of LOD n = `visibility_range_end` of LOD n−1;
    terrain AABB size ≈ (256, *, 192) ± 0.01; `Collision` HeightMapShape3D `map_width == 257`, `map_depth == 193`; `Water` has the sea plane at
    `seaLevel.metres`; `Vegetation` MultiMesh instance count summed over LOD0 = `vegetation.instances`; second build gives the same node count.
  - `dem`: AABB ≈ (9000, *, 6300); collision node scale 30 (uniform) with heights divided by 30; vegetation skipped with warning text
    `"vegetation models were not exported"`.
  - `minimal`: `build` returns null, `last_error` contains `"needs the glTF terrain mesh"`.
  Run `tools\check_godot_import.bat` → expect FAIL (no plugin code yet).
- [ ] **Step 4: Implement the plugin.**
  - Copy: whole pack folder into `res://ptg/<name>/` except `engine/`; in the editor then `EditorInterface.get_resource_filesystem().scan()` and wait
    until `is_scanning()` is false before building.
  - Terrain: `load()` every `meshes.levels[*].file`, instance its tiles (`terrain_<x>_<z>_LOD<n>`) under `Terrain`; LOD n visible from
    `begin_n = levels[n].errorMetres * 500.0` (LOD 0: 0) to `begin_{n+1}`, last LOD to 0 (= infinite), `visibility_range_fade_mode = SELF`, margin 10 % of begin.
  - Material: `ShaderMaterial(ptg_terrain.gdshader)` on every tile surface: `splat_0..splat_3` (unused = black), `Texture2DArray` albedo and
    normal (each texture resized to 1024², slices = material ids, missing texture = 4×4 image of `fallbackColor` / flat normal), per-material
    `tile_metres[16]`, UV from the mesh (= pack UV) for splat, world XZ / tile_metres for detail; no materials → `biome_colors` texture if exported,
    else grey. Save arrays as `.res` next to the scene.
  - Collision: `HeightMapShape3D` from the R16 file (metres via the encoding formula), `map_width = width`, `map_depth = height`, data in metres /
    metresPerTexel, `CollisionShape3D` uniform scale = metresPerTexel, positioned at the map centre (the shape is centred on its node).
  - Water: sea = `PlaneMesh` of the map size at `seaLevel.metres`; rivers/lakes = the water GLB nodes; both with `ptg_water.gdshader`
    (blue, transparent, roughness 0.05, scrolling normal from noise).
  - Vegetation: models via `load()` of `vegetation.modelFiles`; for each model, variant and LOD a `MultiMesh` per square of
    `max(256, ceil(max(size) / 32))` metres; ranges from `lodRanges[model.layer]`: LOD0 0–lod1, LOD1 lod1–lod2, last LOD lod2–max; instance
    transform = `Transform3D(Basis(Vector3.UP, yaw).scaled(Vector3.ONE * scale), position)`.
  - Menu flow in `plugin.gd`: after save, `EditorInterface.open_scene_from_path`.
- [ ] **Step 5: Run `tools\check_godot_import.bat`** → all three packs PASS, no `SCRIPT ERROR` lines in the output.
- [ ] **Step 6: Look at the result.** Render one frame of `full` headless is not possible; instead run the windowed editor once
  (`godot -e --path <proj> res://ptg/full/full.scn`) and take a screenshot only if the user asks — otherwise add "open full.scn in Godot" to the
  manual list.

### Task 4: Unity importer

**Files:**
- Create: `ProceduralTerrainGenerator/res/engine/unity/Editor/PtgPackImporter.cs` (one file: menu, minimal JSON reading, terrain, layers, trees, water)

**Interfaces:**
- Consumes: pack files and manifest fields of Tasks 1–2.
- Produces: menu `Tools/PTG/Import pack...`; output under `Assets/PTG/<name>/` (TerrainData assets, TerrainLayers, copied textures and GLBs,
  tree prefabs) and a root GameObject `<name>` in the open scene with children `Terrain_<x>_<z>`, `Water`.

- [ ] **Step 1: JSON.** `JsonUtility` classes for manifest fields (grid, height, files[] with encoding, materials.list[] with splat/textures/
  fallbackColor, meshes.water, vegetation.file); `seaLevel` present = raw text contains `"seaLevel"`. `_vegetation.json`: `JsonUtility` for
  `models[]` and `lodRanges` is not possible (dictionary) → read `lodRanges.<layer>` and the `instances` rows with a small number scanner
  (rows of 7 numbers after `"instances"`).
- [ ] **Step 2: Terrain.** Heights from the R16 file (manifest `files` with content `height` and format `r16`; missing → `EditorUtility.DisplayDialog`
  "Export the pack with Engine import scripts on"). Tiles: `tilesX = ceil((width − 1) / 4096)`, same for Z; each tile covers `(width − 1) / tilesX`
  texels; resolution = smallest 2^n + 1 ≥ span + 1, capped 4097; heights bilinear from the texel grid; `size = (spanX·m, metresMax − metresMin,
  spanZ·m)`, position y = `metresMin`; Unity Z per Global Constraints; `SetNeighbors` across tiles. Save each `TerrainData` as an asset.
- [ ] **Step 3: Layers.** Per material (id order): `TerrainLayer` with copied albedo / normal (normal texture importer set to `NormalMap`),
  `tileSize = (tileMetres, tileMetres)`; no texture → 4×4 texture of `fallbackColor` saved as PNG asset. Alphamaps from `_splat_N.png`
  (`Texture2D.LoadImage`, bottom-up rows), resolution = power of two nearest the tile span (16–4096), bilinear, renormalised to sum 1.
  No materials → one grey layer.
- [ ] **Step 4: Trees.** If `vegetation` and model GLBs exist: copy GLBs to `Assets/PTG/<name>/Vegetation/`, `AssetDatabase.ImportAsset`;
  `LoadAssetAtPath<GameObject>` null → warning "Install glTFast (com.unity.cloud.gltfast) to import the vegetation models" and skip trees.
  Per (model, variant) prefab: children = nodes `<name>_<variant>_LOD<n>`, `LODGroup` with screen heights from `lodRanges` (relative height =
  `typical height 10 m / (distance · 2 · tan(30°))`, culled after `max`). `TreeInstance`: position normalised in its tile, `rotation = π − yaw`,
  `widthScale = heightScale = scale`, `color/lightmapColor = white`; `terrainData.SetTreeInstances(..., snapToHeightmap: true)`.
- [ ] **Step 5: Water.** Sea: a `Plane` primitive scaled to the map at `seaLevel.metres` with a material of the active pipeline's default shader
  (`GraphicsSettings.currentRenderPipeline?.defaultMaterial.shader ?? Shader.Find("Standard")`), blue, smoothness 0.95; rivers/lakes GLB as a
  child (needs glTFast, same warning). Re-import replaces the existing root GameObject and assets of the same name.
- [ ] **Step 6: Static checks only** (no Unity here): re-read the file against the Global Constraints axes; every API used exists in 2022.3
  (`TerrainData.SetHeights`, `SetAlphamaps`, `terrainLayers`, `treePrototypes`, `SetTreeInstances`, `Terrain.SetNeighbors`). Add the Unity manual
  list to ROADMAP (Task 5).

### Task 5: Documentation

**Files:** `docs/ROADMAP.md` (Faza 5, Eksport: "🟡 2026-11-25 krok 5" paragraph), `CLAUDE.md` (Struktura: `EngineExport.*`, `res/engine/`; Eksport
map: textures, R16 copy, scripts), `res/engine/README.md` (final check).

- [ ] **Step 1:** ROADMAP paragraph: what the pack now contains, both importers, the axes rule, limits (Unity 4097 tiles, Godot needs glTF mesh,
  glTFast), Godot check results (`tools/check_godot_import.bat`), and "Do sprawdzenia w aplikacji / w silniku": Unity 2022.3 or 6 — import `full`
  pack of a real TerrainGen export with and without glTFast, trees face the same way as in the app (asymmetric model), map not mirrored
  (compare a coastline with the 2D map), 8192² Imported map → 2×2+ tiles without seams; Godot — open the scene, LOD switching, collision with a
  CharacterBody, texture repeat matches the app.
- [ ] **Step 2:** Final whole-branch review on model "sonnet" (user's CLAUDE.md), fix Critical/Important, re-run `ptg_tests.exe` and
  `tools\check_godot_import.bat`.
