# Roślinność w Imported map (D4) — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rośliny na mapie Imported map według jej biomów (klimat, pokrycie terenu, malowanie), w prawdziwej skali piksela, widoczne w aplikacji i w eksporcie paczki.

**Architecture:** Rdzeń rozmieszczania (`vegetation::PlaceChunk`) pracuje na siatce komórek w metrach; `ChunkMaps::step` = metry na teksel map (1 dla mapy 1 m, 2^L dla grubszych chunków, piksel dla DEM), więc przy kroku 1 i 2^L arytmetyka jest ta sama co dziś. `vegexport::PlaceMap` dzieli mapę o pikselu p na chunki po 128·max(1, ⌊p⌋) m, liczy nachylenie z wysokości (bez odczytu `surfaceMap`), sadzi na wątkach i przerywa po przekroczeniu limitu. Imported map ma własny `VegetationLayer` (tylko zestaw „extra”), zadanie w tle po każdym przebiegu biomów, ustawienia w `.ptgmap` v6, eksport tym samym wejściem.

**Tech Stack:** C++20, OpenGL 4.5, ImGui, doctest, nlohmann/json, `std::thread`.

**Spec:** projekt D4 zatwierdzony w czacie 2026-11-23 (decyzje właściciela: prawdziwa skala piksela — wariant A; siatka komórek w metrach — podejście 1; limit z 2026-11-22: > 2 mln instancji → tylko drzewa, drzew > 2 mln → brak, z komunikatem). Bez pliku spec (prośba właściciela).

## Global Constraints

- TerrainGen, świat nieskończony i eksport TerrainGen dają **bit w bit** te same rośliny co przed zmianą (krok 1 i 2^L, pozycja, skala, obrót, model, wariant).
- Komórki 8 / 4 / 8 m (`cellTexels`, teraz metry), hash z pozycji komórki w całych metrach; pozycja teksela (0, 0) mapy na siatce = `Input::mapTexel` (całe metry).
- Limit `vegexport::maxMapInstances = 2'000'000`: najpierw drzewa; drzew > limit → nic (`Kept::NONE`); potem krzewy i skały; razem > limit → same drzewa (`Kept::TREES_ONLY`).
- Nachylenie w Imported map = prawdziwe (metry na metry), niezależne od przewyższenia; rośliny stoją na terenie z przewyższeniem, model nie jest rozciągany w pionie (VS: `base.y * heightScale`, kształt × `scale`).
- Elewacja roślin w Imported map = ta, którą widzą biomy mapy: h' = (metry − `baseMetres`) / `heightUnitMetres`, woda h' = `mapbiomes::WaterHeight`.
- Nic nie przelicza się w trakcie pociągnięcia pędzla (Sculpt / Paint); jedno zadanie w tle naraz, wynik starszej wersji odrzucany.
- Poza zakresem: cienie, trawa (wymagają `ChunkWorld`), sadzenie na GPU, przerzedzanie, widok 2D (rośliny jak w TerrainGen — nie w 2D).
- `.ptgmap` `currentVersion = 6`; wersje 1–5 nadal czytane (rośliny włączone, gęstość 1).
- Konwencje repo (CLAUDE.md): `(width, height)`, komentarze po angielsku `//@param`, nowe `.cpp` w `CMakeLists.txt`, pliki zmieniane tylko narzędziami Edit/Write (widoczne diffy).
- Build: `build.bat` w scratchpadzie (VsDevCmd x64 → `cmake --preset x64-release` → `cmake --build --preset x64-release` → `ctest --preset x64-release`); po zmianie shadera cullingu lub rysowania `out\build\x64-release\ProceduralTerrainGenerator.exe --gpu-check` z katalogu repo.
- Commity robi właściciel po obejrzeniu w aplikacji — kroków commit w zadaniach nie ma.

## Review Focus

1. DEM 30 m 8192² (245 km): limit działa, zanim liczenie zajmie minuty — przerwanie po progu, UI nie stoi (zadanie w tle), komunikat zamiast pustki bez wyjaśnienia.
2. Mapa zmieniona w trakcie liczenia roślin (pędzel, Undo, nowa mapa, morze): wynik starszej wersji odrzucony, nowa mapa nie pokazuje roślin poprzedniej ani przez klatkę.
3. Piksel < 1 m (NMT 0,5 m) i mapa niekwadratowa: okno chunka pokrywa komórki, rośliny tylko między środkami skrajnych tekseli.
4. Mapa bez morza: nic nie znika przez poziom wody (woda = brak), elewacja jak w biomach (pod `baseMetres` ujemna).
5. Eksport w Imported map daje te same rośliny co ekran (to samo wejście, ten sam limit); przy `Kept::NONE` checkbox wyłączony z powodem.

Testy: 1 → Task 2 (`"PlaceMapLimited: trees over the limit give nothing, quickly"`), 3 → Task 2 (`"PlaceMap with half-metre pixels"`, `"PlaceMap with 30 m pixels"`), 4 → Task 1 (`"PlaceChunk: elevation seen through the height transform"`) + Task 5 (wejście bez morza), 2 i 5 → Task 5/6 (warunki w kodzie + lista ręczna w ROADMAP).

---

### Task 1: Rdzeń rozmieszczania w metrach (`VegetationPlacement.*`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/VegetationPlacement.h`, `VegetationPlacement.cpp`
- Modify: `ProceduralTerrainGenerator/src/opengl/VegetationLayer.cpp` (`StartPlacement`: `maps.firstTexel = glm::vec2(...)`, `maps.step = static_cast<float>(...)`)
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/VegetationExport.cpp` (to samo w `PlaceMap`, jeszcze bez nowej logiki)
- Test: `ProceduralTerrainGenerator/tests/VegetationPlacementTests.cpp`

**Interfaces:**
- Produces (namespace `vegetation`):
  - `ChunkMaps`: `glm::vec2 firstTexel` (metry siatki teksela 0 map), `float step = 1.0f` (metry na teksel map). Reszta bez zmian.
  - `PlacementSettings` + pola (domyślne = dzisiejsze zachowanie):
    - `float elevationScale = 1.0f, elevationOffset = 0.0f;` i `float elevationWater = std::numeric_limits<float>::quiet_NaN();` — elewacja = `biome::Elevation(h * elevationScale + elevationOffset, isnan(elevationWater) ? waterHeight : elevationWater)`.
    - `glm::vec2 riverOrigin = glm::vec2(0.0f); float riverPositionScale = 1.0f, riverHeightScale = 1.0f;` — test wody: `WaterLevelAt(*rivers, (texel - riverOrigin) * riverPositionScale) * riverHeightScale >= h`.
  - `struct ResolvedPlant { const biome::Plant* plant = nullptr; int model = 0; };` (przeniesione z anonimowej przestrzeni)
  - `using PreparedPlants = std::vector<std::array<std::vector<ResolvedPlant>, layerCount>>;`
  - `PreparedPlants PreparePlants(const std::vector<std::vector<biome::Plant>>& plants, const std::vector<ModelInfo>& models);` — wskaźniki do `plants`, który musi żyć dłużej.
  - `void PlaceChunk(const ChunkMaps&, const PreparedPlants&, const std::vector<ModelInfo>&, const PlacementSettings&, std::vector<Instance>& out);` — stara sygnatura woła `PreparePlants` + tę.
  - Komentarze `cellTexels` / `ChunkMaps` / `PlacementSettings` mówią „metry siatki (TerrainGen: teksel = 1 m)”.

- [ ] **Step 1: Test przypięcia na kodzie PRZED zmianą.** `"PlaceChunk: pinned output at step 1 and 2"`: FNV-1a 64 po polach każdej instancji (`position.x/y/z`, `scale`, `yaw` jako bity float, `model`, `variant`, `layer`) dla (a) `TestMaps(glm::ivec2(256, -128), wavy, glm::vec2(0.0003f, 0.0f), 0)` z `Plants(0.5f)` i `Settings(glm::ivec2(256, -128))`, (b) mapy grubej z testu „coarse chunk” (krok 2, `chunkTexels` 256, wszystkie warstwy). `wavy` jak w teście sąsiednich chunków. `CHECK(hash == 0x...ULL)` — wartości odczytane z pierwszego uruchomienia na niezmienionym rdzeniu (wpisz je do testu i do ledgera).
- [ ] **Step 2:** build.bat → test PASS na starym kodzie (to jest jego cel: zapisuje dzisiejszy wynik).
- [ ] **Step 3: Nowe testy:**
  - `"PlaceChunk: prepared plants give the same as the plain call"`: `PlaceChunk(maps, PreparePlants(p, models), models, s, a)` == `PlaceChunk(maps, p, models, s, b)` (pozycje, modele, warianty).
  - `"PlaceChunk: a fractional step samples the maps in metres"`: mapy 12×12 o kroku 30 (`firstTexel = (-30, -30)`, wysokość = 0.2 + 0.001 · x_metrów, id 0), `chunkTexel (0,0)`, `chunkTexels 256`, `worldUnitsPerTexel 1` → każda instancja: `0 ≤ x,z < 256`, `y == Approx(0.2 + 0.001 · x)` (dwuliniowo w metrach); cztery chunki po 128 (2×2) z mapami o kroku 1 (`TestMaps`, wysokość tą samą funkcją liniową — dwuliniowo dokładna przy obu krokach) dają te same pozycje (do 1 cm), modele i warianty.
  - `"PlaceChunk: elevation seen through the height transform"`: płaska mapa h = 0.5, `waterHeight` 0.1, kamień z zakresem elewacji {0.6, 1}: bez transformacji pusto; z `elevationScale 2, elevationOffset 0, elevationWater 0.1` (h' = 1.0 → elewacja 1) są kamienie; z `elevationWater` NaN i `waterHeight` 0.1 wynik jak `biome::Elevation(1.0, 0.1)`.
  - `"PlaceChunk: river water through the river frame"`: sieć `hydrology::Compute` misy z `HydrologyTests` (wzór misy przepisany do testu, region 60×42, jezioro wokół (30, 21)); mapa wysokości = misa w metrach siatki ×2 (`step` 2, `riverPositionScale 0.5`) → brak roślin w promieniu 4 m siatki od (60, 42); bez `rivers` są tam rośliny.
- [ ] **Step 4:** build.bat → nowe testy FAIL (błąd kompilacji = RED).
- [ ] **Step 5:** Implementacja: pola, `PreparePlants`, overload; w pętli `local = (texel - maps.firstTexel) / maps.step`; `step` dawniej `max(maps.step, 1)` — teraz `maps.step` (> 0; ≤ 0 = return). Wywołania w `VegetationLayer::StartPlacement` i `vegexport::PlaceMap` przepisane na `glm::vec2` / `float`.
- [ ] **Step 6:** build.bat → wszystkie testy PASS, w tym przypięte hashe bez zmiany wartości.

### Task 2: `PlaceMap` w metrach, nachylenie z wysokości, wątki i limit (`VegetationExport.*`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/VegetationExport.h`, `VegetationExport.cpp`
- Modify: `ProceduralTerrainGenerator/src/utility/ExportPanel.cpp` (linia statusu roślinności)
- Test: `ProceduralTerrainGenerator/tests/VegetationExportTests.cpp`

**Interfaces:**
- Consumes: Task 1 (`PreparePlants`, `PlaceChunk` z `PreparedPlants`, pola `PlacementSettings`).
- Produces (namespace `vegexport`):
  - `inline constexpr size_t maxMapInstances = 2'000'000;`
  - `Input` + `float metresPerTexel = 1.0f;` (piksel mapy, oś X) + `size_t limit = 0;` (0 = bez limitu); `mapTexel` = pozycja teksela (0, 0) na siatce w całych metrach (komentarz).
  - `PlaceMap(height, surface, ids, input)`: `surface.empty()` = nachylenie z `height` (różnice centralne, jednostronne przy krawędzi mapy, na metr: `(h[x+1] − h[x−1]) / (2 · metresPerTexel)`), wtedy `settings.heightScale = input.metresPerUnit`; niepusty `surface` musi mieć `height.Size()` jak dziś. Przy `limit > 0` przerywa, gdy ma ponad `limit` roślin (wynik ma wtedy > `limit` elementów).
  - `enum class Kept { ALL, TREES_ONLY, NONE };` `struct Limited { std::vector<Placed> placed; Kept kept = Kept::ALL; };`
  - `Limited PlaceMapLimited(const HeightField& height, const std::vector<glm::vec4>& surface, const BiomeMap& ids, const Input& input);` — `limit == 0` → jedno `PlaceMap` (dzisiejsza kolejność); inaczej drzewa (maska ∩ TREES), potem reszta maski z limitem `limit − drzewa` (Global Constraints).
  - `ToAppInstances(placed, texelZeroWorld, metresPerUnit, float metresPerTexel = 1.0f)` — XZ: `texelZero + p · (unitsPerMetre / metresPerTexel)`, `scale = p.scale · unitsPerMetre / metresPerTexel`.
  - `Result` + `Kept kept = Kept::ALL;`; `Write` woła `PlaceMapLimited`.

Algorytm `PlaceMap` (to, czego sygnatury nie wyznaczają):
```
p = metresPerTexel; o = mapTexel (metry); chunk = 128 * max(1, (int)floor(p))
extent = ((width-1)*p, (height-1)*p)
chunki C (metry, wielokrotności chunk) od floorDiv(o, chunk) do floorDiv(o + floor(extent), chunk)
okno: px0 = floor((C - o) / p) - 1, px1 = floor((C + chunk - o) / p) + 2 (na oś); n = max(px1 - px0 + 1) (kwadrat)
maps.firstTexel = vec2(o) + vec2(px0) * p; maps.step = p; teksele okna z indeksem przyciętym do mapy (jak dziś)
settings.chunkTexel = C; chunkTexels = chunk; mapOrigin = 0; worldUnitsPerTexel = 1
Placed: x = in.position.x - o.x, z = ...; odrzuć poza [0, (W-1)p] × [0, (H-1)p]; y = h * metresPerUnit
```
Przy p = 1 okno = dzisiejsze (`firstTexel = C − 1`, n = 131), więc wynik bez zmian. Wiersze chunków rozdzielane między `min(hardware_concurrency, wiersze)` wątków (`std::thread`), wynik wiersza w osobnym wektorze, sklejane w kolejności wierszy (deterministycznie); licznik `std::atomic<size_t>` do przerwania przy limicie; `PreparePlants` raz na wywołanie.

- [ ] **Step 1: Testy:**
  - istniejące testy `PlaceMap` bez zmian (pilnują p = 1).
  - `"PlaceMap without a surface map takes the slope from the heights"`: mapa 300×170 z nachyleniem 0.5 m/m wzdłuż X (h = x · 0.5 / metresPerUnit), `metresPerUnit` 256, `surface` pusty → brak drzew (maxSlope 30°) i krzewów (40°), są kamienie (70°); ta sama mapa z `metresPerTexel` 2 i h = x · 1.0 / 256 (też 0.5 m/m) → to samo.
  - `"PlaceMap with 30 m pixels"`: mapa 120×90 płaska, `metresPerTexel` 30 → rośliny w [0, 119·30] × [0, 89·30]; drzew na km² w ±25% od mapy 1 m 512×512 z tymi samymi roślinami; `ToAppInstances(..., 256, 30)`: x = texelZero.x + p.x · 4/30, scale = p.scale · 4/30.
  - `"PlaceMap with half-metre pixels"`: mapa 301×171, `metresPerTexel` 0.5 → wszystkie w [0, 150] × [0, 85]; deterministyczne (dwa wywołania równe).
  - `"PlaceMap on threads is deterministic and equals the chunk order"`: mapa 700×500 (wiele wierszy) → dwa wywołania element po elemencie równe.
  - `"PlaceMapLimited: under the limit keeps all"`: limit 10⁶ na 300×170 → `ALL`, rozmiar == `PlaceMap` bez limitu.
  - `"PlaceMapLimited: too many plants give trees only"`: limit = liczba drzew + 1 → `TREES_ONLY`, same `Layer::TREES`, liczba == drzewa bez limitu.
  - `"PlaceMapLimited: trees over the limit give nothing, quickly"`: płaska mapa 4096×4096 (123 km przy 30 m), gęstość drzew 1, limit 1000 → `NONE`, pusto, czas `PlaceMapLimited` < 1 s (`std::chrono`; bez przerwania przy limicie trwałoby to dziesiątki sekund).
- [ ] **Step 2:** build.bat → FAIL. **Step 3:** implementacja + `ExportPanel`: do linii „Vegetation: …” dopisz `" (over the limit: trees only)"` / `" (over the limit: none)"` wg `Result::kept`. **Step 4:** build.bat → PASS.

### Task 3: `VegetationLayer` w dowolnej skali i wspólne ustawienia widoku

**Files:**
- Modify: `ProceduralTerrainGenerator/src/opengl/VegetationLayer.h`, `VegetationLayer.cpp`

**Interfaces:**
- Produces:
  - `void SetUnitsPerMetre(float units)` / `float GetUnitsPerMetre() const` — pole `unitsPerMetre = vegetation::unitsPerMetre`; `Cull` używa go zamiast stałej (zasięgi, LOD, `plantReach`, `plantHeight`). `Update` (chunki) zostaje przy stałej.
  - `void ImGuiViewSettings()` — „Detail distance”, „Impostors”, drzewo „Distances (m)”, „Drawn …”, „Triangles …” (wyjęte z `ImGuiDraw`, który je woła; TerrainGen wygląda tak samo).
- [ ] **Step 1:** implementacja (bez testu jednostkowego — GL; przy domyślnym `unitsPerMetre` liczby identyczne jak dziś).
- [ ] **Step 2:** build.bat + ctest PASS; `--gpu-check` „All checks passed”; `--vegetation-bench` uruchamia się (TerrainGen bez zmian).

### Task 4: Ustawienia roślin w projekcie `.ptgmap` v6 (`MapProject.*`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/MapProject.h`, `MapProject.cpp`
- Test: `ProceduralTerrainGenerator/tests/MapProjectTests.cpp`

**Interfaces:**
- Produces: `struct VegetationSettings { bool enabled = true; float density = 1.0f; };` w `mapproject`, pole `Project::vegetation`; nagłówek `"vegetation": {"enabled": bool, "density": float}`; `currentVersion = 6` (komentarz: „6 (2026-11-23): vegetation”). Odczyt wersji < 6 → domyślne; gęstość nieskończona, < 0 albo > 3 → błąd „The vegetation settings of the project are damaged”.
- [ ] **Step 1: Testy:** `"Project keeps its vegetation settings"` (enabled false, density 2.5 → odczyt równy); `"Version 5 project has default vegetation"` (wersja podmieniona jak w `"Version 4 project has no georeference"` → enabled true, density 1); `"Damaged vegetation settings fail"` (density podmieniona na tekst tej samej długości `"9.5"` przy zapisie 2.5 → `Deserialize` false, `IsMarker`).
- [ ] **Step 2:** FAIL. **Step 3:** implementacja. **Step 4:** PASS.

### Task 5: Rośliny w Imported map (`MapImportVegetation.cpp`)

**Files:**
- Create: `ProceduralTerrainGenerator/src/subSystems/MapImportVegetation.cpp`
- Modify: `MapImportSys.h`, `MapImportSys.cpp` (`Update`, `Draw`, `InstallMap`, `StartSaving`, `InstallProject`, `ReloadShaders`), `MapImportBiomes.cpp` (`RunBiomePass`, `BiomesImGui`, `SetVegetationModels`), `MapImportEdit.cpp` (`UpdateRivers`: nowa sieć → nieaktualne rośliny), `TerrainGenApp.cpp` (bez zmian wywołań; sygnatura z wskaźnikiem niestałym), `CMakeLists.txt` (nowy plik w exe obok `MapImportBiomes.cpp`)

**Interfaces:**
- Consumes: Task 2 (`PlaceMapLimited`, `ToAppInstances`, `Input`, `Kept`, `maxMapInstances`), Task 3 (`SetUnitsPerMetre`, `ImGuiViewSettings`, `SetExtraInstances`, `ClearExtraInstances`, `Draw`), Task 4 (`mapproject::VegetationSettings`).
- Produces (prywatne w `MapImportSys`):
  - `void SetVegetationModels(VegetationModels* models)` (było `const`), pole `VegetationModels* vegetationModels = nullptr;`, `VegetationLayer vegetation;`
  - `mapproject::VegetationSettings plantSettings;`
  - `struct PlantResult { std::vector<vegetation::Instance> instances; vegexport::Kept kept; size_t count = 0; uint64_t version = 0; double ms = 0.0; std::string error; };` `GenerationJob<PlantResult> plantJob;`
  - `bool plantsStale = false; uint64_t plantVersion = 0;` `void PlantsChanged() { plantsStale = true; plantVersion++; }`
  - `std::vector<std::vector<biome::Plant>> placedPlants;` (rośliny tabeli przy ostatnim starcie; różnica co klatkę → `PlantsChanged()`), `int placedReadyModels = -1;`
  - `vegexport::Input PlantInput() const;` — wspólne dla ekranu i eksportu (niżej)
  - `void UpdatePlants();` (w `Update` po `UpdateBiomes`), `void PlantsImGui();` (sekcja w `BiomesImGui` po `LandCoverImGui`), `bool PlantsOn() const { return BiomesOn() && plantSettings.enabled; }`
  - `vegexport::Kept plantKept = Kept::ALL; size_t plantCount = 0; double plantMs = 0.0; std::string plantStatus;`

`PlantInput()` (wartości, których nie da się zgadnąć):
- `plants` = rośliny `biomeSettings.config.table`; `models` jak `TerrainGenerationSys::VegetationExportInput` (gotowe → liczba wariantów, inaczej 0);
- `metresPerUnit = metresPerUnit`, `metresPerTexel = pixelMetres.x`, `mapTexel = glm::ivec2(glm::round(biomeSettings.climateOrigin))`, `limit = maxMapInstances`;
- `settings.waterHeight` = `seaLevelMetres ? *sea / metresPerUnit : -FLT_MAX`; `settings.elevationScale = metresPerUnit / heightUnitMetres`, `elevationOffset = −baseMetres / heightUnitMetres`, `elevationWater = mapbiomes::WaterHeight(biomeSettings, seaLevelMetres)`;
- `settings.density = plantSettings.density`, `settings.seed = static_cast<uint32_t>(biomeSettings.config.detailSeed)`;
- rzeki, gdy `RiversOn() && riverNetwork && riverNetwork->error.empty()`: `rivers = make_shared<const hydrology::Network>(riverNetwork->network)`, `riverOrigin = vec2(mapTexel)`, `riverPositionScale = 1 / pixel`, `riverHeightScale = hydrology::mapMetresPerHeight / metresPerUnit`.

`UpdatePlants()`:
- wynik zadania: przyjęty, gdy `version == plantVersion && PlantsOn()` → `vegetation.SetExtraInstances`, statystyki; inaczej odrzucony.
- `!PlantsOn()` albo brak gotowych modeli → `ClearExtraInstances()`, status, koniec.
- wyzwalacze `PlantsChanged()`: `RunBiomePass` (każdy udany), nowa sieć rzek, zmiana `placedPlants` / liczby gotowych modeli, Density / „Plants” w UI, `InstallProject`.
- start, gdy `plantsStale && !plantJob.IsRunning() && !Busy() && !climateStale && !biomesStale && !climateJob.IsRunning() && !(LandCoverOn() && (landCoverStale || landCoverJob.IsRunning())) && !(RiversOn() && (mapChanged || riverJob.IsRunning()))` i `biomeIds` ma rozmiar mapy: odczyt id (`biomeIds->Read`, jak eksport), kopia `map`, `PlantInput()`, `texelZero = (−(W, H)·0.5 + 0.5) · worldUnitsPerTexel` (jak woda w `Draw`); w zadaniu `PlaceMapLimited(map, {}, ids, input)` → `ToAppInstances(..., metresPerUnit, pixel)`; wyjątki (`bad_alloc`) → `error`.
- `InstallMap` → `ClearExtraInstances()`, `PlantsChanged()`; `vegetation.SetUnitsPerMetre(vegetation::unitsPerMetre / pixelMetres.x)` w `ShowUnits`.

`Draw`: po `PickUnderCursor`, przed wodą: `vegetation.Draw(camera, light, renderSettings, vegetationModels)` (pędzel i orbita czytają głębię samego terenu).

`PlantsImGui()` (teksty UI po angielsku): checkbox „Plants” (tooltip: trees, shrubs and rocks of the biomes, true size of the map pixel), „Density” 0–3, `vegetation.ImGuiViewSettings()`, status: „Placing plants...”, „Plants: %zu (%.1f s)”, przy `TREES_ONLY` „Over 2 million plants: trees only”, przy `NONE` „Over 2 million trees: no plants. Crop the map or lower the density.”, brak modeli: „No vegetation models loaded (python tools/fetch_models.py).” Zmiana checkboxa / gęstości → `dirty = true` + `PlantsChanged()`.

Projekt: `StartSaving` → `project.vegetation = plantSettings`; `InstallProject` → `plantSettings = project.vegetation`; mapa z pliku / przyjęta → ustawienia zostają.

- [ ] **Step 1:** implementacja (GL i UI — bez testów jednostkowych; logika jest w Task 1–2).
- [ ] **Step 2:** build.bat + ctest PASS, `--gpu-check` „All checks passed”.
- [ ] **Step 3:** lista do sprawdzenia w aplikacji (do ledgera i ROADMAP): mapa wysłana z TerrainGen (1 m) — drzewa jak w TerrainGen w tym miejscu; DEM 30 m wycinek 512² — drzewa małe, w lasach biomów; pełny kafel — komunikat limitu; Sculpt → rośliny wracają po puszczeniu; Paint biomu lasu → drzewa po puszczeniu; Undo; morze w górę → rośliny znikają pod wodą; Save → Open zachowuje „Plants” i Density.

### Task 6: Eksport roślinności i dokumentacja

**Files:**
- Modify: `ProceduralTerrainGenerator/src/subSystems/MapImportSys.cpp` (`ImGuiSaving`)
- Modify: `docs/ROADMAP.md` (Faza 6, D4: 🟡 + co zrobione + „Do sprawdzenia w aplikacji” z Task 5 Step 3), `CLAUDE.md` (Struktura: `MapImportVegetation.cpp`; Pułapki: siatka komórek w metrach i `step`, Imported map: rośliny / limit / `SetUnitsPerMetre`, `.ptgmap` v6; eksport: „roślinność tylko 1 m” → w metrach piksela)

**Interfaces:**
- Consumes: `PlantInput()` (Task 5), `ExportPanel::VegetationSource`.
- [ ] **Step 1:** `ImGuiSaving`: gdy `PlantsOn() && biomesCurrent && sameSize(biomeIds) && plantKept != Kept::NONE` i są gotowe modele → `pack.vegetation = { [this] { return PlantInput(); }, [] { return std::vector<glm::vec4>(); }, <odczyt biomeIds jak BiomeTableSource> }`; inaczej `vegetationMissing` z powodem: „Vegetation: turn on Plants in the Biomes tab.” / „Vegetation: wait until the biomes are computed.” / „Vegetation: over 2 million trees on this map.” / „Vegetation: no models loaded.”
- [ ] **Step 2:** build.bat + ctest PASS. Ręcznie (ledger): eksport paczki z DEM 30 m — `_vegetation.json` ma tyle roślin co status Biomes, pozycje w metrach do (W−1)·piksel.
- [ ] **Step 3:** ROADMAP i CLAUDE.md (Edit, nie skrypty).
