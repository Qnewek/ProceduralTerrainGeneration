# Faza 6 etap A — import map rzeczywistych (DEM) — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** tryb Imported map wczytuje mapy wysokości ze świata rzeczywistego (GeoTIFF, SRTM `.hgt`, ESRI `.asc`) w metrach i pokazuje je w prawdziwych proporcjach.

**Architecture:** czytniki w `ptg_core` (`HeightmapImport.*` + nowe `DemTools.*` z czystymi funkcjami: wypełnianie NoData, wycinek, zmniejszanie,
rozmiar piksela w metrach). Import zwraca wysokości w metrach dla DEM (dla PNG/R16 — w zakresie wartości jak dziś). `MapImportSys` trzyma mapę
w jednostkach mapy (`metry / metresPerUnit`) i ustawia `heightScale` na prawdziwe proporcje.

**Tech Stack:** C++20, libtiff (vcpkg `tiff`, bez domyślnych funkcji, `zip`), doctest, Dear ImGui.

**Spec:** projekt zaakceptowany w czacie 2026-11-20 (bez pliku — reguła właściciela), zapisany w `docs/ROADMAP.md` → Faza 6, punkt A.

## Global Constraints

- Formaty: GeoTIFF (`.tif`, `.tiff`: int16/uint16/int32/float32/float64, 1 próbka na piksel, paski lub kafle, LZW/Deflate/bez kompresji), `.hgt` (big-endian int16,
  1201² = 3″ albo 3601² = 1″, NoData −32768, nazwa `N50E019` = narożnik SW), `.asc` (nagłówek `ncols nrows xllcorner|xllcenter yllcorner|yllcenter cellsize
  [NODATA_value]`, wiersze od północy). Dotychczasowe PNG/R16/RAW bez zmian.
- Wiersz 0 mapy = pierwszy wiersz pliku (północ) we wszystkich formatach.
- Mapa wynikowa ≤ 8192 × 8192; plik źródłowy do 32768 na bok i ≤ 2^28 próbek (większy → błąd); większe niż 8192 wymagają wycinka i/lub zmniejszenia.
- Stopnie → metry: `x = deg · 111320 · cos(lat)`, `y = deg · 110574`, lat = szerokość środka mapy. Piksel niekwadratowy → ostrzeżenie, do widoku średnia x i y.
- NoData: wypełniane średnią znanych sąsiadów (4-sąsiedztwo, fala od brzegów dziur); mapa bez żadnej znanej próbki → błąd importu.
- Prawdziwe proporcje: `heightScale = metresPerUnit · TerrainView::worldUnitsPerTexel / pixelMetres` (przewyższenie 1×).
- Bez przeliczania układów współrzędnych; bez zapisu projektu/eksportu (etap B1). Bez commitów (właściciel commituje sam).

## Review Focus

- Plik DEM z NoData w narożniku i na krawędzi → wypełnione, bez NaN (test w Task 1).
- GeoTIFF kafelkowany z kafelkami wystającymi poza obraz → poprawne wartości przy krawędzi (test w Task 2).
- Plik 10000 × 300 bez wycinka → czytelny błąd z rozmiarem, nie awaria ani 8 GB alokacji (test w Task 2: `Import` z mapą wynikową > 8192 → `false` i komunikat).
- Wycinek wychodzący poza plik → błąd, nie odczyt poza buforem (test w Task 2).
- `.asc` z `xllcenter` i bez `NODATA_value` → poprawny import (test w Task 3).

---

### Task 1: Narzędzia DEM (`DemTools.*`)

**Files:**
- Create: `src/terrainGeneration/DemTools.h`, `src/terrainGeneration/DemTools.cpp`; Test: `tests/DemToolsTests.cpp` (+ `CMakeLists.txt`: `ptg_core`, `ptg_tests`)

**Interfaces:**
- Produces (namespace `dem`):
  - `int FillNoData(HeightField& map, const std::vector<uint8_t>& missing);` — `missing[i] != 0` = brak; zwraca liczbę wypełnionych, `-1` gdy brak wszystkich (mapa bez zmian).
  - `HeightField Crop(const HeightField& map, glm::ivec2 first, glm::ivec2 size);` — wywołujący gwarantuje mieszczenie się w mapie.
  - `HeightField Downsample(const HeightField& map, int factor);` — średnia bloków `factor × factor`, rozmiar `ceil(w / factor) × ceil(h / factor)` (niepełne bloki = średnia tego, co jest); `factor 1` = kopia.
  - `int FitDownsample(int width, int height);` — najmniejsza potęga 2, po której oba boki (ceil) ≤ 8192.
  - `glm::dvec2 PixelSizeMetres(glm::dvec2 pixelSize, bool geographic, double latitude);`
  - `constexpr int maxMapSize = 8192;`

- [ ] **Step 1: Testy** (`DemToolsTests.cpp`):
  - „FillNoData averages the known neighbours”: 3×3 `{0,1,0, 2,X,3, 0,4,0}` (środek brak) → środek `2.5`, wynik `1`.
  - „FillNoData fills a hole at the corner and the edge”: 4×3, brak w (0,0), (1,0), (3,2); reszta = 7 → wszystkie `7`, wynik `3`, żadne NaN.
  - „FillNoData of a map with nothing known fails”: 2×2 wszystko brak → `-1`, mapa bez zmian.
  - „Crop takes the given rectangle”: 4×3 `v = y*10 + x`, `Crop({1,1},{2,2})` → `{11,12,21,22}`.
  - „Downsample averages blocks, partial ones too”: 5×3 `v = x`, factor 2 → 3×2; wiersz 0 = `{0.5, 2.5, 4}`.
  - „FitDownsample keeps maps within 8192”: `(8192,100)→1`, `(8193,100)→2`, `(32768,32768)→4`, `(20000,5)→4`.
  - „Pixel size of geographic maps in metres”: `PixelSizeMetres({1.0/1200, 1.0/1200}, true, 50.0)` → `x ≈ 59.63` (±0.01), `y ≈ 92.145` (±0.01); projected `({30,30}, false, 0)` → `{30,30}`.
- [ ] **Step 2: Build `ptg_tests`** — oczekiwane: błąd kompilacji (brak `DemTools.h`).
- [ ] **Step 3: Implementacja.**
- [ ] **Step 4: `ptg_tests.exe`** — oczekiwane: wszystkie przechodzą.

### Task 2: GeoTIFF przez libtiff

**Files:**
- Modify: `vcpkg.json` (`{ "name": "tiff", "default-features": false, "features": [ "zip" ] }`), `CMakeLists.txt` (`find_package(TIFF REQUIRED)`; `TIFF::TIFF` PRIVATE w `ptg_core` i `ptg_tests`),
  `src/terrainGeneration/HeightmapImport.h/.cpp`; Test: `tests/HeightmapImportTests.cpp`

**Interfaces:**
- Consumes: Task 1 (`FillNoData`, `Crop`, `Downsample`, `maxMapSize`).
- Produces (namespace `heightmap`):
  - `ImportInfo` += `bool floatingPoint = false; bool metres = false; std::optional<glm::dvec2> pixelSize; bool geographic = false; double latitude = 0.0; int noDataFilled = 0;`
    (`metres` = wartości to metry, zakres ignorowany; `pixelSize` w jednostkach pliku: metry albo stopnie przy `geographic`).
  - `struct PixelRect { int x = 0, y = 0, width = 0, height = 0; };`
  - `struct ImportOptions { ValueRange range = terrainRange; std::optional<PixelRect> crop; int downsample = 1; };`
  - `struct FileHeader { int width = 0, height = 0; ImportInfo info; };`
  - `bool Inspect(const std::filesystem::path& path, FileHeader& out, std::string& error);` — tylko nagłówek (rozmiar, bity, piksel, geografia, `metres`).
  - `bool Import(const std::filesystem::path& path, const ImportOptions& options, HeightField& out, ImportInfo& info, std::string& error);`
    — kolejność: odczyt → NoData → wycinek → zmniejszenie → kontrola ≤ 8192. Stare `Import(path, range, …)` = wrapper z `ImportOptions{ range }`.
  - `ListImportable` i komunikat „Unsupported file type” obejmują `.tif .tiff .hgt .asc`.
- [ ] **Step 1: Testy** — pomocnik testowy `WriteTiff(path, width, height, sampleFormat, bits, compression, tileSize /*0 = paski*/, values, geo)` zapisujący przez libtiff
  (`geo`: ModelPixelScale 33550, ModelTiepoint 33922, GeoKeyDirectory 34735 z GTModelTypeGeoKey 1024 = 1 projected / 2 geographic, GDAL_NODATA 42113 jako tekst):
  - „GeoTIFF int16 with Deflate in metres”: 7×5 `v = y*100 + x - 50`, piksel 30 m projected → `info.metres`, `pixelSize == {30,30}`, `!geographic`, wartości równe.
  - „GeoTIFF float32 tiled, tiles past the edge”: 20×17, kafle 16×16, LZW, `v = x * 0.5f + y` → wszystkie wartości równe (krawędź prawa i dolna).
  - „GeoTIFF NoData is filled”: 4×4 float32, NoData `-9999` w (0,0) i (3,1), reszta 5 → `noDataFilled == 2`, wszystko `5`.
  - „GeoTIFF in degrees”: piksel `1/3600`, tiepoint lat 50 na górze, 10 wierszy → `geographic`, `latitude ≈ 50 − 5/3600` (±1e-6).
  - „Inspect reads the header only”: rozmiar, bity 16, `metres`, piksel jak wyżej.
  - „GeoTIFF with three samples per pixel is refused” → `false`, komunikat zawiera „sample”.
  - „Import crops then downsamples”: float32 GeoTIFF 8×6 `v = x`, `crop {2,1,4,4}`, `downsample 2` → 2×2, wiersz 0 = `{2.5, 4.5}`.
  - „A crop outside the file fails”: `crop {6,0,4,4}` na 8×6 → `false`, komunikat zawiera „crop”.
  - „A map larger than 8192 needs a crop or downsample”: GeoTIFF 8200×2 → `false`, komunikat zawiera „8192”; z `downsample 2` → 4100×1 sukces.
- [ ] **Step 2: Build** — oczekiwane: błąd kompilacji (brak `ImportOptions`/`Inspect`).
- [ ] **Step 3: Implementacja.** Znaczniki GeoTIFF rejestrowane `TIFFMergeFieldInfo` (zmienna liczba, `passcount = 1`) przed odczytem; brak znaczników = `pixelSize` puste,
  `metres = true` (próbki DEM to metry). Paski: `TIFFReadScanline`; kafle: `TIFFReadTile`. Ostrzeżenia libtiff wyciszone (`TIFFSetWarningHandler(nullptr)`), błędy do `error`.
  Geografia: `latitude` = tiepoint Y − (wysokość/2) · piksel Y.
- [ ] **Step 4: `ptg_tests.exe`** — oczekiwane: wszystkie przechodzą (w tym dotychczasowe testy importu).

### Task 3: `.hgt` i `.asc`

**Files:**
- Modify: `src/terrainGeneration/HeightmapImport.cpp`; Test: `tests/HeightmapImportTests.cpp`

**Interfaces:**
- Consumes: Task 2 (`ImportOptions`, `Inspect`, `ImportInfo`), Task 1.
- [ ] **Step 1: Testy:**
  - „HGT 3 arc-seconds”: plik `N50E019.hgt` 1201², big-endian, `v = (y*1201 + x) % 3000`, jedna próbka −32768 → `metres`, `geographic`, `pixelSize == {1/1200, 1/1200}`, `latitude == 50.5`,
    `noDataFilled == 1`, kilka wartości sprawdzonych; `S10W020.hgt` → `latitude == -9.5`; plik o rozmiarze innym niż 1201²/3601² → błąd.
  - „ASC with xllcenter and no NODATA”: 3×2 `1 2 3 / 4 5 6`, cellsize 25 → wiersz 0 = `{1,2,3}`, `pixelSize {25,25}`, `!geographic`.
  - „ASC with NODATA_value”: `-9999` w środku 3×3 z sąsiadami 2 → wypełnione 2.
  - „ASC in degrees is recognised”: `xllcorner 19`, `yllcorner 50`, `cellsize 0.000833333`, 4×4 → `geographic`, `latitude ≈ 50 + 2·0.000833333`.
- [ ] **Step 2: Build i uruchom** — oczekiwane: testy HGT/ASC padają („Unsupported file type”).
- [ ] **Step 3: Implementacja** czytników `.hgt` (szerokość geograficzna środka z nazwy: narożnik + 0.5) i `.asc` (geografia, gdy `cellsize < 0.01` i `|xll| ≤ 180`, `|yll| ≤ 90`;
  `latitude` = `yll + nrows · cellsize / 2`, dla `yllcenter` odjąć pół komórki).
- [ ] **Step 4: `ptg_tests.exe`** — oczekiwane: wszystkie przechodzą.

### Task 4: Imported map w metrach (UI, proporcje, informacje) + dokumentacja

**Files:**
- Modify: `src/subSystems/MapImportSys.h/.cpp`, `CLAUDE.md` (DemTools, formaty, jednostki), `docs/ROADMAP.md` (Faza 6 A: 🟡 + do sprawdzenia)

**Interfaces:**
- Consumes: Task 1 (`PixelSizeMetres`, `FitDownsample`, `maxMapSize`), Task 2/3 (`Inspect`, `Import` z `ImportOptions`, `ImportInfo`, `PixelRect`).
- [ ] **Step 1: Model.** `MapImportSys` += `float metresPerUnit`, `glm::dvec2 pixelMetres`, `float trueHeightScale`. Po imporcie:
  - DEM (`info.metres`) albo zakres „Custom (metres)”: `metresPerUnit = max(|min|, |max|, 1 m)`, mapa = metry / `metresPerUnit`;
  - zakres TerrainGen `[0, 1]` → `metresPerUnit = 256`, Noise `[-1, 1]` → `64`, piksel 1 m (tak mapy z aplikacji wyglądają jak w swoich trybach);
  - `trueHeightScale` wg Global Constraints, `renderSettings.heightScale = trueHeightScale`.
- [ ] **Step 2: Zakładka Load.** Filtr `Browse…` i lista: `*.png;*.r16;*.raw;*.tif;*.tiff;*.hgt;*.asc`. Po wyborze pliku `Inspect` (w wątku głównym, sam nagłówek): rozmiar, bity / float,
  piksel (w metrach; przy stopniach oba boki i ostrzeżenie „pixels are not square: X × Y m — the map is shown with their mean” gdy różnią się > 1%), „Geographic coordinates: no reprojection”.
  Zakres wartości tylko dla PNG/R16, z trzecią opcją „Custom (metres)” i polami `Lowest` / `Highest` [m]. Pole „Pixel size (m)” (z pliku lub 1 m, edytowalne, > 0).
  „Area”: `x, y, width, height` (domyślnie cały plik) i „Downsample” (1, 2, 4, 8; domyślnie `FitDownsample`); podgląd rozmiaru wyniku, „Load” wyłączony, gdy wynik > 8192 lub wycinek poza plikiem.
- [ ] **Step 3: Display i Output.** Display: obok suwaka „Height scale” tekst „Vertical exaggeration: ×k” (`heightScale / trueHeightScale`) i przycisk „True proportions”.
  Output: plik, rozmiar, bity / float, piksel [m], wysokości min – max [m], „NoData filled: N”, czas.
- [ ] **Step 4: Build, `ptg_tests.exe`, `--gpu-check`** — oczekiwane: exit 0 bez ostrzeżeń, wszystkie testy, „All checks passed”. Ruling: UI bez testu jednostkowego.
- [ ] **Step 5: Dokumentacja** — ROADMAP Faza 6 A: 🟡 + do sprawdzenia w aplikacji (prawdziwy kafel Copernicus/SRTM, czas wczytania 3601², wygląd proporcji);
  CLAUDE.md: `DemTools.*` w strukturze, jednostki Imported map (`metresPerUnit`, `trueHeightScale`), libtiff w zależnościach.
- [ ] **Step 6: Przekazanie do przeglądu** (bez commita).
