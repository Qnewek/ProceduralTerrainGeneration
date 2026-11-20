# Erozja na mapie częściowej TerrainGen — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** mapa częściowa TerrainGen może być erodowana (`GpuErosion`), a wynik jest pokazany obok mapy podstawowej z wodą i roślinami rozmieszczonymi na terenie po erozji.

**Architecture:** parametry erozji w `WorldConfig` (preset); wynik w warstwie OVERLAY `TerrainView`, rysowany z przesunięciem w X jak w Noise heightmap; woda = drugie
`WaterSurface::DrawMap` z mapą po erozji; rośliny = `vegexport::PlaceMap` na mapie po erozji w tle → dodatkowy zestaw instancji w `VegetationLayer`.

**Tech Stack:** C++20, OpenGL 4.5, glm, Dear ImGui, nlohmann/json, doctest.

**Spec:** projekt zaakceptowany w czacie 2026-11-20 (bez pliku — reguła właściciela), punkty 1–8 powtórzone w Global Constraints.

## Global Constraints

- Tylko mapa częściowa TerrainGen; w świecie nieskończonym zakładka Erosion pokazuje komunikat (erozja w świecie nieskończonym odrzucona).
- Jedna mapa po erozji naraz (wybrana metoda); porównanie obu metod — poza zakresem.
- Mapa po erozji obok podstawowej: przesunięcie X = `GetWorldWidth() * 1.05`, widok 2D kadruje obie, podpisy „Terrain” / „Eroded: <metoda>”.
- Biomy, kolory i splat mapy po erozji = te z mapy podstawowej (bez przeliczania).
- Każde przegenerowanie terenu (`pending.terrain`) kasuje erozję i rośliny mapy po erozji; kolejne „Erode map” kumulują się.
- Rośliny mapy po erozji: wszystkie warstwy do 2048 × 2048 tekseli włącznie, powyżej tylko `vegetation::Layer::TREES`.
- Eksport: dodatkowa mapa wysokości z sufiksem `_eroded_droplets` / `_eroded_pipes`, gdy mapa po erozji jest pokazana.
- Domyślne `WorldConfig::erosion` = domyślne `ErosionConfig` z `pipes.heightScale = 256` (wysokość 1 = 1024 jednostki / 4 na teksel).
- Bez zmian: trawa i cień terenu na mapie po erozji. Bez commitów (właściciel commituje sam).

## Review Focus

- Zmiana rozmiaru mapy po erozji → stara tekstura OVERLAY/rośliny innego rozmiaru nie mogą zostać pokazane (reset w Task 5, Step 1: reset przy `pending.terrain`, które zmiana rozmiaru zawsze ustawia).
- Erozja w trakcie rozmieszczania roślin poprzedniej erozji → wynik starego zadania nie może zastąpić nowszego (Task 5: numer erozji zapisany z zadaniem, wynik starszy niż bieżący odrzucany).
- Wyłączone biomy (brak `biomeMap`) → erozja działa, roślin brak, bez awarii (Task 5: rozmieszczanie tylko z biomami i gotowymi modelami).
- Preset bez pola `erosion` → wartości domyślne z `pipes.heightScale = 256` (test w Task 1).
- `layerMask` tylko drzewa → żadnej instancji innej warstwy (test w Task 2).

---

### Task 1: Parametry erozji w `WorldConfig` i presecie świata

**Files:**
- Modify: `src/terrainGeneration/WorldConfig.h` (`struct WorldConfig`), `src/terrainGeneration/Preset.cpp` (`ToJson(const WorldConfig&…)`, `ReadFields(…, WorldConfig&)`, `Check(const WorldConfig&)`)
- Test: `tests/PresetTests.cpp`

**Interfaces:**
- Produces: `erosion::ErosionConfig WorldConfig::erosion` (domyślnie `pipes.heightScale = 256.0f`); klucz JSON `"erosion"` w presecie świata, ten sam format co w presecie Noise (`ErosionToJson` / `ReadErosion` / walidacja erozji, które już istnieją).

- [ ] **Step 1: Testy** — w `DistinctWorld()` ustawić każde pole erozji na wartość różną od domyślnej (użyć tej samej funkcji/wzorca co test presetu Noise, jeśli istnieje `DistinctErosion`); w teście „A world preset keeps every field” porównać erozję (`CheckSameErosion` lub pole po polu). Nowy test:

```cpp
TEST_CASE("A world preset without erosion gets the TerrainGen defaults")
{
	WorldConfig loaded;
	std::string error;
	REQUIRE_MESSAGE(preset::FromJson(R"({"kind": "world", "version": 2, "width": 64, "height": 32})", loaded, error), error);
	CHECK(loaded.erosion.pipes.heightScale == 256.0f);
	CHECK(loaded.erosion.method == erosion::Method::DROPLETS);
}
```
- [ ] **Step 2: Build `ptg_tests`, uruchom** — oczekiwane: błąd kompilacji (brak `WorldConfig::erosion`).
- [ ] **Step 3: Pole w `WorldConfig`, zapis `root["erosion"]`, odczyt, walidacja** — jak dla `NoiseMapConfig`.
- [ ] **Step 4: `ptg_tests.exe`** — oczekiwane: wszystkie przechodzą.

### Task 2: Rośliny mapy po erozji w jednostkach aplikacji (`VegetationExport.*`)

**Files:**
- Modify: `src/terrainGeneration/VegetationExport.h`, `src/terrainGeneration/VegetationExport.cpp`
- Test: `tests/VegetationExportTests.cpp`

**Interfaces:**
- Produces:
  - `uint32_t vegexport::Input::layerMask = (1u << vegetation::layerCount) - 1u;` — `PlaceMap` używa go zamiast stałej „wszystkie warstwy”.
  - `std::vector<vegetation::Instance> vegexport::ToAppInstances(const std::vector<Placed>& placed, glm::vec2 texelZeroWorld, float metresPerUnit);`
    x, z = `texelZeroWorld + position.xz / metresPerTexel * vegetation::unitsPerMetre`; y = `position.y / metresPerUnit`; `scale = placed.scale * unitsPerMetre`; yaw, model, variant, layer bez zmian.

- [ ] **Step 1: Testy**
  - „PlaceMap with only trees in the layer mask places nothing else”: ta sama mapa i wejście co istniejący test roślinności, `input.layerMask = 1u << static_cast<int>(vegetation::Layer::TREES)`; `CHECK` że wynik nie jest pusty i każda `layer == TREES`, oraz że bez maski są też inne warstwy (jeśli dane testu je dają).
  - „App instances stand where the pack places them”: jeden `Placed{ {10.0f, 64.0f, 20.0f}, 0.5f, 2.0f, 3, 1, Layer::SHRUBS }`, `ToAppInstances(p, {-100.0f, 40.0f}, 256.0f)` →
    `position == {-60.0f, 0.25f, 120.0f}`, `scale == 8.0f`, `yaw == 0.5f`, `model == 3`, `variant == 1`, `layer == SHRUBS`.
- [ ] **Step 2: Build, uruchom** — oczekiwane: błąd kompilacji (brak `layerMask` / `ToAppInstances`).
- [ ] **Step 3: Implementacja.**
- [ ] **Step 4: `ptg_tests.exe`** — oczekiwane: wszystkie przechodzą.

### Task 3: Wspólne UI erozji i podpisy map (refaktor Noise heightmap)

**Files:**
- Create: `src/utility/MapLabels.h`, `src/utility/MapLabels.cpp` (+ `CMakeLists.txt`, exe)
- Modify: `src/utility/utilities.h/.cpp`, `src/subSystems/NoiseBasedGenerationSys.h/.cpp`

**Interfaces:**
- Produces:
  - `void ErosionParametersImGui(erosion::ErosionConfig& e);` (utilities) — radio metody + parametry kropli albo rur, dokładnie obecna treść z `NoiseBasedGenerationSys::ErosionImGui` od radia metody do końca gałęzi `else` (bez „Compare”, bez przycisków).
  - `class MapLabels { public: void Place(const glm::mat4& viewProjection, const std::vector<std::pair<float, std::string>>& maps, glm::vec2 center, float halfDepth, float top); void ImGuiDraw() const; void Clear(); };`
    — przeniesione `PlaceLabels` / `ImGuiMapLabels` / `labels` / `labelClip*` z Noise; `maps` = (przesunięcie X od środka mapy, nazwa).
- [ ] **Step 1: Przenieść, Noise używa obu.** Zachowanie Noise bez zmian (to samo UI i podpisy).
- [ ] **Step 2: Build całości** — oczekiwane: exit 0, bez ostrzeżeń. Ruling w ledgerze: refaktor UI bez testu jednostkowego (ImGui/GL), weryfikacja = build + właściciel w aplikacji.

### Task 4: Dodatkowy zestaw roślin w `VegetationLayer`

**Files:**
- Modify: `src/opengl/VegetationLayer.h/.cpp`

**Interfaces:**
- Consumes: `vegetation::Instance` (bez zmian).
- Produces:
  - `void SetExtraInstances(const std::vector<vegetation::Instance>& instances);` — zastępuje poprzedni zestaw: zwalnia jego zakres, alokuje nowy w `placedBuffer` (`Allocate`), dzieli instancje na kwadraty `ChunkWorld::chunkSize * worldUnitsPerTexel` jednostek (posortowane, jeden `DrawRange` z `areaMin/Max` na niepusty kwadrat) i trzyma te zakresy osobno (`extraRanges`).
  - `void ClearExtraInstances();`
  - `bool HasExtraInstances() const;`
- [ ] **Step 1: Implementacja.** `Cull` przechodzi `drawRanges` i `extraRanges`; warunki „nic do rysowania” w `Draw`/`CullShadows` sprawdzają oba; ścieżka czyszcząca `placed` (brak modeli/biomów) nie może zwolnić ani nadpisać zakresu zestawu dodatkowego; `PlacedCount` go wlicza.
- [ ] **Step 2: Build całości** — oczekiwane: exit 0. Ruling: bez testu jednostkowego (GL), weryfikacja w Task 5 + aplikacja.

### Task 5: Erozja w `TerrainGenerationSys` (zakładka, widok, woda, rośliny, eksport) + dokumentacja

**Files:**
- Modify: `src/subSystems/TerrainGenerationSys.h/.cpp`, `src/subSystems/TerrainGenApp.cpp` (podpisy map TerrainGen), `docs/ROADMAP.md` (punkt 4), `CLAUDE.md` (pułapka erozji, struktura `MapLabels`)

**Interfaces:**
- Consumes: Task 1 (`world.erosion`), Task 2 (`Input::layerMask`, `ToAppInstances`), Task 3 (`ErosionParametersImGui`, `MapLabels`), Task 4 (`SetExtraInstances`, `ClearExtraInstances`, `HasExtraInstances`).
- Produces: `void ImGuiMapLabels();` (publiczne, jak w Noise; `TerrainGenApp` woła je dla TerrainGen tam, gdzie woła dla Noise).

- [ ] **Step 1: Stan i erozja.** Pola: `GpuErosion erosion` (Initialize w `Initialize`), `bool erodedShown`, `bool erodeFromBase = true`, `uint32_t erosionRuns`, `uint64_t erosionVersion` (rośnie przy każdej erozji i resecie), `bool erodeRequested`, `double erosionMs` (`PollMeasure`).
  W `GenerateFinite`, gdy `request.terrain`: reset (`erodedShown = false`, `erodeFromBase = true`, `erosionVersion++`, `vegetationLayer.ClearExtraInstances()`).
  `UpdateErosion()` po `GenerateFinite` (tylko mapa częściowa): na żądanie `erosion.Erode(world.erosion, fromBase ? TERRAIN : OVERLAY, view.LayerTexture(OVERLAY, w, h), erosionRuns++)`, potem `erodedShown = true`, `erosionVersion++`, start rozmieszczania (Step 4).
- [ ] **Step 2: Zakładka „Erosion”** w `ImGuiRightPanel` (po Rivers): świat nieskończony → `"Erosion works on the partial map: switch to Partial generation in the Display tab."`; inaczej `ErosionParametersImGui(world.erosion)`, „Erode map”, „Reset” (reset jak w Step 1), czas ostatniej erozji, liczba roślin mapy po erozji / „placing…”.
- [ ] **Step 3: Widok.** W `Draw` (mapa częściowa), gdy `erodedShown`: `SetFlatViewBounds` kadruje obie mapy (wzór z `NoiseBasedGenerationSys::Draw`), `view.Draw(..., Layer::OVERLAY, glm::vec3(shift, 0, 0))`, drugie `water.DrawMap` z teksturą OVERLAY, `corner + (shift, 0)`, `worldTexelOrigin + (shift, 0)`; podpisy przez `MapLabels`. `vegetationLayer.Draw` także, gdy `!partialDetails`, ale `HasExtraInstances()` (wtedy bez `shadows.Render` z chunków).
- [ ] **Step 4: Rośliny.** Gdy biomy włączone i są gotowe modele: w wątku głównym `view.ReadLayer(OVERLAY)`, `view.ReadSurface(OVERLAY)` (sprawdzić, że liczy mapę pochodnych tej warstwy; jeśli nie — ruling i najmniejsza zmiana w `TerrainView`), `biomeMap`; wejście jak `vegetation.input` w `ImGuiSaving` (wydzielić wspólną funkcję `vegexport::Input VegetationExportInput() const`), `layerMask` = drzewa, gdy `w * h > 2048 * 2048`; w `GenerationJob`: `PlaceMap` → `ToAppInstances(placed, texelZeroWorld + (shift, 0), metresPerUnit)`, gdzie `texelZeroWorld = (mapOffset - (size - 1) / 2) * worldUnitsPerTexel` (jak `worldTexelOrigin` w `Draw`, przesunięte o offset mapy). Wynik przyjmowany w `Update` tylko, gdy jego `erosionVersion` = bieżący → `SetExtraInstances`.
- [ ] **Step 5: Eksport.** W `ImGuiSaving`, gdy `erodedShown`: `pack.heightmaps.push_back({ w, h, method == PIPES ? "_eroded_pipes" : "_eroded_droplets", [this] { return view.ReadLayer(TerrainView::Layer::OVERLAY); } })`.
- [ ] **Step 6: Build, `ptg_tests.exe`, `--gpu-check`** — oczekiwane: exit 0 bez ostrzeżeń, wszystkie testy, „All checks passed”.
- [ ] **Step 7: Dokumentacja** — ROADMAP punkt 4: 🟡 + stan, co nie objęte (trawa, cień terenu, biomy), do sprawdzenia w aplikacji (domyślne parametry przy skali 1024, czas rozmieszczania, wiszące brzegi wody). CLAUDE.md: OVERLAY w TerrainGen = mapa po erozji, reset przy `pending.terrain`, rośliny mapy po erozji przez `SetExtraInstances`, `MapLabels.*` w strukturze.
- [ ] **Step 8: Przekazanie do przeglądu** (bez commita).
