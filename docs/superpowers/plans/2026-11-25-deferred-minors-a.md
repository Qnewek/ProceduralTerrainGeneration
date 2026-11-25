# Deferred minors, batch A — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the visible bugs and robustness gaps left as deferred minors in earlier stages (batch A, approved in chat 2026-11-25).

**Architecture:** Small independent fixes. Core logic (project file, history) gets unit tests in `ptg_tests`; GL/ImGui-bound fixes are verified by build + `--gpu-check` + manual list; the Godot importer by `tools\check_godot_import.bat`.

**Tech Stack:** C++20, doctest, OpenGL 4.5 / ImGui app, GDScript (Godot 4.7.2 headless check), C# (Unity, static check only).

**Spec:** none — scope = batch A of the chat triage (2026-11-25), confirmed in code before this plan. Rulings without a spec are provisional.

## Global Constraints

- No commits: the user commits after checking. Branch `fix/deferred-minors` from `feat/engine-import` (`694292c`).
- `/W4 /WX`; new `.cpp` files listed in `CMakeLists.txt`.
- `.ptgmap` format unchanged (no `currentVersion` bump): only stricter reading.
- Polish UI is not used: UI strings stay English like the rest of the app.

## Review Focus

1. A project file whose header promises an 8192² map but whose blocks are a few bytes → error message, no 256 MB+ allocation, no crash.
2. An edit that fails half-way (out of memory in `ShowMetres`) → no history step that undoes nothing; map, sea level and GPU layer unchanged.
3. Export from Imported map while the river network is being recomputed → no water of the previous map in the pack; a note says why.
4. Esc during a sculpt stroke → the map returns to its state before the stroke, no history step; the next stroke works.
5. Godot import of a pack whose splat PNG is missing → terrain is not black, one warning names the file.

---

### Task 1: Project file reading never allocates for a map its blocks cannot hold

**Files:** Modify `src/terrainGeneration/MapProject.cpp`; Test `tests/MapProjectTests.cpp`

- [ ] **Step 1: failing tests** in `MapProjectTests.cpp`:
  - `"Project: a block too small for its map is damage, found before reading it"` — serialize a 64×48 project, patch the header (re-serialize the JSON with `width`/`height` = 8192 and the same blocks) → `Deserialize` false, error contains `"too small for"`.
  - `"Project: a sea level that is not finite is damage"` — header `seaLevel` = 1e39 (written as a JSON number) → false, error contains `"sea level"`.
- [ ] **Step 2:** run `ptg_tests -tc="Project:*"` — Expected: the two new cases FAIL (first gives "The map data is damaged", second succeeds).
- [ ] **Step 3:** in `Deserialize`: before `map->Resize` / `biomepaint::Make`, check `bytes * zlibMaxRatio + 64 >= expected bytes` (float maps 4 B/texel, paint 2 B/texel; reuse the 1032 ratio from `DecompressPlants`, moved to namespace scope) → error `"The project block \"<name>\" is too small for a <w> x <h> map"`; also check the sum of block bytes ≤ bytes after the header before any allocation (`"The project file is cut short"`); catch `std::bad_alloc` around the allocation and decompression (`"Not enough memory for the project"`); `seaLevel` not finite → `"The sea level of the project is damaged"`.
- [ ] **Step 4:** `ptg_tests` — Expected: all pass.

### Task 2: A failed edit leaves no history step

**Files:** Modify `src/terrainGeneration/MapEdit.h/.cpp`, `src/subSystems/MapImportEdit.cpp`; Test `tests/MapEditTests.cpp`

**Interfaces:** Produces `void mapedit::History::DropLast()` — removes the step pushed last (the undo step on top) and its bytes; no-op when there is nothing to undo; redo steps untouched (Push already cleared them).

- [ ] **Step 1: failing test** `"History: dropping the last step"` — push A, B; `DropLast()` → `CanUndo()` true, `UndoLabel() == "A"`, `UsedBytes()` = bytes of A; `DropLast()` twice more → `CanUndo()` false, no crash.
- [ ] **Step 2:** run — Expected: compile error (no `DropLast`).
- [ ] **Step 3:** implement; in `MapImportSys::ApplyEdit` wrap `ShowMetres` in `try`: on `std::bad_alloc` restore `seaLevelMetres`, `history.DropLast()` when the push succeeded, rethrow (callers already catch and restore the GPU layer).
- [ ] **Step 4:** `ptg_tests` — Expected: pass.

### Task 3: Imported map — export, reload, erosion status, brush input

**Files:** Modify `src/subSystems/MapImportSys.cpp/.h`, `MapImportEdit.cpp`, `MapImportSculpt.cpp`. GL/ImGui-bound: verified by build + manual list.

- [ ] **Step 1:** `bool MapImportSys::RiversCurrent() const` = `!RiversOn() || (!mapChanged && !riverJob.IsRunning())`. Saving: river water only when `RiversCurrent()`, else `pack.layersMissing` gains `"River water: wait until the rivers are computed."` (append to an existing note with a newline). Automatic plants in the export use the river network too: `vegetationMissing = "Vegetation: wait until the rivers are computed."` when automatic plants are on and `!RiversCurrent()`.
- [ ] **Step 2:** reload of the same file (another value range) keeps the vertical exaggeration: `InstallResult` saves `heightScale / trueHeightScale` before `InstallMap` and applies it after, when `result.reload`.
- [ ] **Step 3:** `Erode()` returning on `Busy()` sets `"Erosion: wait until the current operation finishes"` (error) instead of leaving `"Eroding..."`.
- [ ] **Step 4:** brushes ignore a left press while the right or middle button is down (`paintPressed` in `SculptInputImGui`); Esc during a sculpt stroke cancels it: `view.SetLayer(TERRAIN, map)` (the CPU map is the state before the stroke), no history step, status `"Sculpt stroke cancelled"`. Biome-paint and plant strokes are left as they are (Ruling).
- [ ] **Step 5:** build + `ptg_tests` + `--gpu-check` — Expected: build clean, all pass.

### Task 4: TerrainGen — plants of the eroded copy follow vegetation settings

**Files:** Modify `src/opengl/VegetationLayer.h`, `src/subSystems/TerrainGenerationSys.cpp/.h`.

**Interfaces:** Produces `uint64_t VegetationLayer::SettingsVersion() const` (the existing `settingsVersion`, bumped when density, ready models, plants, water, seed, rivers or bounds change).

- [ ] **Step 1:** `TerrainGenerationSys` keeps `eroded.plantSettings`; in `UpdateErosion`, when the copy is shown and `SettingsVersion()` differs → `plantsWanted = true` and remember it (also remember it in `StartErodedPlants`).
- [ ] **Step 2:** build + `ptg_tests` — Expected: pass. Manual: Density slider with the eroded copy shown re-places its plants.

### Task 5: App window — closing

**Files:** Modify `src/subSystems/TerrainGenApp.cpp`.

- [ ] **Step 1:** after the main loop `glfwHideWindow(window)` first (background jobs and model loading finish behind a hidden window instead of a frozen one); in the loop, when a quit is requested with unsaved changes while the window is iconified, `glfwRestoreWindow(window)` so the "Unsaved changes" modal can be seen.
- [ ] **Step 2:** build — Expected: clean. Manual: close during a long job; close from the taskbar while minimized with unsaved changes.

### Task 6: Engine importers

**Files:** Modify `res/engine/godot/addons/ptg_import/ptg_builder.gd`, `ptg_terrain.gdshader`, `res/engine/unity/Editor/PtgPackImporter.cs`; Test `tests/engine/godot_import_check.gd`.

- [ ] **Step 1: failing check** — in `godot_import_check.gd` build mode for `full`: copy the pack folder (files only, recursively) to `user://ptg_no_splat/`, delete `full_splat_0.png`, open and build → expect a warning containing `"full_splat_0.png"` and the terrain material's `splat_missing` shader parameter true.
- [ ] **Step 2:** run `tools\check_godot_import.bat` — Expected: FAIL on the two new assertions.
- [ ] **Step 3:** builder: a splat image that fails to load → warning `"<pack>: <file> could not be read, terrain without that material map"` and `splat_missing = true`; shader: where the splat total is below 1e-3 use the fallback colours of the materials… simplest: `base_color_map` when set, else grey 0.5. Zero-plant guard: the "were not exported" warning only when `missing > 0`.
- [ ] **Step 4:** Unity: re-import finds the root among `SceneManager.GetActiveScene().GetRootGameObjects()` by name (also inactive). Roslyn syntax check.
- [ ] **Step 5:** `tools\check_godot_import.bat` — Expected: PASSED.

### Task 7: Docs

- [ ] ROADMAP: one paragraph under Faza 6 / Faza 5 krok 5 listing what batch A fixed and the manual checks; CLAUDE.md only if a convention changed (History::DropLast in the MapEdit line).
