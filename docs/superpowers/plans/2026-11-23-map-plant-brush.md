# Imported map: Vegetation tab, plant brush, model preview — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The plants of the Imported map get their own right-panel tab "Vegetation" (after Biomes) with the automatic-plant settings, a brush that puts
one chosen model at the cursor or scatters several models over the brush circle, and the model list with the 3D preview of the left panel.

**Architecture:** Hand-placed plants are a separate layer (`placedplants::Layer`: palette of model names + plants in metres from texel (0, 0)), kept
apart from the automatic plants, which are placed again after every biome change. The layer lives in the undo history (`mapedit::Step::plants`),
in the project (`.ptgmap` v7) and in the export (`vegexport::Input::extra`, always kept, outside the limit). It is drawn by a second
`VegetationLayer` (`placedLayer`, extra set only); heights come from the map at upload time, so plants follow sculpting.

**Tech Stack:** C++20, OpenGL 4.5, ImGui, doctest, nlohmann::json, zlib (as `MapProject`).

**Spec:** this plan (user decisions in chat 2026-11-23: own tab after Biomes; brush places one model anywhere or a range where several models spread
according to the brush settings; reuse the left-panel "Vegetation" model list for preview; "jeżeli to jest jasne to zaczynaj"). No design doc file
(user preference: none in docs/superpowers/specs).

## Global Constraints

- Edits only with Edit/Write tools; the user commits after checking the app. Branch `feat/map-vegetation` (continues D4, uncommitted).
- TerrainGen output unchanged; all existing tests pass; `/W4 /WX`.
- Older projects (v1–v6) still open; v7 adds header `"placedPlants": {"palette": [names]}` and block `"placedPlants"` = zlib of 19 bytes per plant,
  little-endian: x f32, z f32, yaw f32, scale f32, model u16 (palette index), variant u8.
- Brush plants are never dropped by the 2M limit and never by a biome change; only by the eraser, "Clear placed plants", Undo, or a new map.
- Out of scope: shadows, 2D map view, editing single plants after placing, procedural plants removed under hand-placed ones.

## Review Focus

1. A model name in the layer that is not in the list (deleted model, other machine): kept in the layer and the project, not drawn, not exported, no crash → Task 1 `"ToPlaced skips unknown and unloaded models"`.
2. Sculpt / erosion / sea under placed plants: they stand on the new ground (y from the current heights) → Task 1 `"ToPlaced stands plants on the heights"`.
3. Plants outside the map after a crop or resample (positions beyond (W−1)·pixel): not drawn, not exported, kept → Task 1 `"ToPlaced skips plants outside the map"`.
4. Undo/Redo across mixed steps (sculpt, paint, plants): a plant step restores only the plants, others leave them alone → Task 2 `"History restores placed plants only from their steps"`.
5. Damaged plant block or palette index out of range in a project: load fails with a message, current project untouched → Task 3 `"Damaged placed plants fail"`.

---

### Task 1: Core layer of hand-placed plants (`PlacedPlants.*`)

**Files:**
- Create: `ProceduralTerrainGenerator/src/terrainGeneration/PlacedPlants.h`, `PlacedPlants.cpp` (add to `ptg_core` in `CMakeLists.txt`)
- Test: `ProceduralTerrainGenerator/tests/PlacedPlantsTests.cpp` (add to `ptg_tests`)

**Interfaces (namespace `placedplants`):**
- Produces:
  - `struct Plant { glm::vec2 position; float yaw = 0; float scale = 1; uint16_t model = 0; uint8_t variant = 0; };` position = metres from the centre of texel (0, 0), x right, y = map row direction; model = palette index; scale = metres per metre of the model.
  - `struct Layer { std::vector<std::string> palette; std::vector<Plant> plants; size_t Bytes() const; bool operator==(const Layer&) const = default; };`
  - `inline constexpr int maxPalette = 65535;` `inline constexpr size_t plantBytes = 19;`
  - `struct Brush { glm::vec2 centre; float radius = 10; int count = 1; float spacing = 2; float scaleMin = 0.8f, scaleMax = 1.2f; std::vector<std::string> models; std::vector<int> variants; uint32_t seed = 0; };` variants[i] = variant count of models[i].
  - `size_t Scatter(Layer& layer, const Brush& brush, glm::vec2 extent);` count == 1: one plant exactly at `centre`; count > 1: `count` candidates uniform in the disc. A candidate is dropped when outside [0, extent] or closer than `spacing` to any plant of the layer (also ones added by this call). Model chosen uniformly from `models` (added to the palette by name), variant uniform in [0, variants), yaw uniform [0, 2π), scale uniform [scaleMin, scaleMax]; all from a hash of `seed` and the candidate index (deterministic). Returns plants added; 0 if `models` is empty or the palette would exceed `maxPalette`.
  - `size_t Erase(Layer& layer, glm::vec2 centre, float radius);` removes plants with distance ≤ radius, keeps order; unused palette entries stay.
  - `std::vector<vegexport::Placed> ToPlaced(const Layer& layer, const HeightField& units, float metresPerUnit, float metresPerTexel, const std::vector<vegexport::Model>& models);` model index by name; skipped if the name is not found or `variants == 0`, or position outside [0, (W−1)·p]×[0, (H−1)·p]; variant = `variant % variants`; y = bilinear height at position / p × metresPerUnit; layer = `vegetation::LayerOfKind(kind)`.
  - `std::vector<uint8_t> Pack(const Layer& layer);` 19 bytes per plant as in Global Constraints.
  - `bool Unpack(const uint8_t* data, size_t size, Layer& layer, std::string& error);` uses `layer.palette` (already set); fails when size is not a multiple of 19, a value is not finite, scale ≤ 0, or model ≥ palette size; on failure `layer.plants` untouched.

- [ ] **Step 1: Write the failing tests** in `PlacedPlantsTests.cpp`:
  - `"Scatter with count 1 puts the plant at the centre"`: Brush{centre (5,7), count 1, models {"oak"}, variants {3}} on extent (100,100) → 1 added, `position == (5,7)`, palette == {"oak"}, variant < 3, scale in [0.8, 1.2].
  - `"Scatter keeps the spacing and stays on the map"`: count 200, radius 30, spacing 3 at centre (10, 10) on extent (50, 50) → every pair ≥ 3 m apart, all inside [0, 50]², added < 200; a second identical call with another seed adds only plants ≥ 3 m from all earlier ones.
  - `"Scatter is deterministic"`: same layer + brush twice → equal layers.
  - `"Scatter spreads over several models"`: models {"a","b","c"}, count 300, spacing 0 → all three names used.
  - `"Erase removes plants inside the radius"`.
  - `"ToPlaced stands plants on the heights"`: 3×3 map units, row 0 = 0, row 1 = 1, row 2 = 2, metresPerUnit 10, pixel 2 → plant at (2, 1) has y == 5 (bilinear), x/z unchanged in metres.
  - `"ToPlaced skips unknown and unloaded models"`: palette {"oak","gone","unloaded"} with models [oak variants 2 kind "tree", unloaded variants 0] → only the oak plant, `model` = its index in models, `layer == TREES`.
  - `"ToPlaced skips plants outside the map"`: map 4×4, pixel 1 → plant at (3.5, 1) dropped, (3, 3) kept.
  - `"Pack and Unpack round trip"` and `"Unpack rejects damaged data"` (size 18, model index = palette size, NaN x, scale 0 → false, plants unchanged).
- [ ] **Step 2: Run** `out/build/x64-release/ptg_tests.exe -tc="*Scatter*,*Erase*,*ToPlaced*,*Pack*"` → fails to compile (no `PlacedPlants.h`).
- [ ] **Step 3: Implement** the interfaces above in `PlacedPlants.cpp`. Spacing check is brute force over the layer (hand-placed layers are thousands of plants).
- [ ] **Step 4: Run** the full `ptg_tests.exe` → `Status: SUCCESS!`.

### Task 2: Undo history holds the placed plants (`MapEdit.*`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/MapEdit.h`, `MapEdit.cpp`
- Modify callers: `ProceduralTerrainGenerator/src/subSystems/MapImportEdit.cpp` (`StepHistory`)
- Test: `ProceduralTerrainGenerator/tests/MapEditTests.cpp`

**Interfaces:**
- Consumes: `placedplants::Layer` (Task 1).
- Produces: `mapedit::Step::plants` (`std::optional<placedplants::Layer>`, none = the operation did not change them, last member so existing aggregate initialisers stay valid); `Restored::plants`; `History::Undo/Redo(HeightField& map, std::optional<float>& seaLevel, biomepaint::Layer& paint, placedplants::Layer& plants)`; `BytesOf` adds `plants->Bytes()`.

- [ ] **Step 1: Write the failing test** `"History restores placed plants only from their steps"`: push a map step (no plants), then a plants step holding layer A while the current layer is B; Undo → plants == A, `restored.plants`; Undo again (map step) → plants still A, `!restored.plants`; Redo twice → plants == B. Update existing Undo/Redo calls in the tests to pass a layer.
- [ ] **Step 2: Run** → compile error (`plants` not a member).
- [ ] **Step 3: Implement**; add `placedplants::Layer placedPlants`, `bool placedUploadStale` and `PlacedPlantsChanged()` (Task 5 Interfaces) to `MapImportSys.h` now; `StepHistory` moves `placedPlants` in and out like `biomePaint` and calls `PlacedPlantsChanged()` when `restored->plants`.
- [ ] **Step 4: Run** full tests → SUCCESS.

### Task 3: Project v7 stores the placed plants (`MapProject.*`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/MapProject.h`, `MapProject.cpp`
- Test: `ProceduralTerrainGenerator/tests/MapProjectTests.cpp`

**Interfaces:**
- Consumes: `placedplants::Pack/Unpack/Layer` (Task 1).
- Produces: `Project::placedPlants` (`placedplants::Layer`); `currentVersion = 7`; header and block only when the layer has plants; error text `"The placed plants of the project are damaged"`.

- [ ] **Step 1: Write the failing tests**: `"Project keeps its placed plants"` (round trip equal), `"Version 6 project has no placed plants"` (version digit patched as in the vegetation test, block absent → empty layer), `"Damaged placed plants fail"` (palette of 1 name and a plant with model 1 → false, error not empty, `IsMarker(read)`); bump `currentVersion == 7` in `"Project keeps its biome paint"`.
- [ ] **Step 2: Run** → compile error.
- [ ] **Step 3: Implement** (block written after the paint block; read only for `version >= 7`; repeated block → error as for paint).
- [ ] **Step 4: Run** full tests → SUCCESS.

### Task 4: Export writes the hand-placed plants (`VegetationExport.*`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/VegetationExport.h`, `VegetationExport.cpp` (`Write`)
- Test: `ProceduralTerrainGenerator/tests/VegetationExportTests.cpp`

**Interfaces:**
- Produces: `vegexport::Input::extra` (`std::vector<Placed>`, metres in the axes of the pack, model = index into `models`): appended after the placed plants in `Write`, counted in `Result::instances`, not limited, written even when `kept == NONE`.

- [ ] **Step 1: Write the failing test** `"Write adds the extra plants"`: an existing small `Write` setup with `layerMask = 0` and two `extra` plants → `result.instances == 2` and the JSON instance list holds their positions.
- [ ] **Step 2: Run** → compile error.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** full tests → SUCCESS.

### Task 5: Vegetation tab, brush and drawing in the Imported map

**Files:**
- Modify: `ProceduralTerrainGenerator/src/subSystems/MapImportSys.h`, `MapImportSys.cpp` (tabs, `Draw`, `InstallMap`, `ShowUnits`, project save/load, `ImGuiSaving`, `ReloadShaders`)
- Modify: `MapImportVegetation.cpp` (tab, brush, upload), `MapImportBiomes.cpp` (remove `PlantsImGui()` call; turning the biome brush on turns the plant brush off), `MapImportSculpt.cpp` (dispatch, exclusivity, Shift + wheel radius, outline radius)

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces (members of `MapImportSys`):
  - `placedplants::Layer placedPlants; VegetationLayer placedLayer; bool placedUploadStale = true; void PlacedPlantsChanged() { placedUploadStale = true; dirty = true; }`
  - `bool plantBrushOn = false; std::vector<std::string> brushModels; float plantRadius = 10 (m); int plantCount = 1; float plantSpacing = 2 (m); float plantScaleMin = 0.8f, plantScaleMax = 1.2f; uint32_t plantSeed = 0;`
  - `struct PlantStroke { placedplants::Layer before; glm::vec2 lastDab; bool changed = false; bool erase = false; }; std::optional<PlantStroke> plantStroke;` included in `Busy()`.
  - `void UpdatePlantBrush(const Camera&, bool active)`, `void FinishPlantStroke()`, `void UploadPlacedPlants()`, `void VegetationImGui()`, `const LightSource* lastLight = nullptr` (set in `Draw`).
- Behaviour:
  - Tabs: `Project, Edit, Biomes, Vegetation, Load`. `VegetationImGui()`: SeparatorText "Automatic plants" + the former `PlantsImGui` body; SeparatorText "Plant brush": checkbox "Place plants with the mouse" (turns Sculpt and the biome brush off), a checkbox per ready model ("Models of the brush", none checked → hint and no placing), Count 1–50 ("1 = one plant at the cursor"), Radius (m) 0.5–500 log, Spacing (m) 0–50, Scale min/max 0.25–4, hint "Left button: place, Ctrl: erase, Shift + wheel: radius", "Clear placed plants" (history step, disabled when empty), "Placed: N"; SeparatorText "Models": `vegetationModels->ImGuiDraw(*lastLight)` when both exist.
  - Stroke: press starts (`before` = copy of the layer, `erase` = Ctrl); a dab at the press and then whenever the cursor moved ≥ `max(plantSpacing, plantRadius * 0.5f, pixel)` metres since `lastDab`; count 1 places at the cursor; erase uses `plantRadius`; release → history step "Place plants" / "Erase plants" when `changed`. Cursor metres = `*hoverTexel * pixelMetres.x`. Brush seed `++plantSeed` per dab.
  - `BrushOn()` includes `plantBrushOn`; `ActiveBrushRadius()` = `plantRadius / pixel` texels for it.
  - Upload: in `UpdatePlants()` when `placedUploadStale` and not mid-change: `ToPlaced(placedPlants, map, metresPerUnit, pixel, PlantInput().models)` → `ToAppInstances(..., texelZero, metresPerUnit, pixel)` → `placedLayer.SetExtraInstances` (or Clear when empty / no models). Marked stale by every dab, Undo of a plant step, `ShowUnits` (heights), ready-model count change, project load.
  - `placedLayer`: `SetUnitsPerMetre` with `vegetation` in `ShowUnits`; drawn right after `vegetation.Draw` (after `PickUnderCursor`); keeps the default drawing settings (the "Drawing" node edits the automatic layer only); `ReloadShaders` reloads it.
  - `InstallMap`: empty `placedPlants` and stale upload; project load puts its layer back after; save writes it.
  - Export (`ImGuiSaving`): `PlantInput()` fills `extra = ToPlaced(...)`; the vegetation source is offered when models are ready and (automatic plants are on with biomes current) or `placedPlants` has plants; with automatic plants off, `layerMask = 0` and ids are a zero `BiomeMap` of the map size; `Kept::NONE` blocks the export only when there are no hand-placed plants.

- [ ] **Step 1: Implement** the above; build with `/W4 /WX`.
- [ ] **Step 2: Run** full `ptg_tests.exe` → SUCCESS; `ProceduralTerrainGenerator.exe --gpu-check` → `All checks passed`.
- [ ] **Step 3: Docs**: `docs/ROADMAP.md` D4 entry gets the tab, the brush and a manual check list (place single, scatter 3 models, erase, Undo/Redo, sculpt under plants, Save → Open, export count = auto + placed); `CLAUDE.md` mentions `PlacedPlants.*`, `.ptgmap` v7 and the tab.
