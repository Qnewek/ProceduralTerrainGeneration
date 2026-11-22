# Faza 6 D1: biomy z klimatu w Imported map — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kopia robocza Imported map dostaje biomy liczone z klimatu i terenu (id, kolory, splat materiałów), edytowane tym samym edytorem co w TerrainGen, zapisywane w projekcie i eksportowane jako mapy biomów i materiałów.

**Architecture:** CPU (`terrainGeneration/MapBiomes.*`, `ptg_core`, testy): ustawienia biomów projektu + JSON, kontynentalność (odległość od brzegu ze znakiem) i górzystość (lokalna rzeźba) z mapy w metrach, w wątku tła. GPU: nowy `map_biome_compute.shader` na wspólnych `noise.glsl` / `biomes.glsl` czyta warstwę TERRAIN + teksturę C/M i pisze warstwę BIOMES, id i splat (`GpuHeightGenerator::GenerateMapBiomes`); `biome_compute.shader` nietknięty. `MapImportSys`: zakładka Biomes, przeliczanie po zmianie mapy/ustawień, projekt `.ptgmap` v3, Saving z mapami biomów i materiałów.

**Tech Stack:** C++20, OpenGL 4.5 compute, glm (`packHalf2x16`), nlohmann/json, Dear ImGui, doctest.

**Spec:** projekt etapu D zatwierdzony w rozmowie 2026-11-22 (bez pliku specyfikacji — właściciel nie chce `docs/superpowers/specs`); ten plan = etap D1. D2 (pędzel), D3 (pokrycie terenu), D4 (roślinność) — osobne plany.

## Global Constraints

- Źródło automatyczne w D1: tylko klimat. Kolejność źródeł z projektu (malowany → pokrycie → klimat) przyjdzie w D2/D3; nie budować jej teraz.
- Parametry biomu na teksel: C = odległość od brzegu ze znakiem (+ ląd, − woda) / `coastMetres`, przycięta do [-1, 1] (bez morza: C = 1 wszędzie); M = `2 · relief / reliefMetres − 1` przycięte do [-1, 1], relief = max − min w oknie o promieniu `reliefRadiusMetres`; W = 0; T, H = szumy `BiomeConfig::temperature/humidity` w pozycji `climateOrigin + teksel · piksel [m]`; potem `applyClimate` z `biomes.glsl` bez zmian.
- Wysokość dla klimatu (jednostki terenu TerrainGen): `h' = (metry − baseMetres) / heightUnitMetres`; `waterHeight = morze ? clamp((morze − base) / unit, 0, 1) : 0`. Teksel płaskiego morza (morze − 1 m ≤ metry ≤ morze) dostaje `h' = waterHeight · (1 − |C|)` (sztuczna głębia rosnąca od brzegu, inaczej morze DEM = plaża).
- Domyślnie: `heightUnitMetres` 256, `baseMetres` = morze − `TerrainConfig{}.WaterHeight()` · 256 (bez morza 0), `coastMetres` 20000, `reliefRadiusMetres` 1000, `reliefMetres` 600, `climateOrigin` (0, 0), `enabled` false. Mapa wysłana z TerrainGen: tabela/klimat/szumy z jej presetu, `heightUnitMetres` = `Pack::metresPerUnit`, `baseMetres` 0, `climateOrigin` = `Pack::worldOrigin`, `enabled` = TerrainGen miał biomy.
- Pozycja szumu łat materiałów = ta sama pozycja w metrach (rozmiar łat w metrach jak w TerrainGen na 1 m).
- Modyfikatory terenu (`BiomeConfig::modifyTerrain`) w Imported map nie działają i nie są pokazywane.
- Ustawienia biomów to nie krok historii (jak rzeki); ich zmiana ustawia `dirty` projektu.
- `.ptgmap` `currentVersion` = 3: pole nagłówka `"biomes"` (string JSON); wersje 1–2 nadal czytane (brak pola = ustawienia domyślne).
- Konwencje z CLAUDE.md: `(width, height)`, `Grid<T>`, komentarze po angielsku, `/W4 /WX`, nowe `.cpp` w `CMakeLists.txt`, edycje plików narzędziami Edit/Write, nowy shader generacji = sekcja w `--gpu-check`.

## Review Focus

1. Mapa bez morza (DEM gór, `seaLevelMetres` puste): C = 1, brak sztucznej głębi, `waterHeight` 0 — biomy lądowe, bez wody i bez NaN.
2. Mapa 8192²: liczenie C/M w tle (nie blokuje klatki), pamięć tekstury C/M = 4 bajty na teksel (RG16F), stary wynik (`mapVersion`) odrzucony po kolejnej zmianie.
3. Zmiana mapy w trakcie liczenia (pociągnięcie pędzla, Cofnij, Open): biomy przeliczone dla nowej mapy, nie dla starej; w trakcie pociągnięcia nie startuje nowe liczenie.
4. Projekt z uszkodzonym/nieznanym JSON biomów: projekt się otwiera, biomy domyślne, komunikat w statusie (jak przy rzekach).
5. Tabela biomów zmieniona w edytorze (dodanie, usunięcie, kolejność) i eksport zaraz potem: id w `_biome_ids.png` i lista w manifeście z tej samej tabeli co mapa na ekranie.

Testy: 1 → Task 2 (`"No sea gives continentalness 1"`), 4 → Task 1 (`"Bad map biome JSON is rejected"`) + obsługa w Task 4, 5 → Task 5 (krok sprawdzenia w aplikacji), 2–3 → Task 4 (`version`), krok ręczny.

---

### Task 1: Ustawienia biomów projektu i JSON

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/Preset.h`, `Preset.cpp` (wydzielenie bloku `"biomes"` z `ToJson(WorldConfig)` do `BiomesToJson`)
- Create: `ProceduralTerrainGenerator/src/terrainGeneration/MapBiomes.h`, `MapBiomes.cpp`
- Create: `ProceduralTerrainGenerator/tests/MapBiomesTests.cpp`
- Modify: `ProceduralTerrainGenerator/tests/PresetTests.cpp`, `CMakeLists.txt` (MapBiomes.cpp → `ptg_core`, test → `ptg_tests`)

**Interfaces:**
- Produces (Preset):
  - `std::string preset::BiomesJson(const BiomeConfig& config);` — obiekt `"biomes"` presetu świata (ten sam JSON, `ToJson(WorldConfig)` używa tej samej funkcji).
  - `bool preset::BiomesFromJson(const std::string& text, BiomeConfig& out, std::string& error);` — wzór `RiversFromJson`: `ReadBiomes` + `CheckBiomes`, `out` zmieniane tylko przy sukcesie.
- Produces (`namespace mapbiomes`, `MapBiomes.h`):
  - `struct Settings { bool enabled = false; float heightUnitMetres = 256.0f; float baseMetres = 0.0f; float coastMetres = 20000.0f; float reliefRadiusMetres = 1000.0f; float reliefMetres = 600.0f; glm::vec2 climateOrigin = glm::vec2(0.0f); BiomeConfig config = BiomeConfig::Default(); };`
  - `Settings Default(std::optional<float> seaLevelMetres);` — wartości z Global Constraints (`baseMetres` z morza).
  - `Settings FromGeneration(const std::string& generationJson, glm::ivec2 worldOrigin, float metresPerUnit, std::optional<float> seaLevelMetres, bool biomesOn);` — preset świata → `config = world.biomes`, unit = `metresPerUnit`, base 0, origin = worldOrigin, `enabled = biomesOn`; JSON nie-świata (Noise) lub błędny → `Default(sea)` z `climateOrigin = worldOrigin`, `enabled` false.
  - `float WaterHeight(const Settings&, std::optional<float> seaLevelMetres);`
  - `std::string ToJson(const Settings&);` / `bool FromJson(const std::string& text, Settings& out, std::string& error);` — pola z nazwami jak w `Settings` + `"config"` = `preset::BiomesJson`; brak pola = wartość domyślna; `heightUnitMetres`, `coastMetres`, `reliefRadiusMetres`, `reliefMetres` muszą być > 0 i skończone (inaczej błąd).

- [ ] **Step 1: Testy**
  - `PresetTests`: `"Biome config survives its own JSON"` — `BiomeConfig` z `Distinct*`/`CheckSame*` (istniejące helpery dla biomów) przez `BiomesJson` → `BiomesFromJson`; `"Biome config JSON rejects an empty table"`.
  - `MapBiomesTests`: `"Map biome settings survive JSON"` (wszystkie pola różne od domyślnych, w tym `climateOrigin` i jeden biom tabeli); `"Bad map biome JSON is rejected"` (nie-JSON, `heightUnitMetres` 0, `coastMetres` −1 → false, `out` bez zmian); `"Default base puts the sea at the TerrainGen water height"` (`Default(100.0f)`: `WaterHeight(s, 100) == Approx(TerrainConfig{}.WaterHeight())`; `Default(nullopt).baseMetres == 0`); `"Settings from a TerrainGen preset keep its biomes"` (`preset::ToJson(world)` z inną tabelą i seedem temperatury → `FromGeneration(json, {512, -256}, 256, sea, true)`: tabela i seed jak w świecie, `climateOrigin == (512, -256)`, `heightUnitMetres == 256`, `baseMetres == 0`, `enabled`); `"Settings from a noise preset are defaults"` (`preset::ToJson(NoiseMapConfig{})` → `enabled` false, origin zachowany).
- [ ] **Step 2:** `cmake --build --preset x64-release` → błąd kompilacji (brak `mapbiomes`, `BiomesJson`).
- [ ] **Step 3:** Implementacja wg Interfaces.
- [ ] **Step 4:** `ctest --preset x64-release --output-on-failure` → PASS.
- [ ] **Step 5:** Bez commita (właściciel commituje sam); `git status` pokazuje tylko pliki tego taska.

### Task 2: Kontynentalność i górzystość z mapy (CPU)

**Files:**
- Modify: `MapBiomes.h`, `MapBiomes.cpp`, `tests/MapBiomesTests.cpp`

**Interfaces:**
- Produces:
  - `HeightField CoastDistance(const HeightField& metres, glm::dvec2 pixelMetres, float seaLevelMetres);` — odległość w metrach (środki tekseli, osie z `pixelMetres.x/y`) do najbliższego teksela drugiego rodzaju; ląd (`h > sea`) dodatnia, woda ujemna; mapa bez drugiego rodzaju → ±`FLT_MAX`. Dokładna euklidesowa transformata odległości (Felzenszwalb–Huttenlocher, dwa przebiegi 1D z wagami osi), O(n).
  - `HeightField LocalRelief(const HeightField& metres, glm::dvec2 pixelMetres, float radiusMetres);` — max − min w prostokącie ±`rx`, ±`ry` tekseli, `rx = max(1, round(radius / pixel.x))` (tak samo y), przycięty do mapy; separowalne min/max z kolejką monotoniczną, O(n).
  - `struct ClimateMaps { int width = 0, height = 0; std::vector<uint32_t> packed; };` — `glm::packHalf2x16(vec2(C, M))` na teksel, wiersze od y = 0 (pod `GL_RG16F`, `GL_RG`, `GL_HALF_FLOAT`).
  - `ClimateMaps ComputeClimate(const HeightField& metres, glm::dvec2 pixelMetres, std::optional<float> seaLevelMetres, const Settings& settings);` — wzory C i M z Global Constraints.
  - `float ClimateHeight(float metres, float continentalness, std::optional<float> seaLevelMetres, const Settings& settings);` — `h'` z Global Constraints (wzorzec CPU dla shadera i `--gpu-check`).

- [ ] **Step 1: Testy** (mapa 40×24):
  - `"Coast distance is signed and in metres"`: woda (−10 m) dla x < 20, ląd (10 m) dla x ≥ 20, piksel (2, 3), morze 0 → teksel x = 25: 12; x = 17: −6; dowolne y.
  - `"Coast distance follows the pixel size of each axis"`: woda dla y < 12, piksel (2, 3) → (5, 15): 9; (5, 9): −9.
  - `"Local relief sees a spike within the radius only"`: płasko 0, szczyt 100 m w (10, 10), piksel (2, 1), promień 4 m (rx 2, ry 4) → (12, 10): 100, (13, 10): 0, (10, 14): 100, (10, 15): 0.
  - `"No sea gives continentalness 1"`: `ComputeClimate(..., nullopt, ...)` → `unpackHalf2x16(...).x == 1` wszędzie.
  - `"Climate maps clamp and scale"`: `coastMetres` 10, `reliefMetres` 100 → ląd 40 m od brzegu: C ≈ 1; woda 4 m: C ≈ −0,4; relief 50: M ≈ 0; relief 0: M = −1 (tolerancja 2e-3, połówki).
  - `"Climate height maps metres to terrain units"`: unit 100, base −30, morze 0 (waterHeight 0,3) → ląd 50 m: 0,8; płaskie morze (0 m) z C = −1: 0; z C = −0,5: 0,15; głęboka woda −20 m: 0,1; bez morza 50 m: (50 + 30) / 100.
- [ ] **Step 2:** Build → błąd kompilacji.
- [ ] **Step 3:** Implementacja.
- [ ] **Step 4:** `ctest` → PASS; dodatkowo jednorazowo (nie w CI) czas `ComputeClimate` na 8192² w `ptg_tests.exe` z `--no-skip` jako `"Climate maps timing"` z `doctest::skip()` (jak „Mesh export timing”) — oczekiwane < 5 s w Release.
- [ ] **Step 5:** Bez commita.

### Task 3: Shader biomów mapy i sekcja `mapbiomes:` w `--gpu-check`

**Files:**
- Create: `ProceduralTerrainGenerator/res/shaders/ComputeShaders/map_biome_compute.shader`
- Modify: `ProceduralTerrainGenerator/src/opengl/GpuHeightGenerator.h`, `GpuHeightGenerator.cpp`, `GpuHeightCheck.cpp`

**Interfaces:**
- Consumes: `mapbiomes::Settings`, `mapbiomes::WaterHeight`, `mapbiomes::ClimateHeight`, `mapbiomes::ComputeClimate` (Task 1–2).
- Produces:
  - `struct MapBiomeInput { const TextureClass& heights; const TextureClass& climate; float metresPerUnit; glm::vec2 texelMetres; };` (`heights` = R32F jednostki mapy, `climate` = RG16F z `ClimateMaps`).
  - `void GpuHeightGenerator::GenerateMapBiomes(const mapbiomes::Settings& settings, std::optional<float> seaLevelMetres, const MapBiomeInput& input, TextureClass& colors, TextureClass& ids, TextureClass* splat);` — rozmiar z `colors`; `colors` RGBA32F, `ids` R32I, `splat` jak w `GenerateBiomes`.

- [ ] **Step 1: Shader.** Układ jak `biome_compute.shader` (grupa 32×32, `tileOrigin`, `DispatchTiles`); includy `noise_params.glsl`, `world_input.glsl`, `noise.glsl`, `biomes.glsl` (bez `terrain.glsl`, `rivers.glsl`). Wejścia: `heights` (`r32f` readonly, binding 3), `climateMap` (sampler2D lub `rg16f` readonly image, binding 4); uniformy `metresPerUnit`, `hasSea`, `seaLevel`, `heightUnit`, `baseMetres`, `positionOrigin`, `texelMetres` + `waterHeight`, `climate`, `biomeCount`, `blendWidth`, materiały z `biomes.glsl`. Na teksel: `position = positionOrigin + texel · texelMetres`; `values = {C, M, 0, evaluateNoise(3, position), evaluateNoise(4, position), 0}`; `h'` wg `ClimateHeight`; `applyClimate(values, h')`; dalej pętla id/kolor/splat jak w `biome_compute.shader` bez gałęzi modyfikatorów i rzek, `finishMaterials(weights, position)`. Komentarz nagłówka: wzorzec CPU = `mapbiomes::ClimateHeight` + `biome::ApplyClimate` / `Nearest` / `MixMaterials`.
- [ ] **Step 2: `GenerateMapBiomes`** — `FillWorldInput` nie pasuje (teren): wypełnij `GpuWorldInput` tylko szumami 3, 4 i tablicą `detailTable` (`SetNoise`, `SetPermutation(..., detailSeed)`), `UploadBiomeTable(settings.config)`, `islandSize` = rozmiar mapy; nowy `mapBiomeShader` w `Initialize`/`ReloadShaders`.
- [ ] **Step 3: Testy GPU** (`CheckMapBiomes(GpuHeightGenerator&)` w `GpuHeightCheck.cpp`, wywołane obok `CheckSculpt`, nazwy przypadków z prefiksem `mapbiomes:`), mapa 40×24, piksel (2, 3), morze 0, wysokości rampą −50…300 m, C/M z `ComputeClimate`:
  - `mapbiomes: ids and colors match the CPU` — tabela: domyślna z zakresami T i H ustawionymi na [-1, 1] (wynik nie zależy od szumów) → `ids` = `biome::Nearest(table, ApplyClimate({C, M, 0, 0, 0, 0}, ClimateHeight(...), WaterHeight(...), climate))` dla każdego teksela; kolory ≤ 1e-5 od mieszanki `BlendWeight` na CPU.
  - `mapbiomes: splat matches the CPU without patches` — `materialPatchiness` 0 → splat (8 bit) ≤ 1/255 od `biome::MixMaterials` (patch dowolny, bo k = 0).
  - `mapbiomes: deterministic` — dwa wywołania, `ids` i `colors` bit w bit.
  - `mapbiomes: temperature noise matters` — tabela dzielona po T (dwa biomy: T < 0, T ≥ 0) → dwa różne seedy temperatury dają różne `ids`.
- [ ] **Step 4:** Build; `out\build\x64-release\ProceduralTerrainGenerator.exe --gpu-check` → `All checks passed`, sekcje `world:`, `chunk:`, `sculpt:` bez zmian, nowe `mapbiomes:` ok.
- [ ] **Step 5:** Bez commita.

### Task 4: Biomy w `MapImportSys` — stan, przeliczanie, projekt v3, zakładka Biomes

**Files:**
- Create: `ProceduralTerrainGenerator/src/utility/BiomeSettingsPanel.h`, `BiomeSettingsPanel.cpp` (sekcje wydzielone z `TerrainGenerationSys::BiomesEditor`)
- Create: `ProceduralTerrainGenerator/src/subSystems/MapImportBiomes.cpp` (część `MapImportSys`, jak `MapImportSculpt.cpp`)
- Modify: `TerrainGenerationSys.cpp` (używa panelu), `utility/BiomeEditor.h/.cpp` (ukrywanie sekcji „Terrain modifier”), `MapImportSys.h`, `MapImportSys.cpp`, `TerrainGenApp.cpp` (przekazuje nazwy materiałów i modele roślin), `MapProject.h/.cpp`, `tests/MapProjectTests.cpp`, `utility/SendToImported.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1–3.
- Produces:
  - `namespace biomepanel { bool Blend(BiomeConfig&); bool Climate(biome::Climate&); bool Materials(BiomeConfig&, bool showSeed); }` — te same suwaki i podpowiedzi co dziś w TerrainGen (`showSeed` = „Detail seed” w sekcji materiałów, bo w Imported map nie ma sekcji modyfikatorów); `@return` zmiana.
  - `mapproject::Project::biomes` (`std::string`, JSON `mapbiomes::ToJson`; puste = domyślne); `currentVersion = 3`.
  - `void BiomeEditor::ShowTerrainModifiers(bool show);` (domyślnie true; Imported map ustawia false, bo modyfikatory tam nie działają).
  - `MapImportSys::SetMaterialNames(std::vector<std::string>)` i `MapImportSys::SetVegetationModels(const VegetationModels* models)` (lista `BiomeEditor::PlantModel` budowana jak w `TerrainGenerationSys::SetVegetationModels`; same rośliny na mapie — D4); `TerrainGenApp` woła oba obok wywołań dla `terrainGenSys` (i `SetVegetationModels(nullptr)` przy zamykaniu); prywatnie: `mapbiomes::Settings biomeSettings`, `BiomeEditor biomeEditor`, `GpuHeightGenerator biomeGenerator`, `std::unique_ptr<TextureClass> climateTexture, biomeIds`, `BiomeMap biomeMap`, `GenerationJob<ClimateResult> climateJob` (`ClimateResult { mapbiomes::ClimateMaps maps; uint64_t version; double ms; }`), flagi `climateStale` (mapa, morze, unit/base/coast/relief) i `biomesStale` (tabela, klimat, szumy, mieszanie, materiały, origin), `void UpdateBiomes()` (co klatkę w `Update`, wątek GL), `void BiomesImGui()`.

- [ ] **Step 1: Test projektu** (`MapProjectTests`): `"Project keeps its biome settings"` (pole `biomes` przez `Serialize`/`Deserialize`); `"Version 2 project has no biome settings"` (plik z `"version": 2` bez pola → `biomes` puste).
- [ ] **Step 2:** `MapProject` v3 → test PASS.
- [ ] **Step 3: Panel.** `BiomeSettingsPanel` + `TerrainGenerationSys::BiomesEditor` przepisany na `biomepanel::*` (wygląd i działanie TerrainGen bez zmian).
- [ ] **Step 4: Przeliczanie.** `OnMapChanged()` ustawia też `climateStale`; `UpdateBiomes()`: gdy `enabled` i `climateStale` i brak pociągnięcia i `climateJob` wolny → start z `WorkingMetres()`, `pixelMetres`, morzem, kopią ustawień, `mapVersion`; wynik z inną `mapVersion` odrzucany (wtedy `climateStale` znowu); wynik → `climateTexture` (`GL_RG16F`, `Upload(..., GL_RG, GL_HALF_FLOAT)`) i `biomesStale`; gdy `biomesStale` i tekstura gotowa → `GenerateMapBiomes` do `view.LayerTexture(BIOMES)`, `biomeIds`, `view.SplatTexture`, potem odczyt id do `biomeMap`. Wyłączenie biomów: `view.ReleaseSplat()`, `biomeMap` czyszczona, tryb BIOMES → GREYSCALE; włączenie: tryb inny niż TEXTURED → BIOMES. Nowa mapa (`InstallMap`, `InstallProject`, `InstallReceived`) = ustawienia z `project.biomes` (`FromJson`; błąd → `Default(sea)` + status jak przy rzekach) albo `Default(sea)`; `SaveProject` zapisuje `ToJson(biomeSettings)`. `SendToImported`: `project.biomes = ToJson(FromGeneration(project.generation, pack.worldOrigin, pack.metresPerUnit, project.seaLevel, pack.biomes.has_value()))`.
- [ ] **Step 5: Zakładka „Biomes”** (`PanelTabs` w `ImGuiRightPanel`, po Edit): checkbox „Biomes”; „Climate from terrain”: „Height of terrain 1 (m)” (`heightUnitMetres`, 16–10000, log), „Height 0 at (m)” (`baseMetres`) + przycisk „Fit to sea level” (`baseMetres` jak w `Default`, nieaktywny bez morza), „Coast distance (km)” (1–200), „Relief radius (m)” (10–10000, log), „Relief of mountains (m)” (10–5000, log), „Climate origin (m)” (`InputFloat2`); `biomepanel::Blend/Climate/Materials(…, true)`; szumy: przyciski Temperature / Humidity + `utilities::NoiseImGui` (bez podglądu); „Load biomes from preset” = lista `preset::List("presets")`, wczytanie `preset::Load(WorldConfig)` → `config = world.biomes` (błąd → status); `biomeEditor.ImGuiDraw(config.table)`; status „Computing climate…” / czasy CPU i GPU. Każda zmiana: odpowiednia flaga + `dirty = true`.
- [ ] **Step 6:** Build, `ctest`, `--gpu-check` → PASS. Ręcznie w aplikacji (właściciel): DEM z morzem i bez, mapa wysłana z TerrainGen (biomy podobne do TerrainGen), TEXTURED, Cofnij po pędzlu przelicza biomy, Save → Open zachowuje ustawienia.
- [ ] **Step 7:** Bez commita.

### Task 5: Eksport map biomów i materiałów + dokumentacja

**Files:**
- Modify: `ProceduralTerrainGenerator/src/utility/ExportPanel.h`, `ExportPanel.cpp` (dwie fabryki źródeł), `TerrainGenerationSys.cpp` (używa ich), `MapImportSys.cpp` (`ImGuiSaving`), `CLAUDE.md`, `docs/ROADMAP.md`

**Interfaces:**
- Produces:
  - `static ExportPanel::MaterialSource ExportPanel::SplatSource(const TextureClass& splat, std::vector<std::string> names);` (dzisiejsza lambda z TerrainGen)
  - `static ExportPanel::BiomeSource ExportPanel::BiomeTableSource(const std::vector<biome::Biome>& table, std::function<BiomeMap()> readIds, const TextureClass& colors);`

- [ ] **Step 1:** Fabryki + TerrainGen na nich (eksport TerrainGen bez zmian w plikach wyjściowych).
- [ ] **Step 2:** `MapImportSys::ImGuiSaving`: gdy `biomeSettings.enabled`, splat, BIOMES i `biomeMap` mają rozmiar mapy → `pack.materials` / `pack.biomes` z fabryk (tabela = `biomeSettings.config.table`); inaczej `layersMissing = "Material and biome maps: turn on biomes in the Biomes tab."`; komunikat roślinności zostaje (D4).
- [ ] **Step 3:** Build, `ctest`, `--gpu-check` → PASS. Eksport paczki z Imported map z biomami: pliki `_splat_*.png`, `_biome_ids.png`, `_biome_colors.png`, manifest z listą biomów; po zmianie kolejności biomów w edytorze eksport ma id zgodne z listą (Review Focus 5).
- [ ] **Step 4: Dokumentacja.** CLAUDE.md: w strukturze `MapBiomes.*`, `MapImportBiomes.cpp`, `BiomeSettingsPanel.*`, `map_biome_compute.shader`; w pułapkach Imported map akapit „Biomy (D1)” (wzory C/M/h', flagi przeliczania, projekt v3, wzorzec CPU dla shadera, `mapbiomes:` w `--gpu-check`, modyfikatory wyłączone). ROADMAP Faza 6 punkt D: 🟡 D1 z datą i listą „do sprawdzenia w aplikacji”.
- [ ] **Step 5:** Bez commita; podsumowanie dla właściciela.
