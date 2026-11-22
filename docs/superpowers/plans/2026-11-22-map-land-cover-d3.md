# D3: biomy z pokrycia terenu (Imported map) — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** biomy Imported map z pliku pokrycia terenu (ESA WorldCover i podobne): klasa zawęża listę biomów, klimat wybiera w jej obrębie; georeferencja mapy (stopnie + poprzeczny Merkator), tryb wyświetlania LAND COVER.

**Architecture:** georeferencja (`GeoReference.*`, ptg_core) czytana przy imporcie i trzymana w projekcie v5; mapa klas (`LandCover.*`, CPU w tle) = klasa najczęstsza w obszarze piksela mapy, `R8UI`; `map_biome_compute.shader` wybiera i miesza tylko biomy z maski klasy (SSBO). Wzorzec CPU w `Biome.*` (maska), sprawdzany `--gpu-check`.

**Tech Stack:** C++20, OpenGL 4.5 compute, ImGui, libtiff + zlib (vcpkg, już są), nlohmann/json, doctest.

**Spec:** `docs/superpowers/specs/2026-11-22-map-land-cover-d3-design.md`

## Global Constraints

- Bez nowych bibliotek (przeliczenie poprzecznego Merkatora własne). `ptg_core` bez GL/ImGui; `/W4 /WX`; nowy `.cpp` dopisany do `CMakeLists.txt`.
- Elipsoida jedna: a = 6378137, f = 1/298.257223563 (WGS84 = GRS80 dla tego celu).
- EPSG: 4326 (stopnie), 32601–32660 i 32701–32760 (WGS84 UTM N/S), 25828–25838 (ETRS89 UTM), 2180 (PUWG 1992: λ0 19°, k0 0,9993, FE 500 000, FN −5 300 000), 2176–2179 (PUWG 2000 strefy 5–8: λ0 15/18/21/24°, k0 0,999923, FE strefa·10⁶ + 500 000, FN 0); UTM: λ0 = 6·strefa − 183, k0 0,9996, FE 500 000, FN 0 (N) / 10 000 000 (S). `epsg = 0` = współrzędne bez znanego układu.
- Maska biomów 64-bitowa (`biome::maxBiomes = 64`); pusta maska = wszystkie biomy; kod klasy 0 = brak danych.
- Klasa piksela mapy: siatka k×k próbek, k = clamp(ceil(piksel mapy / piksel pokrycia), 1, 4), klasa najczęstsza (remis: mniejszy kod), kod 0 pomijany.
- Raster pokrycia w pamięci ≤ 2× rozdzielczość mapy na oś; większy prostokąt zmniejszany krokiem s z klasą najczęstszą w bloku s×s.
- Komentarze po angielsku, UI po angielsku, dokumentacja po polsku. Bez commitów (commituje właściciel).
- Build: `build.bat` w scratchpadzie (configure + build + ctest); po zmianie shaderów `ProceduralTerrainGenerator.exe --gpu-check` z katalogu repo.

## Review Focus

1. Mapa na styku dwóch kafli pokrycia: bez szwu i bez dziury na granicy (Task 4: „Two files cover the two halves of the map”).
2. Półkula południowa / długość zachodnia (327xx, λ < 0): znaki i FN 10 000 000 (Task 1: „UTM south round trip”).
3. Plik pokrycia zniknął z dysku przy otwarciu projektu: komunikat, biomy jak z klimatu, bez wyjątku (Task 7: warunki w kodzie + ręcznie).
4. Biom z listy klasy usunięty / przemianowany w tabeli: pomijany w masce; gdy z listy nie zostaje nic → wszystkie biomy (Task 5: „Class masks follow biome names”).
5. Duży kafel (36000², kafelkowany, skompresowany) pod małą mapą: czytany tylko prostokąt, pamięć ograniczona limitem (Task 4: „A large window is reduced by blocks”).

---

### Task 1: `GeoReference.*` — georeferencja i poprzeczny Merkator

**Files:**
- Create: `ProceduralTerrainGenerator/src/terrainGeneration/GeoReference.h/.cpp`, `ProceduralTerrainGenerator/tests/GeoReferenceTests.cpp`
- Modify: `CMakeLists.txt` (ptg_core + ptg_tests)

**Interfaces:**
- Produces (namespace `geo`):
  - `constexpr int degreesEpsg = 4326;`
  - `struct Projection { double lon0Degrees, k0, falseEasting, falseNorthing; };`
  - `std::optional<Projection> TransverseMercatorOf(int epsg);` (none dla 4326, 0 i nieobsługiwanych)
  - `bool Supported(int epsg);` (4326 albo `TransverseMercatorOf` daje wartość)
  - `std::string SystemName(int epsg);` („Degrees (EPSG:4326)”, „UTM 34N (EPSG:32634)”, „ETRS89 / UTM 34N (EPSG:25834)”, „PUWG 1992 (EPSG:2180)”, „PUWG 2000 zone 7 (EPSG:2178)”, „Unknown system”, „EPSG:N (not supported)”)
  - `std::vector<int> ChoosableSystems();` (4326, 2180, 2176–2179, 32601–32660, 32701–32760, 25828–25838 — do combo w UI)
  - `glm::dvec2 ToMetres(const Projection&, glm::dvec2 lonLat);` / `glm::dvec2 ToDegrees(const Projection&, glm::dvec2 eastNorth);` (x = długość / easting)
  - `struct GeoReference { int epsg = 0; glm::dvec2 origin{ 0.0 }; glm::dvec2 pixel{ 1.0, -1.0 }; };` — origin = lewy górny narożnik piksela (0, 0) w jednostkach układu
  - `glm::dvec2 PixelToSystem(const GeoReference&, glm::dvec2 pixel);` / `glm::dvec2 SystemToPixel(const GeoReference&, glm::dvec2 point);` (piksel ciągły: środek piksela i = i + 0,5)
  - `std::optional<glm::dvec2> PixelToDegrees(const GeoReference&, glm::dvec2 pixel);` / `std::optional<glm::dvec2> DegreesToPixel(const GeoReference&, glm::dvec2 lonLat);` (none dla nieobsługiwanego układu)
  - `GeoReference Cropped(const GeoReference&, int x, int y, int downsample);` (origin += (x, y)·pixel, pixel *= downsample)
  - `std::optional<GeoReference> FromGeoTiffTags(const std::vector<double>& scale, const std::vector<double>& tiepoint, const std::vector<double>& transformation, const std::vector<uint16_t>& keys);` — 33550 / 33922 / 34264 / 34735; GeoKeys 1024 (2 = stopnie → 4326), 1025 (2 = PixelIsPoint → narożnik − pół piksela), 2048 (gdy 1024 = 2, zwykle 4326), 3072 (kod układu); transformacja z obrotem → none; brak skali i transformacji → none; 1024 = 1 bez 3072 → epsg 0.

- [ ] **Step 1: Testy** (`GeoReferenceTests.cpp`):
  - „Central meridian maps to the false easting”: `ToMetres(*TransverseMercatorOf(32633), {15, 45})` → x = 500000 (±1e-6), y = 0,9996 · 4 984 944,378 = 4 982 950,400 (±0,01; łuk południka WGS84 do 45° = 4 984 944,378 m).
  - „Equator 3 degrees from the meridian”: `ToMetres(*TransverseMercatorOf(32631), {6, 0})` → x = 833 978,556 (±0,05), y = 0 (±1e-6). (Wartość referencyjna UTM; gdy test nie przechodzi, najpierw sprawdź ją w niezależnym źródle, potem kod.)
  - „UTM north round trip” (32634: (21,3; 52,1), (18,0; 49,0), (24,0; 54,8)) i „UTM south round trip” (32723: (−45,2; −23,5)) → `ToDegrees(ToMetres(p)) ≈ p` (±1e-9°); dla 327xx y > 5 000 000.
  - „Symmetric about the meridian”: dla 2180 punkty λ0 ± 2° na φ 51° → x − 500000 przeciwne, y równe (±1e-6).
  - „PUWG 1992 north of the false northing”: `ToMetres(*TransverseMercatorOf(2180), {19, 52})`: x = 500000, y = 0,9993 · łuk(52°) − 5 300 000, gdzie łuk liczony w teście niezależnym szeregiem południka (np. całka numeryczna Simpsona z M(φ) = a(1−e²)/(1−e² sin²φ)^1,5, 2000 przedziałów) — zgodność ±0,01.
  - „Supported systems”: Supported(4326, 2180, 2177, 32634, 32760, 25834) = true; Supported(0, 3857, 3035, 32661, 25827) = false; `SystemName(32634) == "UTM 34N (EPSG:32634)"`.
  - „Pixel and degrees of a georeference”: GeoReference{4326, (19, 51), (0,001, −0,001)} → PixelToDegrees((0,5; 0,5)) = (19,0005; 50,9995); DegreesToPixel tego = (0,5; 0,5); GeoReference{2180, (500000, 300000), (30, −30)} → PixelToDegrees((0,0)) round trip ±1e-6 piksela.
  - „Cropped moves the corner”: Cropped({32634, (400000, 5800000), (30, −30)}, 10, 4, 2) → origin (400300, 5799880), pixel (60, −60).
  - „GeoTIFF tags”: skala (30, 30, 0), tiepoint (0,0,0, 400000, 5800000, 0), klucze [1,1,0,3, 1024,0,1,1, 1025,0,1,1, 3072,0,1,32634] → {32634, (400000, 5800000), (30, −30)}; to samo z 1025 = 2 → origin (399985, 5800015); klucze z 1024 = 2 → epsg 4326; transformacja z obrotem (34264, element [1] ≠ 0) → none; brak skali → none.
- [ ] **Step 2:** build.bat → nowe testy FAIL (brak symboli = błąd kompilacji to RED; zapisz w ledgerze).
- [ ] **Step 3:** implementacja: szereg Krügera rzędu n⁶ (α, β, A wg Karney 2011 / „Transverse Mercator with an accuracy of a few nanometers”), kody stref z Global Constraints.
- [ ] **Step 4:** ctest PASS.

### Task 2: georeferencja przy imporcie

**Files:**
- Modify: `src/terrainGeneration/HeightmapImport.h/.cpp`, `tests/HeightmapImportTests.cpp`
- Create: `tests/TiffWriter.h` (wyjęty z `HeightmapImportTests.cpp` `WriteTiff`/`TiffSpec`/`Geo`/`SetArray`, rozszerzony: `Geo::epsg` (0 = jak dziś: tylko 1024), `Geo::pixelIsPoint`, `Geo::leftX` (X lewej krawędzi, dziś 0), `TiffSpec` z `bits = 8` + `SAMPLEFORMAT_UINT` dla plików klas)

**Interfaces:**
- Consumes: Task 1 (`GeoReference`, `FromGeoTiffTags`, `Cropped`).
- Produces: `ImportInfo::std::optional<geo::GeoReference> georeference` (po wycinku i zmniejszeniu; `Inspect` = całego pliku); `ImportOptions::std::optional<int> epsg` (nadpisuje układ współrzędnych bez układu, `epsg == 0` z pliku); `.asc`: układ z `.prj` obok (`<nazwa>.prj`): `PROJCS` z „UTM” i „zone NN” + N/S (w nazwie „UTM_Zone_34N” albo „UTM zone 34N”) → 326NN/327NN (z „ETRS” → 258NN), „CS92” lub „1992” → 2180, „CS2000” + „zone N”/„_5”…„_8” → 2176–2179, `GEOGCS` bez `PROJCS` → 4326, inaczej 0; bez `.prj`: 4326 gdy reguła stopni z importu, inaczej 0. `.hgt`: 4326, origin = (lon − 0,5·piksel, lat + 1 + 0,5·piksel) z nazwy, piksel ±1/1200 lub 1/3600.

- [ ] **Step 1: Testy** (nowe w `HeightmapImportTests.cpp`, istniejące przepięte na `TiffWriter.h`):
  - „GeoTIFF georeference in UTM”: 6×10, piksel 30, epsg 32634, leftX 400000, topY 5800000 → `info.georeference` = {32634, (400000, 5800000), (30, −30)}.
  - „Pixel is point moves the corner by half a pixel”.
  - „Crop and downsample move the georeference”: crop (2, 4, 4, 6), downsample 2 → origin (400060, 5799880), pixel (60, −60).
  - „HGT georeference from its name”: `N50E019.hgt` 1201² → {4326, (19 − 1/2400, 51 + 1/2400), (1/1200, −1/1200)}.
  - „ASC with a prj in PUWG 1992”: `.prj` z `PROJCS["ETRF2000-PL / CS92",...]`, xllcorner 500000, yllcorner 300000, cellsize 1, 4×3 → {2180, (500000, 300003), (1, −1)}.
  - „ASC without a prj in metres has no system”: epsg 0, origin z nagłówka; z `ImportOptions::epsg = 2180` → 2180.
  - „PNG has no georeference”.
- [ ] **Step 2:** FAIL. **Step 3:** implementacja (GeoTIFF: odczyt 33550/33922/34264/34735 jak dziś przez `GetArray`, potem `FromGeoTiffTags`). **Step 4:** ctest PASS.

### Task 3: georeferencja w projekcie i w `MapImportSys`

**Files:**
- Modify: `MapProject.h/.cpp` + testy, `MapImportSys.h/.cpp`, `MapImportBiomes.cpp` (sekcja georeferencji), `SendToImported.cpp` (bez georeferencji — sprawdzić tylko, że pole zostaje puste)

**Interfaces:**
- Consumes: Task 1–2.
- Produces: `mapproject::Project::std::optional<geo::GeoReference> georeference`; `currentVersion = 5`; nagłówek `"georeference": {"epsg": int, "origin": [x, y], "pixel": [x, y]}` (brak = none; wartości nieskończone → błąd „The georeference of the project is damaged”); `MapImportSys::std::optional<geo::GeoReference> georeference` (instalowane z `ImportInfo` / projektu, czyszczone przy mapie przyjętej z innego trybu); `void MapImportSys::GeoreferenceImGui()` (w sekcji „Land cover”, Task 7 ją woła), `void MapImportSys::SetGeoreference(std::optional<geo::GeoReference>)` (dirty + `landCoverStale = true` z Task 7 — do czasu Task 7 samo dirty).

- [ ] **Step 1: Testy** `MapProjectTests.cpp`: „Project keeps its georeference” (v5, wartości bit w bit), „Version 4 project has no georeference”, „Damaged georeference fails” (origin `[1e400, 0]` w JSON → błąd, marker nietknięty — przez zamianę tekstu w nagłówku jak w testach D2).
- [ ] **Step 2:** FAIL. **Step 3:** implementacja + `MapImportSys`: `InstallResult` bierze `info.georeference` (reload też), `InstallProject` z projektu, `InstallReceived` czyści, `StartSaving` zapisuje. Zakładka Load: przy pliku ze współrzędnymi bez układu (`Inspect` → `georeference->epsg == 0`) combo „Coordinate system” (`ChoosableSystems`, `SystemName`) → `ImportOptions::epsg`; tekst „Coordinate system: …” dla pozostałych.
  `GeoreferenceImGui`: tekst układu i narożnika (albo „No georeference: land cover is stretched over the map”), combo układu (gdy mapa ma georeferencję), „Take georeference from file…” (`OpenFileDialog` na GeoTIFF/.hgt/.asc → `heightmap::Inspect` → pola „Crop x / y” (0) i „Downsample” (1) → przycisk „Use” → `Cropped`; rozmiar pliku po wycinku i zmniejszeniu (`(w − x) / d`, `(h − y) / d` całkowicie) ≠ rozmiar mapy → status błędu, nic się nie zmienia; plik bez georeferencji → błąd).
- [ ] **Step 4:** ctest PASS.

### Task 4: `LandCover.*` — mapa klas

**Files:**
- Create: `src/terrainGeneration/LandCover.h/.cpp`, `tests/LandCoverTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1, `tests/TiffWriter.h` z Task 2.
- Produces (namespace `landcover`):
  - `struct FileStatus { std::string path; std::string system; float coverage = 0; bool stretched = false; std::string skipped; std::string error; };` (coverage = część pikseli mapy, które dostały klasę z tego pliku, 0–1; `skipped` = powód pominięcia)
  - `struct ClassMap { int width = 0, height = 0; std::vector<uint8_t> classes; std::array<uint64_t, 256> counts{}; std::vector<FileStatus> files; };`
  - `ClassMap Compute(const std::vector<std::filesystem::path>& files, int width, int height, const std::optional<geo::GeoReference>& map, int maxRasterSide = 0);` (0 = 2 · max(width, height)); wyjątki tylko `std::bad_alloc` (błędy plików w `FileStatus::error`).
  - Algorytm: mapa z georeferencją w obsługiwanym układzie → każdy plik z georeferencją w obsługiwanym układzie: prostokąt pod mapą (narożniki i środki krawędzi mapy → stopnie → układ pliku → piksele, ± 2 piksele), czytany z decymacją (blok s×s, klasa najczęstsza, s = najmniejsze całkowite z bokiem ≤ `maxRasterSide`); pozostałe pliki `skipped`. Mapa bez georeferencji (albo nieobsługiwany układ / epsg 0) → pierwszy czytelny plik rozciągnięty (`stretched`), reszta `skipped: "only the first file is stretched"`. Próbki: k×k na piksel mapy, położenie w układzie mapy → stopnie przez siatkę węzłów co 16 pikseli (dokładne przeliczenie w węzłach, dwuliniowa interpolacja stopni między nimi) → pierwszy plik, który ma próbkę ≠ 0. Równolegle po wierszach (`std::execution::par`, jak `MapBiomes.cpp`).
  - Plik pokrycia: GeoTIFF 1 próbka uint8, kafle albo paski (`TIFFReadTile` / `TIFFReadEncodedStrip`), tylko kafle/paski przecinające prostokąt.

- [ ] **Step 1: Testy** (pliki uint8 przez `TiffWriter.h` w katalogu tymczasowym):
  - „Land cover in degrees under a map in degrees”: plik 40×40 piksel 0,001° od (19, 51) w dół, lewa połowa 10, prawa 30; mapa 10×10 {4326, (19,010; 50,990), (0,002; −0,002)} → kolumny 0–4 = 10, 5–9 = 30; coverage 1.
  - „Most frequent class wins in a map pixel”: plik 0,001°, mapa 0,004° (k = 4): w każdym bloku 4×4 dziewięć próbek klasy 40, siedem 20 → 40; remis 8:8 → mniejszy kod.
  - „Map in UTM over a file in degrees”: mapa 32634, piksel 100 m, 20×20, środek w (21°, 52°) (narożnik z `ToMetres`); plik w stopniach: zachód od 21° = 10, wschód = 30 → piksele, których środek leży na zachód od 21° (sprawdzane `PixelToDegrees`), mają 10, na wschód 30; najwyżej 1 piksel różnicy na wiersz przy samej granicy.
  - „Two files cover the two halves of the map”: dwa pliki stykające się na 19,02°, klasy 10 i 50 → żadnego 0 w mapie, granica w odpowiedniej kolumnie, coverage obu ≈ 0,5.
  - „Map without georeference stretches the first file”: mapa none 4×2, plik 8×4 (bez georeferencji) z kolumnami klas → mapa = najczęstsza w blokach 2×2; drugi plik `skipped`.
  - „File without georeference is skipped under a georeferenced map”: status `skipped` niepusty, mapa z drugiego pliku.
  - „No data and outside the files give 0”: plik z kodem 0 w części i mapa wystająca poza plik → 0 tam; counts[0] zgadza się.
  - „A large window is reduced by blocks”: plik 200×200 kafelkowany (tile 64), mapa 10×10 nad całym, `maxRasterSide = 20` → wynik równy klasie najczęstszej w blokach 20×20 pliku (plik ułożony z bloków, w których ta klasa jest jednoznaczna).
  - „Unreadable file reports an error”: ścieżka nieistniejąca → `files[0].error` niepusty, mapa 0, bez wyjątku.
- [ ] **Step 2:** FAIL. **Step 3:** implementacja. **Step 4:** ctest PASS. Pomiar (do ledgera, nie test): mapa 8192² nad syntetycznym plikiem 16384² — czas `Compute` (cel ≤ 10 s; więcej = Ruling z kosztem).

### Task 5: ustawienia, tabela klas, maski, reguły CPU

**Files:**
- Modify: `src/terrainGeneration/MapBiomes.h/.cpp`, `Biome.h/.cpp`, `tests/MapBiomesTests.cpp`, `tests/BiomeTests.cpp` (lub plik, w którym są testy `Nearest`/`MixMaterials`)

**Interfaces:**
- Produces:
  - `biome::constexpr uint64_t allBiomes = ~uint64_t(0);`
  - `int biome::Nearest(const std::vector<Biome>&, const ParameterValues&, uint64_t mask);` (tylko biomy z ustawionym bitem indeksu; maska bez żadnego biomu tabeli → jak bez maski)
  - `MaterialWeights biome::MixMaterials(..., const MaterialWeights& patch, uint64_t mask);` (te same zasady; istniejące wersje = `allBiomes`)
  - `glm::vec3 biome::MixColor(const std::vector<Biome>&, const ParameterValues&, float blendWidth, uint64_t mask);` (kolor jak w shaderze; jeśli już istnieje odpowiednik — użyć go z maską)
  - `mapbiomes::enum class Source { CLIMATE, LAND_COVER };`, `struct LandCoverClass { int code = 0; std::string name; glm::vec3 color{ 0.5f }; std::vector<std::string> biomes; };`
  - `Settings`: `Source source = Source::CLIMATE; std::vector<std::string> landCoverFiles; std::vector<LandCoverClass> classes = WorldCoverClasses();`
  - `std::vector<LandCoverClass> WorldCoverClasses();` — kody, nazwy i biomy ze spec (sekcja „Tabela klas i maski”), kolory legendy WorldCover: 10 (0,0,100,0), 20 (255,187,34), 30 (255,255,76), 40 (240,150,255), 50 (250,0,0), 60 (180,180,180), 70 (240,240,240), 80 (0,100,200), 90 (0,150,160), 95 (0,207,117), 100 (250,230,160) /255.
  - `std::array<uint64_t, 256> ClassMasks(const Settings&);` (kod → bity indeksów biomów z listy nazw; kody spoza tabeli klas i klasy z pustą/nieistniejącą listą → 0 = wszystkie; przy LAND_COVER, przy CLIMATE wszystkie 0)
  - `void AddMissingClasses(Settings&, const std::array<uint64_t, 256>& counts);` (kody obecne na mapie bez wpisu → „Class N”, szary, bez biomów; kolejność po kodzie)
  - JSON (`ToJson`/`FromJson`): `"source": "climate"|"landCover"`, `"landCoverFiles": [...]`, `"classes": [{"code", "name", "color": [r,g,b], "biomes": [...]}]`; brak pól = domyślne (stary JSON czytany); kod spoza 1–255 albo powtórzony → błąd.

- [ ] **Step 1: Testy:**
  - „Masked nearest picks only allowed biomes” (tabela domyślna, wartości, dla których najbliższy = Plains; maska {Forest, Taiga} → jeden z nich, ten z mniejszą odległością), „Mask without biomes of the table means all”.
  - „Masked materials mix only allowed biomes” (blendWidth duży: bez maski udział materiału biomu spoza maski > 0, z maską = 0; suma 1).
  - „Class masks follow biome names” (lista z nazwą usuniętą z tabeli → bit pominięty; lista tylko z nazwami spoza tabeli → 0; zmiana kolejności tabeli przesuwa bity).
  - „WorldCover classes name existing biomes” (każda nazwa z `WorldCoverClasses` jest w `BiomeConfig::Default().table`).
  - „Land cover settings survive JSON” + „Old biome JSON keeps the climate source” + „Repeated class code fails”.
  - „Missing classes are added” (counts[42] > 0 → „Class 42”).
- [ ] **Step 2:** FAIL. **Step 3:** implementacja. **Step 4:** ctest PASS.

### Task 6: GPU — klasa i maska w `map_biome_compute.shader`

**Files:**
- Modify: `GpuHeightGenerator.h/.cpp`, `res/shaders/ComputeShaders/map_biome_compute.shader` (ew. `common/biomes.glsl` — wariant z maską, bez zmiany wersji bez maski), `GpuHeightCheck.cpp`

**Interfaces:**
- Consumes: Task 5 (maski, CPU `Nearest`/`MixMaterials`/`MixColor` z maską).
- Produces: `MapBiomeInput::const TextureClass* classes = nullptr;` (`GL_R8UI` rozmiaru mapy; nullptr = brak pokrycia) i `std::array<uint64_t, 256> classMasks{};` (ignorowane bez `classes`); SSBO maski (binding 2, 256 × uvec2: lo, hi), obraz klas `layout(r8ui, binding = 6)`, uniform `useClasses`; bez pokrycia generator binduje własną teksturę 1×1 zera i maski zerowe.
- Shader: maska teksela (0 = wszystkie) — najbliższy biom, pętla koloru i materiałów tylko po biomach z bitem (`BlendWeight` względem najbliższego dozwolonego); potem malowanie D2 bez zmian.

- [ ] **Step 1:** `--gpu-check` (sekcja `mapbiomes:`): „mapbiomes: land cover limits the biomes” (mapa klas: pasy kodów 10/30/60/0, maski z `ClassMasks(WorldCoverClasses + tabela)`, porównanie id/kolorów/splat z CPU z maską: 0 złych id, kolory ≤ 1e-5, splat ≤ 1/255 + 1e-4; i co najmniej 1 teksel z innym id niż bez pokrycia), „mapbiomes: empty masks equal no land cover” (wszystkie maski 0 → wynik bit w bit jak bez `classes`), „mapbiomes: paint over land cover” (malowanie D2 wygrywa nad maską tam, gdzie s ≥ 128).
- [ ] **Step 2:** build (RED = błąd kompilacji do czasu pól `MapBiomeInput`; zapisz). **Step 3:** implementacja. **Step 4:** ctest + `--gpu-check` „All checks passed”.

### Task 7: `MapImportSys` — źródło, pliki, tabela klas, liczenie w tle

**Files:**
- Modify: `MapImportSys.h/.cpp`, `MapImportBiomes.cpp`

**Interfaces:**
- Consumes: Task 3–6.
- Produces (prywatne): `struct LandCoverResult { landcover::ClassMap map; uint64_t version = 0; uint64_t filesVersion = 0; double ms = 0; std::string error; };`, `GenerationJob<LandCoverResult> landCoverJob`, `std::unique_ptr<TextureClass> classTexture` (`GL_R8UI`), `landcover::ClassMap`-owe statusy (`std::vector<landcover::FileStatus> landCoverFiles`, `std::array<uint64_t, 256> classCounts`), `bool landCoverStale`, `uint64_t landCoverFilesVersion`, `double landCoverMs`, `void UpdateLandCover()`, `void LandCoverImGui()`.
- Liczenie: przy `source == LAND_COVER` i niepustej liście, po: nowej mapie, zmianie listy plików, zmianie georeferencji (`SetGeoreference`), otwarciu projektu; w tle (try/catch `std::exception` → `error`), wynik starszej `mapVersion`/`filesVersion` odrzucany; wynik → `classTexture` (Upload `GL_RED_INTEGER`, `GL_UNSIGNED_BYTE`; wiersze nieparzystej szerokości → `UploadRegion` z wyrównaniem 1), `AddMissingClasses`, `biomesStale = true`. Edycje terenu i morza go nie ruszają.
- `RunBiomePass`: przy `LAND_COVER` i gotowej `classTexture` tego rozmiaru → `input.classes`, `input.classMasks = ClassMasks(biomeSettings)`; inaczej bez pokrycia (jak D1).
- Nowa mapa: `UseBiomeSettings("")` zachowuje `source`, `landCoverFiles`, `classes` poprzednich ustawień (spec, decyzja projektanta); projekt przywraca swoje. Ścieżki plików w projekcie: zapis bezwzględny + względny do folderu projektu (`landCoverFiles` jako bezwzględne w pamięci; przy zapisie JSON dodatkowo `"landCoverFilesRelative"`; przy odczycie: bezwzględna, gdy istnieje, inaczej względna do projektu).

- [ ] **Step 1: UI** (zakładka Biomes, po „Climate origin”, przed „Biomes”): radio „Source”: „Climate from terrain” / „Land cover”; przy Land cover sekcja „Land cover”: `GeoreferenceImGui()` (Task 3), lista plików (nazwa, `system`, „covers N%” / „stretched” / „skipped: …” / błąd na czerwono, przycisk „Remove”), „Add file…” (`OpenFileDialog` z filtrem GeoTIFF), stan („Reading land cover…” / „Land cover read in N ms”), tabela klas (`ImGui::BeginTable`: kolor, kod, nazwa, udział na mapie %, przycisk z liczbą biomów otwierający popup z checkboxami biomów tabeli), „WorldCover defaults”. Komunikat, gdy lista pusta albo żaden plik nie dał klasy: „No land cover on the map: biomes from the climate”. Zmiana listy biomów klasy = `biomesStale` + dirty (bez przeliczania mapy klas).
- [ ] **Step 2:** stan: brak pliku przy otwarciu projektu = `FileStatus::error` + komunikat, źródło zostaje; „Refresh” przy liście plików liczy od nowa.
- [ ] **Step 3:** build, ctest, `--gpu-check` → PASS.

### Task 8: tryb wyświetlania LAND COVER + dokumentacja

**Files:**
- Modify: `src/utility/utilities.h/.cpp` (`heightMapMode::LAND_COVER` na końcu enuma; nazwa w `DisplayModeImGui`), `TerrainView.h/.cpp`, `res/shaders/TerrainShaders/Terrain_evalTes.shader` / `Terrain_fragment.shader` (gdzie liczony jest kolor trybu BIOMES), `MapImportSys.cpp` (Display), `CLAUDE.md`, `docs/ROADMAP.md`

**Interfaces:**
- Consumes: Task 7 (`classTexture`, `biomeSettings.classes`).
- Produces: `TerrainView::SetLandCover(const TextureClass* classes, const std::array<glm::vec3, 256>& colors)` (nullptr = brak); klasa na jednostce tekstur 7 (`usampler2D landCoverMap`, `texelFetch`), paleta jako `uniform vec3 landCoverColors[256]` w FS (kolor kodu 0 = szary 0,5; tylko FS, więc limit uniformów FS ≥ 1024 vec4 wystarcza) — ustawiane w `TerrainView::Draw` jak pozostałe uniformy.
- Tryb dostępny tylko w Imported map z mapą klas (inne tryby: pozycja ukryta w combo); utrata mapy klas (nowa mapa, źródło CLIMATE, biomy wyłączone) przy trybie LAND_COVER → BIOMES (biomy włączone) albo GREYSCALE.

- [ ] **Step 1:** implementacja + build; `--gpu-check` PASS (shader terenu kompiluje się w teście ChunkWorld/TerrainView).
- [ ] **Step 2: Dokumentacja.** CLAUDE.md: `GeoReference.*`, `LandCover.*` w strukturze; akapit D3 przy biomach Imported map (źródło, maska, georeferencja, jednostka 7 w liście jednostek tekstur terenu, tryb 7 w liście `displayMode`); `MapProject` wersja 5. ROADMAP Faza 6 D: 🟡 D3 z datą i listą do sprawdzenia (WorldCover na styku kafli, NMT PUWG 1992, `.asc` bez `.prj`, stary projekt + „Take georeference from file”, tryb LAND COVER, eksport, czas na 8192²).
- [ ] **Step 3:** build, ctest, `--gpu-check` → PASS. Ręcznie (właściciel): powyższa lista.
- [ ] **Step 4:** Bez commita (commituje właściciel).
