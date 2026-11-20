# Region rzek za kamerą — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** w świecie nieskończonym TerrainGen sieć rzek REGION przeskakuje za kamerą na stałej siatce, więc rzeki są wszędzie, gdzie lata kamera.

**Architecture:** dwie czyste funkcje w `Hydrology.*` (przyciąganie środka do siatki, test opuszczenia regionu) z testami; `TerrainGenerationSys::UpdateRivers`
dostaje kamerę i w świecie nieskończonym liczy sieć wokół przyciągniętej pozycji kamery zamiast `MapCenter()`. Podmiana sieci, chunków i roślinności — istniejący mechanizm.

**Tech Stack:** C++20, glm, doctest, Dear ImGui; build CMake/Ninja (VsDevCmd x64).

**Spec:** `docs/superpowers/specs/2026-11-20-river-region-follows-camera-design.md`

## Global Constraints

- Tylko świat nieskończony TerrainGen, metoda REGION; mapa częściowa liczy region wokół `MapCenter()` jak dziś.
- Bez zmian GLSL, `RiverMap`, presetów (checkbox to stan widoku, nie generacji).
- Krok siatki = `regionSize / 4` zaokrąglone w dół do wielokrotności `cellSize`, nie mniej niż `cellSize`; przeskok, gdy kamera jest dalej niż krok w osi X lub Y.
- Nowa sieć nie startuje, dopóki poprzednia się liczy (jak dziś).
- Właściciel commituje sam: zadania kończą się przekazaniem do przeglądu, nie commitem.

## Review Focus

- Kamera daleko w ujemnych współrzędnych → przyciąganie zaokrągla symetrycznie (round, nie obcięcie ku zeru): test w Task 1.
- Kamera dokładnie na progu (odległość = krok) → bez przeskoku; tuż za progiem → przeskok: test w Task 1.
- `regionSize` mniejszy niż 4 × `cellSize` (min. 16 komórek, więc nie wystąpi, ale funkcja nie może dać kroku 0): krok ≥ `cellSize`, test w Task 1.
- Środek ustawiony z mapy częściowej (niecałkowity, poza siatką) po wejściu w świat nieskończony → sieć zostaje, dopóki kamera nie odejdzie o krok: test w Task 1 (bieżący środek poza siatką).
- Powrót w to samo miejsce z innego regionu → ten sam środek: test w Task 1.

---

### Task 1: Środek regionu na siatce (`Hydrology.*`)

**Files:**
- Modify: `src/terrainGeneration/Hydrology.h` (przed zamykającym `}` przestrzeni `hydrology`), `src/terrainGeneration/Hydrology.cpp`
- Test: `tests/HydrologyTests.cpp`

**Interfaces:**
- Produces:
  - `float hydrology::RegionStep(int regionSize, int cellSize)` — krok siatki środków (map texels).
  - `glm::vec2 hydrology::SnapRegionCenter(glm::vec2 position, int regionSize, int cellSize)` — `round(position / step) * step` na każdej osi.
  - `bool hydrology::LeftRegion(glm::vec2 center, glm::vec2 position, int regionSize, int cellSize)` — `|position − center| > step` w X lub Y.

- [ ] **Step 1: Testy**

```cpp
TEST_CASE("Region centre: a grid of a quarter region in whole cells")
{
	CHECK(hydrology::RegionStep(8192, 8) == 2048.0f);
	CHECK(hydrology::RegionStep(1000, 32) == 224.0f);	//250 down to a multiple of 32
	CHECK(hydrology::RegionStep(16, 8) == 8.0f);		//never below one cell
	CHECK(hydrology::SnapRegionCenter({ 5000.0f, -3100.0f }, 8192, 8) == glm::vec2(4096.0f, -4096.0f));
	CHECK(hydrology::SnapRegionCenter({ -1023.0f, 1025.0f }, 8192, 8) == glm::vec2(0.0f, 2048.0f));
}

TEST_CASE("Region centre: moves only when the camera leaves a quarter region, and comes back the same")
{
	CHECK_FALSE(hydrology::LeftRegion({ 0.0f, 0.0f }, { 2048.0f, -2048.0f }, 8192, 8));	//on the threshold
	CHECK(hydrology::LeftRegion({ 0.0f, 0.0f }, { 2049.0f, 0.0f }, 8192, 8));
	CHECK(hydrology::LeftRegion({ 0.0f, 0.0f }, { 0.0f, -2049.0f }, 8192, 8));
	//A centre set by the partial map, off the grid
	CHECK_FALSE(hydrology::LeftRegion({ 517.5f, 300.0f }, { 2500.0f, 300.0f }, 8192, 8));
	//After a jump the camera is at most half a step from the new centre: no jumping back
	const glm::vec2 camera(5000.0f, -3100.0f);
	const glm::vec2 jumped = hydrology::SnapRegionCenter(camera, 8192, 8);
	CHECK_FALSE(hydrology::LeftRegion(jumped, camera, 8192, 8));
	//The same place gives the same centre whichever region the camera came from
	CHECK(hydrology::SnapRegionCenter(camera, 8192, 8) == hydrology::SnapRegionCenter(camera + glm::vec2(0.4f, -0.4f), 8192, 8));
}
```

- [ ] **Step 2: Build `ptg_tests`, uruchom `ptg_tests.exe -tc="Region centre*"`** — oczekiwane: błąd kompilacji (brak funkcji).
- [ ] **Step 3: Zaimplementuj trzy funkcje** (deklaracje z komentarzami `//@param` w stylu pliku; `std::round`, `std::floor`).
- [ ] **Step 4: `ptg_tests.exe`** — oczekiwane: wszystkie testy przechodzą (dotąd 134 przypadki + 2 nowe).
- [ ] **Step 5: Przekazanie do przeglądu** (bez commita).

### Task 2: Region za kamerą w `TerrainGenerationSys` + UI + dokumentacja

**Files:**
- Modify: `src/subSystems/TerrainGenerationSys.h` (pola, deklaracje), `src/subSystems/TerrainGenerationSys.cpp` (`Update` linia ~63, `UpdateRivers` ~145, sekcja Region w `RiversEditor` ~975)
- Modify: `docs/ROADMAP.md` (punkt 3 listy „Mapa częściowa jako wycinek świata”), `CLAUDE.md` (akapit „Rzeki”: zdanie o regionie)

**Interfaces:**
- Consumes: `hydrology::SnapRegionCenter`, `hydrology::LeftRegion` (Task 1).
- Produces (w klasie):
  - `void UpdateRivers(const Camera& camera);` (zamiast `UpdateRivers()`; wywołanie w `Update` z `camera`)
  - `glm::vec2 MapPositionUnder(const Camera& camera) const;` — pozycja mapy (teksele świata) pod kamerą:
    `camera.xz / modelScale / worldUnitsPerTexel + (glm::vec2(world.width, world.height) - 1) * 0.5f` (odwrotność wzoru z komentarza `OffsetUnder`).
  - `bool riversFollowCamera = true;` — stan widoku.
  - `std::optional<glm::vec2> riverCenter;` — środek ostatnio wystartowanej sieci; brak = żadnej jeszcze.

- [ ] **Step 1: `UpdateRivers(camera)`**
  - Na początku sprawdzenia (po przyjęciu gotowej sieci, przed `if (!riversStale ...) return`): gdy metoda REGION, `infiniteGeneration && riversFollowCamera`,
    zadanie nie trwa, `riverCenter` istnieje i `LeftRegion(*riverCenter, MapPositionUnder(camera), regionSize, cellSize)` → `riversStale = true`.
    Nie przez `RequestRivers()` / `RequestTerrain()` (teren się nie zmienił; podmianę chunków robi przyjście sieci).
  - Środek nowej sieci: `infiniteGeneration && riversFollowCamera ? SnapRegionCenter(MapPositionUnder(camera), ...) : MapCenter()`; zapisz w `riverCenter`.
- [ ] **Step 2: UI w sekcji Region**
  - `ImGui::Checkbox("Follow camera (infinite world)", &riversFollowCamera)`, tooltip: region przeskakuje o ćwierć boku, gdy kamera odejdzie o ćwierć od środka;
    nowa sieć liczy się w tle, chunki odświeżają się od najbliższych; rzeki w części wspólnej mogą się zmienić. Zmiana checkboxa nie oznacza sieci jako nieaktualnej.
  - `ImGui::Text("Region centre: %.0f, %.0f", ...)` z `riverCenter` (gdy jest).
  - Tooltip „Region size”: „Side of the square that gets rivers: around the middle of the partial map, or following the camera in the infinite world; they fade out at its border.”
- [ ] **Step 3: Build całości** (`build.bat` w scratchpadzie) — oczekiwane: exit 0, bez ostrzeżeń (`/W4 /WX`).
- [ ] **Step 4: `ptg_tests.exe` i `ProceduralTerrainGenerator.exe --gpu-check`** (z katalogu `ProceduralTerrainGenerator/`) — oczekiwane: wszystkie testy, „All checks passed”.
- [ ] **Step 5: Dokumentacja**
  - ROADMAP punkt 3: 🟡 2026-11-20, opis jak w Global Constraints + „do zmierzenia w aplikacji: czas przeskoku = Rivers → czas sieci (`riverMs`) + Output → czas do odświeżenia
    chunków po zmianie (`ChunkWorld::updateMs`); czy różnice rzek przy przeskoku są akceptowalne”.
  - CLAUDE.md, akapit „Rzeki”: region REGION wokół środka mapy częściowej, w świecie nieskończonym przeskakuje za kamerą (`hydrology::SnapRegionCenter` / `LeftRegion`,
    krok ¼ regionu w całych komórkach), sprawdzenie w `UpdateRivers(camera)` ustawia `riversStale` bezpośrednio.
- [ ] **Step 6: Przekazanie do przeglądu** (bez commita; właściciel sprawdza w aplikacji).
