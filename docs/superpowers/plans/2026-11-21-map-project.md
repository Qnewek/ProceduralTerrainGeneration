# Faza 6 etap B1 — projekt `.ptgmap` i przenoszenie map — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** tryb Imported map zapisuje i otwiera projekt `.ptgmap` (kopia robocza + oryginał w metrach), przyjmuje mapy wysłane z TerrainGen
i Noise heightmap oraz eksportuje kopię roboczą paczką w prawdziwych metrach.

**Architecture:** format projektu w `ptg_core` (`MapProject.*`, czyste funkcje + zlib, testy). `MapImportSys` trzyma stan projektu (ścieżka,
zmiany niezapisane, źródło, poziom morza, oryginał), zapisuje i otwiera w tle (`GenerationJob`), a okno „Unsaved changes” obsługuje każdą akcję,
która zastąpiłaby mapę, i zamknięcie aplikacji. Subsystemy generacji oddają mapę jako `mapproject::Project`, `TerrainGenApp` przekazuje ją do
`MapImportSys` i przełącza tryb. `ExportPanel` dostaje rozmiar piksela z paczki zamiast stałej 1 m.

**Tech Stack:** C++20, zlib (vcpkg `zlib`), nlohmann_json, doctest, Dear ImGui, comdlg32.

**Spec:** projekt zaakceptowany w czacie 2026-11-21 (bez pliku — reguła właściciela), w skrócie w `docs/ROADMAP.md` → Faza 6, punkt B1.
Decyzja właściciela: polecenia projektu w zakładce „Project” prawego panelu Imported map (obok Load), nie w pasku menu.

## Global Constraints

- Wysokości w projekcie: metry, float32, `map[y * width + x]`; mapa ≤ `dem::maxMapSize` (8192) na bok, ≥ 1.
- Plik: 8 bajtów sygnatury `"PTGMAP\x1A\n"`, `uint32` little-endian = długość nagłówka, nagłówek JSON (UTF-8), potem bloki w kolejności z nagłówka.
  Blok = strumień zlib (`compress2`, poziom 3) floatów rozdzielonych na 4 płaszczyzny bajtów (płaszczyzna k = bajt k każdego floata).
- Nagłówek: `version` (1), `width`, `height`, `pixelMetres` `[x, y]`, opcjonalne `seaLevel` (m), `source`, `generation` (tekst presetu, może być pusty),
  `verticalExaggeration`, `blocks`: `[{"name": "working", "bytes": N}, {"name": "original", "bytes": M}]` — `original` tylko, gdy jest.
- Zapis przez `WriteFileAtomic` (`FileIO.h`). Zapis i odczyt projektu poza wątkiem głównym.
- Domyślny folder projektów `projects/` (względem katalogu roboczego, poza gitem), rozszerzenie `.ptgmap`.
- UI po angielsku, komentarze po angielsku (`//@param`), konwencje z CLAUDE.md (PascalCase, `(width, height)`, `[ERROR]`/`[LOG]`).
- Nowe pliki do jawnych list w `CMakeLists.txt`. Build przez `.bat` z `VsDevCmd.bat -arch=x64 -host_arch=x64` (pamięć: local-build-environment).
- Bez commitów — właściciel commituje sam po obejrzeniu w aplikacji.

## Review Focus

- Wynik wczytywania (Load albo Open) przychodzący po „Send to Imported map” lub po innym Open nie może zastąpić nowszej mapy — starszy wynik jest odrzucany (Task 5).
- Ctrl+S / Save w trakcie trwającego zapisu albo wczytywania: ignorowane z komunikatem, bez drugiego zadania i bez awarii (Task 3).
- Nieudany Open (uszkodzony plik) zostawia bieżącą mapę, nazwę projektu i stan zmian bez zmian, pokazuje błąd (test w Task 1 + Task 3).
- Zapis, który się nie udał (np. folder docelowy jest plikiem), zostawia `*` i pokazuje błąd (test w Task 1 + Task 3).
- Zamknięcie okna z niezapisanymi zmianami, gdy aktywny jest inny tryb, też pokazuje okno pytania (Task 4).

---

### Task 1: Format projektu (`MapProject.*`)

**Files:**
- Create: `src/terrainGeneration/MapProject.h`, `src/terrainGeneration/MapProject.cpp`; Test: `tests/MapProjectTests.cpp`
- Modify: `vcpkg.json` (`"zlib"`), `CMakeLists.txt` (`find_package(ZLIB REQUIRED)`, `ZLIB::ZLIB` PRIVATE w `ptg_core`, nowe pliki w `ptg_core` i `ptg_tests`)

**Interfaces:**
- Produces (namespace `mapproject`):
  - `struct Project { HeightField working; HeightField original; glm::dvec2 pixelMetres{ 1.0 }; std::optional<float> seaLevel; std::string source; std::string generation; float verticalExaggeration = 1.0f; };`
    — `working`/`original` w metrach, `original` puste = taki sam jak `working`; `seaLevel` w metrach.
  - `inline constexpr int currentVersion = 1;` `inline constexpr const char* extension = ".ptgmap";`
  - `bool Serialize(const Project& project, std::vector<uint8_t>& out, std::string& error);` — błąd: pusta/za duża mapa, `original` innego rozmiaru, wartość nieskończona/NaN.
  - `bool Deserialize(const uint8_t* data, size_t size, Project& out, std::string& error);` — `out` zmieniane tylko przy sukcesie.
  - `bool Save(const std::filesystem::path& path, const Project& project, std::string& error);` `bool Load(const std::filesystem::path& path, Project& out, std::string& error);`
  - `std::vector<std::filesystem::path> ListProjects(const std::filesystem::path& folder);` — pliki `.ptgmap`, posortowane, pusta lista bez folderu.

- [ ] **Step 1: Testy** (`MapProjectTests.cpp`, mapa 5×3 `v = y * 10 + x - 7.25f`):
  - „Project round trip keeps every field”: `original` = `working + 1`, `pixelMetres {30, 28}`, `seaLevel 12.5f`, `source "alps.tif"`, `generation "{\"a\":1}"`,
    `verticalExaggeration 2.0f` → po `Serialize` + `Deserialize` wszystko równe, floaty bit w bit (`std::memcmp`).
  - „Project without original, sea level and generation”: po odczycie `original.Empty()`, `!seaLevel`, `generation == ""`.
  - „Truncated project fails”: bajty ucięte do `size - 1`, `size / 2`, `10`, `0` → `false`, `error` niepusty, `out` (wcześniej mapa 2×2 = 9) bez zmian.
  - „Damaged project fails”: odwrócony bajt w ostatnim bloku → `false`; zła sygnatura (bajt 0) → `false`.
  - „Project from a newer version fails”: w bajtach nagłówka `"version":1` → `"version":2` (ta sama długość) → `false`, `error` zawiera `"newer"`.
  - „Non-finite heights are not saved”: `working[3] = NaN` → `Serialize` zwraca `false`; `original` 4×3 przy `working` 5×3 → `false`.
  - „Save and Load a file”: folder `temp_directory_path() / "ptg_project_tests"` (czyszczony na początku); `Save` + `Load` → równe; plik `.png` obok i `ListProjects` → tylko `.ptgmap`;
    `Save` do `<plik>/x.ptgmap`, gdzie `<plik>` jest zwykłym plikiem → `false` z błędem.
  - „Smooth map compresses”: 256×256 `v = 300 * sin(x / 40.0) * cos(y / 50.0)` → `Serialize` daje mniej bajtów niż `256 * 256 * 4`.
- [ ] **Step 2: Build `ptg_tests`** — oczekiwane: błąd kompilacji (brak `MapProject.h`).
- [ ] **Step 3: Implementacja** wg Global Constraints. Na MSVC `uLong` ma 32 bity — 8192² · 4 B mieści się; rozmiar rozpakowany = `width * height * 4`
  z nagłówka, `uncompress` musi dać dokładnie tyle. Przed alokacją sprawdzić rozmiar mapy i to, że suma `bytes` bloków mieści się w pliku.
  Nieznana nazwa bloku albo brak `working` → błąd.
- [ ] **Step 4: `ctest --preset x64-release`** — oczekiwane: wszystkie testy przechodzą (dotychczasowe 168 + nowe).

### Task 2: Rozmiar piksela w paczce eksportu

**Files:**
- Modify: `src/terrainGeneration/ExportManifest.h/.cpp`, `src/terrainGeneration/MeshExport.h/.cpp`, `src/utility/ExportPanel.h/.cpp`,
  `src/subSystems/TerrainGenerationSys.cpp:916`; Test: `tests/ExportManifestTests.cpp`, `tests/MeshExportTests.cpp`

**Interfaces:**
- Produces: stała `exportpack::metresPerTexel` usunięta; w jej miejsce pola `float metresPerTexel = 1.0f;` w `exportpack::Manifest`, `meshexport::Settings`
  i `ExportPanel::Pack`. `ExportPanel` przepisuje `pack.metresPerTexel` do manifestu i ustawień siatek. W JSON nazwa `grid.metresPerTexel` bez zmian (bez podbicia `manifestVersion`).

- [ ] **Step 1: Testy:** „Manifest uses the pixel size of the pack”: `width 11, height 6, metresPerTexel 30` → `grid.metresPerTexel == 30`, `grid.sizeMetres == [300, 150]`,
  Unreal `scale[0] == 3000`, Unity `terrainSize[0] == 300`. „Mesh export uses the pixel size”: mapa 9×5, `metresPerTexel 30`, LOD 1, OBJ/GLB jak w istniejących testach
  → największe X wierzchołka `== 240`, Z `== 120`.
- [ ] **Step 2: Build** — oczekiwane: błąd kompilacji (brak pola).
- [ ] **Step 3: Implementacja:** każde użycie stałej (`ExportManifest.cpp` 81/83/171/177, `MeshExport.cpp` 33/55/76) bierze pole; woda rzek w `TerrainGenerationSys.cpp:916`
  mnoży przez `1.0f` jak dotąd przez `pack.metresPerTexel` (TerrainGen i Noise zostawiają domyślne 1).
- [ ] **Step 4: `ctest --preset x64-release`** — wszystkie przechodzą.

### Task 3: Zakładka „Project” — Open, Save, Save as, Ctrl+S

**Files:**
- Modify: `src/subSystems/MapImportSys.h/.cpp`, `src/utility/FileDialog.h/.cpp`

**Interfaces:**
- Consumes: `mapproject::Project`, `Save`, `Load`, `ListProjects`, `extension` (Task 1).
- Produces:
  - `std::optional<std::filesystem::path> SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExtension, const std::filesystem::path& startFolder, const std::string& suggestedName, std::string& error);`
    (`GetSaveFileNameW`, `OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR`, `lpstrDefExt`; nazwa UTF-8 → wide).
  - `MapImportSys`: `bool HasUnsavedChanges() const;` `void SaveProject();` (bez ścieżki → `SaveProjectAs()`), `void SaveProjectAs();`, `void OpenProject(const std::filesystem::path& path);`
  - Wspólna instalacja mapy: `void InstallMap(HeightField units, float metresPerUnit, glm::dvec2 pixelMetres)` (min/max, `trueHeightScale`, `view.Resize/SetLayer`), używana przez Load, Open i Receive (Task 5).
  - Pola: `projectPath` (puste = bez nazwy), `bool dirty`, `std::string projectSource`, `projectGeneration`, `std::optional<float> seaLevelMetres`, `HeightField original` (jednostki mapy, puste = jak `map`),
    `GenerationJob<ProjectLoad> projectJob`, `GenerationJob<SaveResult> saveJob`, `std::string projectStatus`, `char projectFolder[260] = "projects"`.

- [ ] **Step 1: Implementacja zapisu:** na wątku głównym kopia `map` (i `original`) do zadania; w zadaniu mnożenie przez `metresPerUnit` → `Project` (`verticalExaggeration = heightScale / trueHeightScale`,
  `pixelMetres`, morze, źródło, generacja) → `mapproject::Save`. Po sukcesie `projectPath = ścieżka`, `dirty = false`, status „Saved <nazwa> (x MB, y ms)”; po błędzie `dirty` bez zmian, błąd w statusie.
  Save w trakcie zapisu lub wczytywania → status „Wait for the current save/load to finish”, nic nie startuje.
- [ ] **Step 2: Implementacja odczytu:** `OpenProject` → zadanie `mapproject::Load` + przeliczenie na jednostki (`metresPerUnit = max(|min|, |max|, 1 m)` z `working`, ten sam dla `original`).
  W `Update`: sukces → `InstallMap`, `heightScale = trueHeightScale * verticalExaggeration`, stan projektu z pliku, `dirty = false`, kamera z góry (Task 5 dodaje `Camera&`); błąd → tylko komunikat.
  Load z pliku mapy (istniejący) = nowy projekt bez nazwy: `projectPath` puste, `source` = nazwa pliku, `generation` puste, `seaLevelMetres` brak, `original` puste, `dirty = true`.
- [ ] **Step 3: UI:** prawy panel = zakładki „Project” i „Load” (`importTabs`). Project: nazwa (`projectPath.filename()` albo „Untitled”) + `" *"` przy `dirty`, źródło, „Open…” (`OpenFileDialog`, filtr
  `L"Terrain project (*.ptgmap)\0*.ptgmap\0"`), „Save” (wyłączony bez mapy), „Save as…” (`SaveFileDialog`, sugerowana nazwa = nazwa projektu albo stem źródła), folder + lista `ListProjects` z „Refresh”
  (dwuklik = Open), status. Ctrl+S: `ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)` w `ImGuiRightPanel`, gdy `!ImGui::GetIO().WantTextInput`. Output: nazwa projektu z `*` przed nazwą pliku mapy.
- [ ] **Step 4: Build `x64-release` + `ctest --preset x64-release`** — oczekiwane: build bez ostrzeżeń, wszystkie testy przechodzą. W aplikacji (właściciel): Load DEM → `*` → Save as → `*` znika → Open tego pliku daje tę samą mapę i przewyższenie.

### Task 4: Okno „Unsaved changes” i zamknięcie aplikacji

**Files:**
- Modify: `src/subSystems/MapImportSys.h/.cpp`, `src/subSystems/TerrainGenApp.cpp`

**Interfaces:**
- Consumes: `HasUnsavedChanges`, `SaveProject`, `OpenProject` (Task 3).
- Produces: `void ImGuiModals();` (wołane przez `TerrainGenApp::ImGuiRender` w każdym trybie), `void RequestQuit();`, `bool TakeQuit();` (raz `true`, gdy wolno zamknąć).
  Wewnętrznie odłożona akcja `PendingAction { enum Kind { OPEN, LOAD, RECEIVE, QUIT } kind; std::filesystem::path path; std::optional<mapproject::Project> received; }`.

- [ ] **Step 1: Implementacja:** Open, Load i Receive (Task 5) przy `dirty` zapamiętują akcję i otwierają popup „Unsaved changes” (`BeginPopupModal`): „Save” (projekt bez nazwy → Save as;
  anulowanie okna pliku = anulowanie akcji; akcja wykonywana dopiero po udanym zapisie, błąd zapisu = akcja porzucona z komunikatem), „Discard” (akcja od razu), „Cancel”.
  Bez `dirty` akcja od razu.
- [ ] **Step 2: Zamknięcie:** w `TerrainGenApp::Start` po `glfwPollEvents`: `glfwWindowShouldClose` przy `mapImportSys.HasUnsavedChanges()` → `glfwSetWindowShouldClose(window, GLFW_FALSE)` + `RequestQuit()`
  (raz, nie co klatkę); `TakeQuit()` → `glfwSetWindowShouldClose(window, GLFW_TRUE)`. Popup rysuje `ImGuiModals` niezależnie od trybu.
- [ ] **Step 3: Build + `ctest`** — oczekiwane: jak w Task 3. W aplikacji: zmieniony projekt + zamknięcie okna z trybu TerrainGen → pytanie; Cancel zostawia aplikację, Discard zamyka.

### Task 5: „Send to Imported map”

**Files:**
- Modify: `src/subSystems/TerrainGenerationSys.h/.cpp`, `src/subSystems/NoiseBasedGenerationSys.h/.cpp`, `src/subSystems/MapImportSys.h/.cpp`, `src/subSystems/TerrainGenApp.cpp`

**Interfaces:**
- Consumes: `mapproject::Project` (Task 1), `InstallMap` (Task 3), odłożona akcja `RECEIVE` (Task 4).
- Produces: `std::optional<mapproject::Project> TakeSentMap();` w obu subsystemach generacji; `void MapImportSys::Receive(mapproject::Project project);`;
  `void MapImportSys::Update(Camera& camera);` (zamiast `Update()`); licznik `uint64_t mapVersion` w `MapImportSys`.

- [ ] **Step 1: Wysyłanie:** w `ImGuiSaving` obu trybów (tam, gdzie działa eksport — w świecie nieskończonym niedostępne) sekcja „Imported map”: „Send map” i „Send eroded copy”
  (tylko gdy kopia po erozji jest pokazana; Noise: przy obu metodach dwa przyciski „pipes”/„droplets”). Mapa = te same odczyty co `pack.heightmaps` × `pack.metresPerUnit`; piksel 1 m;
  `seaLevel = WaterHeight() * metresPerUnit` (tylko TerrainGen); `source = "TerrainGen map at (x, y)"` / `"Noise heightmap map at (x, y)"` z `GetMapOffset()` + `", eroded (pipes)"` itp.;
  `generation = preset::ToJson(...)`. Wynik w polu `sentMap` oddawany raz przez `TakeSentMap`.
- [ ] **Step 2: Przyjęcie:** `TerrainGenApp` co klatkę (przed `ImGuiRender`): `TakeSentMap()` z obu → `mapImportSys.Receive(...)`, `currentMode = MAP_IMPORT`.
  `Receive` → akcja `RECEIVE` (Task 4); wykonanie: przeliczenie na jednostki (`metresPerUnit = max(|min|, |max|, 1 m)`), `InstallMap`, `heightScale = trueHeightScale`, nowy projekt bez nazwy
  ze źródłem/generacją/morzem z wysłanej mapy, `dirty = true`, `lookDown = true`. `Update(camera)` przy `lookDown` → `TerrainView::LookDownAtMap(camera, renderSettings, {0, 0}, rozmiar)`
  (także po udanym Open).
- [ ] **Step 3: Starsze wyniki:** każde zadanie Load/Open zapamiętuje `mapVersion` z chwili startu; każda instalacja mapy zwiększa `mapVersion`; wynik zadania ze starszą wersją jest odrzucany
  (status „Discarded an older load”). Load/Open w trakcie innego wczytywania → komunikat, bez drugiego zadania (jak dziś przy `job.IsRunning()`).
- [ ] **Step 4: Build + `ctest`** — oczekiwane: jak w Task 3. W aplikacji: TerrainGen → Send map → Imported map z tą samą rzeźbą, przewyższenie x1.00, „Heights” w Output = zakres w metrach z TerrainGen,
  `*` przy „Untitled”; Send eroded copy pokazuje kopię po erozji.

### Task 6: Saving w Imported map + dokumentacja

**Files:**
- Modify: `src/subSystems/MapImportSys.h/.cpp`, `src/subSystems/TerrainGenApp.cpp` (zakładka Saving dla `MAP_IMPORT`), `CLAUDE.md`, `docs/ROADMAP.md`

**Interfaces:**
- Consumes: `ExportPanel::Pack::metresPerTexel` (Task 2), stan projektu (Task 3).
- Produces: `void MapImportSys::ImGuiSaving();` (pole `ExportPanel exportPanel{ "exports" }`, `exportPanel.Update()` w `Update`).

- [ ] **Step 1: Implementacja:** bez mapy tekst „Load a map or open a project first.”; inaczej `Pack`: `mode = "Imported map"`, `range = {-1, 1}`, `metresPerUnit`, `metresPerTexel = pixelMetres.x`,
  `seaLevel = seaLevelMetres / metresPerUnit` (jeśli jest), `generation` = `projectGeneration` (puste → `[] { return std::string(); }`), `heightmaps = { { w, h, "", [this] { return map; } } }`,
  `layersMissing = "Material and biome maps: not available for imported maps."`, `vegetationMissing = "Vegetation: not available for imported maps."`.
- [ ] **Step 2: Dokumentacja:** CLAUDE.md — w Strukturze `MapProject.*` (format pliku jednym zdaniem) i rozszerzona linia `MapImportSys.*` (projekt, zakładki Project/Load, Saving, przyjmowanie map);
  w Pułapkach przy imporcie: projekt trzyma metry, mapa w pamięci = jednostki (`metresPerUnit` liczone na nowo przy Open/Receive), `exportpack::metresPerTexel` jest teraz polem paczki,
  okno „Unsaved changes” (każda akcja zastępująca mapę idzie przez odłożoną akcję). ROADMAP Faza 6 B1: 🟡 2026-11-21 (branch `feat/map-project`) + „Do sprawdzenia w aplikacji”:
  czas zapisu/odczytu 8192², rozmiar pliku po kompresji, kadr po Send/Open.
- [ ] **Step 3: Weryfikacja końcowa:** build `x64-debug` i `x64-release` bez ostrzeżeń, `ctest --preset x64-release` — wszystkie przechodzą. Shadery bez zmian, więc `--gpu-check` niepotrzebny.
  W aplikacji: Saving w Imported map z DEM 30 m → manifest `grid.metresPerTexel == 30`, siatka GLB w prawdziwych metrach.
