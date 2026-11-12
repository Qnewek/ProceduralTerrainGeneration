# Procedural Terrain Generator — analiza i plan rozwoju

Diagnoza na 2026-10-27 (branch `lodChunked` = `master`, commit `fbd32ab`); postęp odhaczany na bieżąco, ostatnia aktualizacja 2026-11-12
(Fazy 0–3 i Teren 2.0 zamknięte: fundament i porządki, generacja wyłącznie na GPU, chunki z LOD, biomy ciągłe z edytorem, klimatem, modyfikatorami terenu
i teksturowaniem; następna Faza 4 — roślinność).
Cel projektu: **generowanie terenu z edycją parametrów w czasie rzeczywistym, maksymalna wydajność,
biomy, roślinność i kolejne feature'y.**

---

## 1. Diagnoza

### 1.1 Błędy (B) — rzeczy, które działają źle już teraz

| ID | Gdzie | Problem | Skutek |
|----|-------|---------|--------|
| ~~B1~~ ✅ | `BiomeGenerator.cpp:72` | `if (GenerateComponentNoises()) return false;` — odwrócony warunek | **Zrobione 2026-10-28:** warunek odwrócony na `!GenerateComponentNoises()` |
| ~~B2~~ ✅ | `BiomeGenerator.cpp:129` | `DetermineBiome(H, T, ...)` przy sygnaturze `(temperature, humidity, ...)` | **Zrobione 2026-10-28:** `DetermineBiome(T, H, ...)`. Tabela biomów była projektowana pod T jako pierwszy argument; zakresy domyślnych biomów się nie nakładają (kolejność `unordered_map` nie ma znaczenia), luki (np. gorąco+wilgotno na lądzie) spadają na fallback id 0 = Grassplains — do przeprojektowania w Fazie 3 |
| ~~B3~~ ✅ | `utilities.cpp:312, 400`, `TerrainGenerator.cpp:186, 203`, `Noise.cpp:166` | Brak `return` na części ścieżek (C4715) | **Zrobione 2026-10-28:** brakujące `return` dodane, `GetSelectedNoiseConfig` deleguje do `GetSelectedNoise`. Build bez C4715; test ręczny — zwinięte nagłówki nie wyzwalają regeneracji |
| ~~B4~~ ✅ | `NoiseBasedGenerationSys.cpp:195` | `PushStyleColor(..., (1.0f, 0.0f, 0.0f, 1.0f))` — operator przecinka | **Zrobione 2026-10-28:** `ImVec4(1, 0, 0, 1)` |
| ~~B5~~ ✅ | `TerrainGenerator.cpp:66`, `NoiseBasedGenerationSys.cpp:53,130`, `TerrainGenerationSys.cpp:33`, shadery (`size = height/2`) | Niespójna kolejność `width/height` między API | **Zrobione 2026-10-29:** wszędzie `(width, height)` (w tym `TextureClass`/`glTexImage2D`), TES liczy texel osobno dla x i y; checksum benchmarku bez zmian |
| ~~B6~~ ✅ | `Terrain_evalTes.shader` (od A4 jedyny TES) | `FragPos` liczony w clip space, światło w world space; normalna liczona z kroku w UV (`texel`) vs wysokość w jednostkach świata; normalna nie przechodzi przez `model`; tryb 2D (`flatten`) ignoruje `model` | **Zrobione 2026-10-29:** `FragPos` w world space, krok normalnej `texelWorldSize`, `normalMatrix`; tryb 2D = ortogonalna kamera z góry w `TerrainView` (cała mapa, proporcje viewportu, `modelScale`, obie mapy przy erozji) — wcześniej pokazywał ~1/16 mapy obróconej o 90° |
| ~~B7~~ ✅ | `TerrainGenerationSys::Draw`, `NoiseBasedGenerationSys::Draw` | Tryb infinite: regeneracja **całej** mapy + nowa tekstura z mipmapami przy każdym ruchu kamery, w wątku renderującym; biomy i podgląd szumów zawsze z originem (0,0); `MakeIsland` zależy od rozmiaru mapy | **Zrobione 2026-11-08:** chunki (`ChunkWorld`, Faza 2 pkt 3); biomy i podgląd z originem chunka, wyspa wg rozmiaru mapy (`islandSize`) |
| ~~B8~~ ✅ | `Noise.cpp:82` + `vendor/Simplex/SimplexNoise.cpp:105` | `PointNoise` woła `reseed(seed)` przy każdej próbce; tablica permutacji jest globalna | **Zrobione 2026-10-28:** permutacja per instancja, `reseed` poza hot-path; teren 512² 3239 → 227 ms (14,3×), checksum bez zmian |
| ~~B9~~ ✅ | `Erosion.cpp:45`, klasy z `new[]` | `Erosion::map` nigdy nie zwalniany; klasy z surowymi wskaźnikami mają domyślny copy-ctor | **Zrobione 2026-10-29:** wszystkie mapy w `Grid<T>` (`std::vector`), brak `new[]`/`delete[]` poza vendor |
| ~~B10~~ ✅ | `Camera.cpp:20` | `SetCameraConfig` przypisuje parametry same do siebie (shadowing); FOV slider do 300° | **Zrobione 2026-10-28:** parametry z prefiksem `_`, ustawia też `speed`; FOV slider 1–120° |
| ~~B11~~ ✅ | `TerrainGenApp.cpp:27-90`, `Application.cpp` | `Initialize` zwraca 0 przy błędzie i sukcesie; `glewInit` błąd ignorowany; `ImPlot::DestroyContext` po `ImGui::DestroyContext` | **Zrobione 2026-10-28:** `Initialize` zwraca `bool` z logami `[ERROR]`, `main` przerywa przy błędzie, błąd `glewInit` przerywa start, `ImPlot::DestroyContext` przed `ImGui::DestroyContext` |
| ~~B12~~ ✅ | `TerrainGenerationSys.cpp:376`, edytory | Nagłówek „Spline editor” użyty dwa razy w jednym oknie (kolizja ID ImGui); `static int` w edytorach; `'0' + i` dla `size_t`; pętla `j > 0` w `SegmentDrag` nie poprawia granicy 0 | **Zrobione 2026-10-28:** drugi nagłówek → „Biome levels editor”; stan edytorów (`*PressedButton`, `splineDraggedPoint`) jako pola klasy, metoda ewaluacji czytana z `terrainGen`; etykiety poziomów `%d`. (Zarzut o `j > 0` był błędny — granica 0 to stałe −1, a przeciągane wartości są do niej clampowane) |
| ~~B13~~ ✅ | `.gitignore`, vcxproj | `.sln`, `.vcxproj`, `.filters`, `Dependencies/GLEW/lib` ignorowane; x64 bez include/lib; Release bez `NDEBUG`; vcxproj wskazuje nieistniejące pliki (`ValueGenerators/*`, `Terraformers/*`, `TerrainObjects/*`) | **Zrobione 2026-10-28:** pliki projektu i libki GLEW śledzone, x64 usunięte (GLFW w repo tylko x86 — wróci z A8), `NDEBUG` w Release, martwe wpisy usunięte, `imgui.ini` w ignore. Zweryfikowane buildem Debug/Release z czystego eksportu indeksu |
| ~~B14~~ ✅ | `TerrainGenerator.cpp` | `int resolution` inicjalizowany `500.0f`; `EvaluationMethod::C` nie ustawia `elevation` (zostaje wartość z poprzedniego piksela); `SetResolution` nie regeneruje terenu automatycznie | **Zrobione 2026-10-28:** `resolution(500)`; `C`/`default` ustawiają `elevation = 0`. (Zarzut o `SetResolution` był błędny — przycisk „Change resolution” woła `GenerateTerrain`) |
| ~~B15~~ ✅ | `TextureClass`, subsystemy | Każda zmiana parametru = `glGenTextures` + `glTexImage2D` + `glGenerateMipmap` | **Zrobione 2026-10-30:** `TextureClass` = niezmienny storage (`glTextureStorage2D`, DSA) + `Upload` przez `glTextureSubImage2D`; `TerrainView` tworzy teksturę warstwy tylko przy braku lub zmianie rozmiaru. Mipmapy usunięte — mapy czyta tylko TES, gdzie `texture()` zawsze bierze poziom 0, więc były liczone na darmo. Nieużywane konstruktory (stb, `unsigned char*`) usunięte |
| ~~B16~~ ✅ | `LightSource.cpp:52`, `Shader.cpp` | Ścieżka `Light_source_*.shader` vs plik `light_source_*.shader`; brak sprawdzania statusu linkowania programu; komunikat zawsze „vertex/fragment”; `malloc` bez `free` | **Zrobione 2026-10-28:** ścieżki małymi literami, `CheckLinkStatus` po `glLinkProgram`, nazwy wszystkich 4 typów shaderów w komunikacie, log w `std::string` zamiast `malloc` |
| ~~B17~~ ✅ | `NoiseBasedGenerationSys::Draw` | Mapa po erozji przesuwana o `width + 1`, a siatka ma szerokość `width * 4` | **Zrobione 2026-10-29 (A4):** przesunięcie = `TerrainView::GetWorldWidth() * 1.05` |
| ~~B18~~ ✅ | `TerrainGenerationSys::NoisesLevelsForBiomes` | Przeciąganie poziomów biomów zmieniało dane, ale nie przeliczało mapy biomów (zmiana widoczna dopiero po innej regeneracji) | **Zrobione 2026-10-29:** `SegmentDrag` zwraca informację o zmianie, przycisk „Apply biome levels” (aktywny tylko przy niezastosowanych zmianach) regeneruje mapę — na żądanie, bo przeliczana jest cała mapa |
| ~~B19~~ ✅ | `Noise.cpp` (`Evaluate`) | Przy opcji innej niż `NOTHING` ujemna wartość po `Ridge` trafiała do `pow` z ułamkowym wykładnikiem | **Zrobione 2026-11-02:** redistribution zachowuje znak przy każdej opcji (jak dotąd przy `NOTHING`), bez NaN w mapie. Przy parzystym wykładniku ujemne wartości po `Ridge` zostają teraz ujemne (wcześniej były dodatnie) |
| ~~B20~~ ✅ | `Erosion.cpp` (`AdjustVelocity`) | `deltaElevation` = nowa − stara wysokość, czyli ujemna w dół, więc kropla zwalniała w dół i przyspieszała pod górę; przy małej prędkości lub dużym `gravity` pierwiastek dawał NaN, a NaN w pojemności kazał `ErodeRadius` kopać sąsiednie komórki do 0 | **Zrobione 2026-11-02:** `sqrt(max(0, v² − Δh·g))`, kropla bez siły na podjazd ma prędkość 0 (i dalej się przesuwa). Test regresji: na stromym stoku stary kod wykopywał 3064 z 3072 komórek. Erozja wygląda inaczej (mocniejsza niżej na stokach), wartości domyślne bez zmian; benchmark jej nie obejmuje |
| ~~B21~~ ✅ | `vendor/Simplex/SimplexNoise.cpp` | Tablice `perm`/`originalPerm` miały 247 wartości zamiast 256 (ręcznie zmieniona tabela): brak 23, 40, 94, 138, 159, 172, 181, 239, 242, a `0` występowało 10 razy | **Zrobione 2026-11-02:** brakujące wartości dopisane, `std::shuffle` zastąpiony własnym Fisher-Yates na `mt19937` (wynik nie zależy już od biblioteki standardowej, test porównuje go z osobną implementacją). Zmieniły się wszystkie światy i checksumy. Nie sprawdzone, czy VS 2026 daje teraz te same checksumy co VS 2022 (przyczyna różnic nie była potwierdzona, mogą też wynikać z obliczeń `float`) |
| ~~B22~~ ✅ | `Camera::SteerCamera` | Kamera czytała klawisze z GLFW także podczas wpisywania tekstu w polach ImGui (WASD, spacja, Ctrl, M, Esc) | **Zrobione 2026-11-03:** klawisze ignorowane, gdy `ImGui::GetIO().WantTextInput`; obrót myszą działa dalej |

### 1.2 Problemy architektury i jakości (A)

| ID | Problem | Dlaczego przeszkadza w celu projektu |
|----|---------|--------------------------------------|
| A1 | **Generacja wyłącznie na CPU, jednowątkowo, synchronicznie w pętli renderującej** | Edycja parametrów w czasie rzeczywistym jest możliwa tylko dla małych map; każdy suwak blokuje klatkę |
| ~~A2~~ ✅ | Ręczne zarządzanie pamięcią (`new[]`/`delete[]`, raw pointers), brak rule-of-5 | Błędy B9, trudne refaktory — **Zrobione 2026-10-29:** mapy w `Grid<T>` (`std::vector`) |
| ~~A3~~ ✅ | Logika generacji, stan UI i rendering wymieszane w subsystemach (`TerrainGenerationSys` = 420 linii UI+GL+logika) | Nie da się przenieść generacji na wątek/GPU ani testować bez okna — **Zrobione 2026-10-29:** config w `WorldConfig.h`, generatory bezstanowe względem configu, GL w `TerrainView` |
| ~~A4~~ ✅ | Duplikacja: dwa subsystemy z identycznym `Initialize/Draw/Resize`, dwa zestawy shaderów różniące się kilkoma liniami | Każdą poprawkę (np. B6) trzeba robić 2× — **Zrobione 2026-10-29:** wspólny `TerrainView`, jeden zestaw shaderów |
| A5 | Brak modelu „świata”: mapa = jeden prostokąt `width×height` przypięty do kamery; brak chunków, LOD, cache | Infinite terrain i duże światy są niewykonalne wydajnie — ✅ chunki z cache i LOD (quadtree), 2026-11-08 |
| A6 | Biomy oparte o dyskretne poziomy `int` i pierwszy pasujący biom z `unordered_map` (kolejność nieokreślona!) | Ostre granice, niedeterministyczny wybór przy nakładaniu się zakresów, brak blendingu, trudne do konfiguracji — ✅ ciągła przestrzeń parametrów, najbliższy biom (remis → pierwszy w tabeli) i blending, 2026-11-08 (Faza 3) |
| A7 | Brak serializacji (presety, konfiguracje biomów, eksport) — „Saving” to placeholder | Nie da się zapisać efektu pracy ani porównać ustawień — ✅ presety JSON i eksport heightmapy (2026-11-03), tabela biomów z materiałami w presetach (Faza 3) |
| A8 | Brak testów, CI, profilera; brak menedżera zależności (binarki w repo) | Regresje niewykrywalne; aktualizacje bibliotek ręczne — **Częściowo:** CMake + vcpkg, CI z kontrolą checksum (2026-10-30), testy jednostkowe (2026-11-02), panel pomiarów (2026-11-03); Tracy odłożony (Faza 1 pkt 8) |
| ~~A9~~ ✅ | Martwy kod: `VegetationGenerator` (pusty), CPU-mesh w `utilities.cpp` (`MapToVertices`, `PerformErosion`, `PaintVerticesByHeight`, `MeshIndicesStrips`, `CalculateHeightMapNormals`), `TINYOBJLOADER_IMPLEMENTATION` bez użycia, `DrawTriangleStrips`, gałęzie git bez scalenia | Szum poznawczy — **Zrobione 2026-10-30:** usunięte `VegetationGenerator`, CPU-mesh z `utilities` (zostały `GenerateVerticesForResolution`, `GetBiomeColorMap` i helpery ImGui), `DrawTriangleStrips`, nieużywane vendor `ObjLoader` (tiny_obj_loader) i `PoissonSampling` — wrócą w nowej postaci w Fazie 4. `res/textures/` zostaje (assety pod Fazę 3) |
| ~~A10~~ ✅ | Higiena repo: śledzony `imgui.ini`, commity „sda/asd/x”, 17 zdalnych branchy, README z pustymi sekcjami | Trudno wrócić do projektu po przerwie (jak teraz) — **Zrobione 2026-10-30:** `imgui.ini` nieśledzony (od B13), scalone branche usunięte lokalnie i z GitHuba (zostaje `master`), jedyny nie scalony commit (`infiniteTerrain`, klasa `Chunk`) zachowany jako tag `archive/infinite-terrain-chunks`; README przepisany (funkcje, build, sterowanie, benchmark) — brakuje screenshotów |

---

## 2. Docelowa architektura (w skrócie)

```
App (okno, pętla, input)
 ├─ UI layer (ImGui)            ── edytuje ──►  WorldConfig (POD: szumy, splajny, biomy, roślinność, erozja)
 │                                                  │ dirty flags + wersja
 ├─ GenerationService  ◄────────────────────────────┘
 │    ├─ GPU path: compute shadery (height, biome weights, normal, splat) → tekstury per chunk
 │    └─ CPU path: ten sam algorytm (SIMD/wątki) do zapytań gameplay/eksportu/testów
 ├─ ChunkManager (quadtree/clipmap, cache LRU, kolejka priorytetowa wokół kamery)
 ├─ TerrainRenderer (teselacja, frustum culling w TCS, splat texturing, world-space lighting)
 └─ VegetationSystem (Poisson per chunk → bufor instancji → indirect draw)
```

Zasady:
- **Generacja jest funkcją czystą `f(worldX, worldZ, config) → próbka`** — bez zależności od rozmiaru mapy
  i pozycji kamery. To warunek konieczny dla chunków, infinite terrain i spójności biomów.
- UI nie dotyka obiektów GL ani generatorów — zmienia `WorldConfig` i ustawia dirty-flagi.
- Jedno źródło prawdy dla kodu szumu na GPU (GLSL `#include`) i CPU (C++), z testem porównującym wyniki.

---

## 3. Plan naprawczy

### Faza 0 — „żeby dało się pracować” (1–2 dni)
Cel: świeży clone buduje się, brak UB, oczywiste bugi poprawione.

1. **B13** Build: usuń `*.sln`, `*.vcxproj`, `*.filters` z `.gitignore` i zacommituj; usuń z vcxproj nieistniejące pliki;
   dodaj include/lib + `GLEW_STATIC` do x64 (lib `GLEW/lib/Release/x64`), `NDEBUG` do Release; zacommituj
   `Dependencies/GLEW/lib` *albo* (lepiej, Faza 1) przejdź na CMake + vcpkg/FetchContent.
2. **B3, B1, B4, B10, B11, B14, B16** — poprawki punktowe (brakujące `return`, warunek, `ImVec4`, shadowing,
   kody błędów, kolejność `DestroyContext`, wielkość liter ścieżek, sprawdzanie `GL_LINK_STATUS`).
3. **B2** zamiana argumentów H/T + przegląd tabeli biomów.
4. **B12** unikalne ID w ImGui (`"Spline editor##levels"`), stan edytorów jako pola klasy zamiast `static`.
5. ~~Włącz `/W4` i `/WX` dla kodu własnego (bez vendor), napraw ostrzeżenia.~~ ✅ 2026-10-28: vendor + GLFW/GLEW jako
   `ExternalIncludePath` z `/external:W0`, `.cpp` z vendor bez ostrzeżeń; 70 ostrzeżeń naprawionych (m.in. `SimplexNoiseClass`
   ma teraz `int width/height` jak reszta klas). Zostają LNK4099/LNK4098 z binarek bibliotek → A8.
6. `imgui.ini` do `.gitignore` (`git rm --cached`), uzupełnij README (build, sterowanie, screenshoty).

**Kryterium ukończenia:** clone → otwarcie `.sln` → build Debug/Release Win32 bez ostrzeżeń w `src/` (poza vendor). x64 — w Fazie 1 (A8).

### Faza 1 — porządki i fundament pod wydajność (1–2 tygodnie)
Cel: kod gotowy na wielowątkowość/GPU, jedna ścieżka renderingu, mierzalna wydajność.

1. ~~**A8 Build system:** CMake (+ `CMakePresets.json`), zależności przez vcpkg manifest lub FetchContent
   (glfw, glew lub glad, glm, imgui, implot, stb). Usunąć binarki z repo. GitHub Actions: build Windows (+ Linux opcjonalnie).~~
   ✅ 2026-10-30: CMake + presety `x64-debug`/`x64-release` (Ninja, MSVC), vcpkg manifest z przypiętym baseline
   (triplet `x64-windows-static`). `.sln`/`.vcxproj`, `Dependencies/` i vendorowane glm/imgui/ImPlot/stb usunięte —
   w repo zostały tylko Simplex (własne przeciążenia) i tk::spline. Win32 → x64: checksumy benchmarku identyczne,
   512² teren 47 → 37 ms. Targety `ptg_core` (generacja bez GL) / exe. LNK4099/LNK4098 zniknęły.
   GitHub Actions: Windows, oba presety, kontrola checksum. Linux odłożony (`std::execution::par` na GCC wymaga TBB).
2. ~~**A2/B9 RAII:** `std::vector<float>` zamiast `new[]` we wszystkich mapach; typ `HeightField { int w, h; std::vector<float> data; }`;
   jedna konwencja `(width, height)` wszędzie (**B5**).~~ ✅ 2026-10-29: `Grid<T>` w `terrainGeneration/Grid.h`
   (`HeightField = Grid<float>`, `BiomeMap = Grid<int>`), indeks `y * width + x`; checksum benchmarku bez zmian.
3. ~~**B8 Szum:** usunąć `reseed` z hot-path.~~ ✅ 2026-10-28: tablica permutacji per instancja `SimplexNoiseClass`
   (vendor dostał przeciążenia `noise(perm, ...)`), wynik bit w bit identyczny. Teren 512²: 3239 → 227 ms (**14,3×**).
   Dalej do rozważenia: **FastNoise2** (SIMD, fBm/ridged/domain warp) — kolejne ~5–10× na CPU.
4. ~~**A3 Rozdzielenie warstw:** `WorldConfig` (czyste struktury), generatory bez ImGui/GL, UI tylko edytuje config.~~
   ✅ 2026-10-29: `terrainGeneration/WorldConfig.h` — `WorldConfig` (rozmiar + `TerrainConfig` + `BiomeConfig`) dla trybu
   TerrainGen, `NoiseMapConfig` (rozmiar + szum + `ErosionConfig`) dla trybu Noise. `SimplexNoiseClass`, `TerrainGenerator`,
   `BiomeGenerator`, `Erosion` dostają `const Config&` i trzymają tylko wyniki; checksum benchmarku bez zmian.
   Zostało na później: definicje biomów nadal w `BiomeGenerator` (przebudowa w A6). Regeneracja sterowana flagami
   żądań (`WorldRequest` / `NoiseRequest`, pkt 7b) — porównanie/wersja configu zamiast flag ustawianych przez UI nadal do rozważenia.
   ~~Jeden `TerrainView` (mesh + shader + tekstury) używany przez oba tryby (**A4**).~~ ✅ 2026-10-29: `opengl/TerrainView`
   (warstwy tekstur TERRAIN/OVERLAY/BIOMES + `TerrainRenderSettings`), subsystemy bez kodu GL, jeden zestaw shaderów
   `TerrainShaders/` (izolinie w FS, `bandWidth = 0` wyłącza). Przy okazji: mapa erozji przesunięta o faktyczną szerokość
   siatki (wcześniej `width + 1` przy siatce `width * 4` — mapy nakładały się). Tryb 2D ignorujący `model` naprawiony w B6.
5. ~~**Shadery:** jeden zestaw z preprocesorem `#include`/`#define` (prosty loader w `Shader.cpp`), hot-reload (F5);
   poprawka **B6** — normalne w world space (krok = rozmiar texela w jednostkach świata), `FragPos = model * p`,
   normal matrix~~ ✅ 2026-10-29 — `#include "plik"` (ścieżka względna, `#line` dla numerów błędów), `Shader::Reload()`
   pod F5 / przyciskiem (przy błędzie zostaje stary program), wspólne `res/shaders/common/transform.glsl`.
   `#define` bez osobnej obsługi (wystarcza natywny preprocesor GLSL). Wspólny UBO kamery/światła przeniesiony do Fazy 4
   (przy 2 shaderach nie zyskuje nic, a komplikuje podmianę kamery w trybie 2D).
6. ~~**B15 Tekstury:** alokacja raz (`glTexStorage2D`), aktualizacja `glTexSubImage2D`; mipmapy tylko tam, gdzie potrzebne.~~
   ✅ 2026-10-30 — mipmapy nigdzie nie są dziś potrzebne (wrócą przy teksturach materiałów w Fazie 3, czytanych w FS).
   Niezmienny storage przygotowuje też grunt pod zapis z compute shaderów (`glBindImageTexture`) w Fazie 2.
7. **Wielowątkowość CPU (przejściowo):** ~~generacja wierszami w `std::for_each(std::execution::par)` lub prosty thread-pool~~
   ✅ 2026-10-30 (7a): `ParallelFor` (`terrainGeneration/Parallel.h`) w `GenerateFractalNoise`, `GenerateTerrain`, `Biomify`;
   `SimplexNoiseClass::Evaluate` (const, bez leniwej permutacji) do wołania z wielu wątków. 6 wątków: 512² szumy 206 → 43 ms,
   teren 211 → 47 ms, biomy 120 → 28 ms (4,2–4,8×), wszystkie checksumy bez zmian. Erozja zostaje sekwencyjna (krople
   modyfikują wspólną mapę po kolei) — do Fazy 5.
   ~~**7b:** generacja asynchroniczna z double-bufferingiem (UI nie czeka, wynik podmieniany po zakończeniu).~~
   ✅ 2026-10-30: wszystkie mapy trybu w jednej strukturze (`GeneratedWorld` / `NoiseMaps`); front należy do wątku głównego,
   zadanie (`GenerationJob<T>`, `std::async`) dostaje kopię frontu i configu, wynik podmienia front. UI tylko ustawia flagi
   (`WorldRequest` / `NoiseRequest`), `Update()` co klatkę instaluje wynik i startuje kolejne zadanie z najnowszym configiem.
   Erozja też w tle. Bez anulowania trwającego zadania: zmiana w trakcie liczenia czeka na jego koniec (opóźnienie ≤ ~2× czas
   generacji, UI nie stoi) — anulowanie co wiersz (`atomic` w `ParallelFor`) do dodania, jeśli będzie przeszkadzać.
   Tryb infinite „nadgania” kamerę o czas generacji (B7 bez zmian — do chunków w Fazie 2).
8. ~~**Pomiar:** `GL_TIME_ELAPSED` queries + panel statystyk (czas generacji, czas klatki, liczba trójkątów).~~
   ✅ 2026-11-03: panel Output pokazuje czas klatki i CPU, czas GPU sceny i UI (`GpuQuery`, bez blokowania potoku, średnia
   ze 120 klatek) oraz trójkąty po teselacji (`GL_PRIMITIVES_GENERATED`). Przełącznik VSync, bo przy włączonym czas klatki
   jest przycięty do odświeżania monitora. Tracy odłożony: dodać, gdy te liczby nie wystarczą do znalezienia wąskiego gardła.

   Punkt odniesienia przed Fazą 2 (Release, VSync wyłączony, tryb TerrainGen, kamera startowa (0, 256, 0) patrząca poziomo,
   RTX 2060, i5-9600K, monitor 2560×1440; pomiar sprzed poprawki układu paneli, widok 3D był wtedy o ok. 150 px wyższy):

   | Mapa | Klatka | Scena GPU | Trójkąty | Generacja |
   |------|--------|-----------|----------|-----------|
   | 512² | 0,83 ms | 0,67 ms | 8 092 012 | 97,6 ms |
   | 1024² | 0,52 ms | 0,38 ms | 4 332 012 | 393,8 ms |

   Większa mapa ma mniej trójkątów, bo siatka ma stałe 50×50 patchy, a poziom teselacji maleje z głębokością w widoku.
   Liczba zależy mocno od kamery (według wzoru z shadera od ok. 0,1 do 12,5 mln na tej samej mapie), więc porównywać tylko przy tej samej pozycji i kierunku.
   Wąskim gardłem jest generacja: 1024² liczy się dłużej niż ok. 750 klatek renderowania.
9. ~~**Testy:** doctest/Catch2 — determinizm szumu (ten sam seed → te same wartości), zakresy [-1,1],
   wybór biomu dla zadanych parametrów, regresja height = f(x,z).~~ ✅ 2026-11-02: doctest, target `ptg_tests`
   (`ProceduralTerrainGenerator/tests/`), `ctest --preset <preset>` także w CI (Debug i Release). Testują właściwości, nie
   konkretne wartości, więc nie zależą od kompilatora (w przeciwieństwie do checksum). Znalazły B19, B20 i B21 (wszystkie naprawione). Kolejne znane błędy oznaczać `doctest::should_fail()`:
   po naprawie błędu test zgłosi niepowodzenie, dopóki oznaczenie nie zostanie usunięte.
10. ~~**Serializacja (A7):** presety JSON (nlohmann/json) dla `WorldConfig`; load/save w UI; eksport heightmapy do PNG 16-bit / RAW.~~
    ✅ 2026-11-03 presety: `terrainGeneration/Preset.*` (`WorldConfig` i `NoiseMapConfig`, testowane bez okna) + sekcja „Presets”
    w UI obu trybów (`presets/world`, `presets/noise`, poza gitem). Enumy po nazwie, wersja formatu, brakujące pola = wartości domyślne.
    Loader odrzuca wartości, których generacja nie obsłuży, a błąd zostawia aktualny świat. Bez ustawień renderowania
    (`heightScale`, tryb wyświetlania) i definicji biomów (do A6).
    ✅ 2026-11-03 eksport: `terrainGeneration/HeightmapExport.*` + sekcja „Saving” w UI, PNG 16-bit (lodepng) i RAW R16 (little-endian,
    rozmiar w nazwie pliku) do `exports/`, poza gitem. Stały zakres: TerrainGen [0, 1], Noise [-1, 1], wartości spoza zakresu przycinane
    i zgłaszane w UI. Mapa po erozji, gdy jest widoczna, trafia do osobnego pliku `_eroded`. EXR odłożony: 16 bitów wystarcza,
    dodać przy potrzebie dokładnych wartości `float` (np. obróbka w Houdini). Zapis idzie w wątku głównym (PNG 1024² ok. 0,1 s).
11. ~~**A9/A10:** usuń martwy kod, posprzątaj branche (zostaw `master` + aktywny feature branch).~~ ✅ 2026-10-30

**Kryterium ukończenia:** mapa 1024² regeneruje się po ruchu suwaka bez blokowania UI; wszystkie testy zielone w CI.

---

## 4. Plan rozwojowy

### Faza 2 — generacja na GPU + chunki/LOD (główny skok wydajności, 2–4 tygodnie) — ✅ zamknięta 2026-11-08
Cel: **edycja w czasie rzeczywistym przy 60+ FPS** i nieskończony świat.

1. **Compute shader height pass:** port szumu (fBm, ridged, domain warping) i ewaluacji splajnów do GLSL
   (splajny jako 1D LUT-tekstury `R32F`, 256–1024 próbek — przeliczane na CPU przy edycji punktów).
   Wyjście: `R32F` heightmap per chunk. Mapa 2048² w < 2 ms na średnim GPU → suwaki działają „na żywo”.

   ✅ 2026-11-04: `opengl/GpuHeightGenerator.*`, `res/shaders/ComputeShaders/`, `common/noise.glsl`, `common/simplex.glsl`. **Aplikacja generuje wyłącznie na GPU** (decyzja właściciela);
   generatory CPU zostają w `ptg_core` tylko dla `--benchmark`, `--gpu-check` i testów. Erozja nadal na CPU, na mapie odczytanej z tekstury, do Fazy 5.
   Dzięki temu poziomy biomów działają na żywo (bez przycisku „Apply biome levels”), a tryb infinite nie nadgania już kamery (B7: nadal cała mapa przy każdym ruchu, biomy i tak z originem 0,0).
   Obejmuje wszystkie opcje szumu (także szum 4D `symmetrical`, wyspy, ridge) i wszystkie metody łączenia. Splajny to tablice 1024 próbek. Domain warping nie istnieje też na CPU, więc nie ma czego portować.
   `ProceduralTerrainGenerator.exe --gpu-check` porównuje GPU z CPU na 29 konfiguracjach (w tym biomy) i mierzy czas; wymaga GPU, więc uruchamiany ręcznie, nie w CI.

   Teren, domyślny config (LINEAR_COMBINE), RTX 2060 i i5-9600K, Release, ukryte okno, mediana z `glFinish`:

   | Mapa | CPU (6 wątków) | GPU |
   |------|----------------|-----|
   | 512² | 41 ms | 0,38 ms |
   | 1024² | 169 ms | 1,23 ms |
   | 2048² | 736 ms | 5,26 ms |
   | 4096² | 2702 ms | 20,3 ms |

   Założenie „2048² w < 2 ms” nie jest spełnione na tej karcie (5,26 ms), ale 2048² nadal mieści się w jednej klatce przy 60 FPS.
   Mapy nie są identyczne bit w bit. Dla typowych ustawień różnica to ok. 1e-5. Większe różnice sprawdzone na CPU, przesuwając współrzędną o najmniejszy krok `float`: 8 oktaw
   przy lacunarity 3,1 daje 2,4e-3 na samym CPU (2,35e-3 między GPU i CPU), a szum 4D ma nieciągłości na granicach sympleksów, ok. 3,8e-4 (promień narożnika 0,6 zamiast 0,5).
   Splajn liniowy w tablicy: do 1,4e-4 na zagięciach. Różnice w zachowaniu: dla szumu spoza [-1, 1] tablica bierze wartość krańcową, a `tk::spline` ekstrapoluje; GPU nie zgłasza błędu przy szumie poniżej -1.
2. **Normal/derived pass:** compute liczący normal map + slope + curvature (potrzebne do teksturowania i roślinności).

   ✅ 2026-11-08: `opengl/GpuSurfacePass.*`, `ComputeShaders/surface_compute.shader`, `common/surface.glsl`; wersja CPU `terrainGeneration/SurfaceDerivatives.*` (testy, `--gpu-check`).
   Mapa `RGBA32F` trzyma gradient (dx, dy) i laplasjan **surowej** wysokości na jednostkę świata, a nie gotową normalną. Normalna, nachylenie i krzywizna to te wartości razy `heightScale`
   (funkcje w `surface.glsl` / `surface::Normal`, `Slope`, `Curvature`), więc zmiana skali pionowej nie wymaga przeliczenia mapy. `TerrainView` liczy mapę osobno dla TERRAIN i OVERLAY,
   w `Draw`, tylko po zmianie warstwy (`LayerTexture` / `SetLayer` oznaczają ją jako nieaktualną). TES bierze normalną z tej mapy (1 odczyt zamiast 4); nowe tryby wyświetlania SLOPE i CURVATURE.
   Na krawędziach mapy gradient jest jednostronny (wcześniej TES zaniżał go o połowę przez `CLAMP_TO_EDGE`). GPU i CPU dają te same wartości (0 ulp w `--gpu-check`).
   Czas (RTX 2060): 512² 0,11 ms, 1024² 0,16 ms, 2048² 0,54 ms, 4096² 2,09 ms.
3. **ChunkManager:** siatka chunków (np. 256×256 próbek, 1 próbka = 1 m), współrzędne świata, generacja tylko nowych chunków
   w kolejce priorytetowej (odległość od kamery + frustum), cache LRU, budżet ms/klatkę.
   Usuwa „kotwiczenie” kamery i regenerację całej mapy (**B7**, **A5**). Do rozważenia: floating origin dla dużych współrzędnych.

   ✅ 2026-11-08 (bez frustum i budżetu w ms): `opengl/ChunkWorld.*`. „Infinite generation” w trybach TerrainGen i Noise heightmap to teraz świat z chunków (decyzja właściciela);
   „Partial generation” zostaje skończoną mapą do edycji, eksportu i erozji (te dwa są ukryte w trybie infinite). Kamera porusza się swobodnie, kotwiczenie usunięte.
   Chunk = 128×128 tekseli (512 jednostek świata), tekstury 131² z marginesem 1 teksela, więc normalne są ciągłe, a sąsiednie chunki dzielą krawędź.
   Świat jest ustawiony tak, że pokrywa skończoną mapę w tym samym miejscu (`AlignWithMap`), a wyspa wypełnia obszar o rozmiarze mapy (nowy parametr `islandSize` generatora).
   Kolejność: odległość od kamery; najwyżej 16 chunków na klatkę (liczba, nie ms). Zmiana parametru unieważnia wszystkie chunki, stara zawartość jest widoczna do czasu podmiany.
   Cache: chunki poza zasięgiem zostają, dopóki jest ich więcej niż połowa liczby chunków w zasięgu (+16), najdłużej niewidziane idą pierwsze, a ich tekstury są używane ponownie.
   Biomy pod kamerą: odczyt identyfikatorów chunka z GPU tylko po wejściu do niego lub jego regeneracji. Zasięg = „Render distance” kamery.
   `--gpu-check` sprawdza, że chunk (origin + `islandSize`) jest bit w bit równy fragmentowi pełnej mapy (szum z wyspą, teren, biomy).
   Pamięć: ok. 0,7 MB na chunk z biomami (0,35 MB bez). Przy render distance 5000 to ok. 300 chunków w zasięgu, więc bez LOD (pkt 4) duży zasięg kosztuje setki MB.
   Zostało: budżet w ms, floating origin (priorytet chunków w polu widzenia zrobiony w pkt 5).
   Przy okazji (prośba właściciela): słońce zamiast światła punktowego (`LightSource`: azymut i wysokość), proceduralne niebo (`Sky`, `common/sky.glsl`)
   z mgłą terenu w kolorze nieba przy granicy zasięgu (ukrywa też doładowywanie chunków) oraz przeniesienie kamery nad skończoną mapę po wyłączeniu trybu infinite.
4. **LOD:** quadtree chunków (lub geometry clipmaps) + teselacja w TCS oparta o rozmiar krawędzi w pikselach ekranu
   (zamiast stałych `MIN/MAX_DISTANCE`); morfowanie/skirts przeciw szczelinom między poziomami.

   ✅ 2026-11-08 (bez teselacji wg pikseli ekranu i bez morfowania): quadtree chunków w `ChunkWorld`. Chunk poziomu L obejmuje 128·2^L tekseli tymi samymi teksturami 131²,
   czyli bierze co 2^L-tą próbkę (`MapArea::step` w generatorze GPU). Próbki leżą na pozycjach całkowitych, więc tam, gdzie poziomy się stykają, są równe;
   `--gpu-check` sprawdza kroki 1, 2 i 4 bit w bit względem pełnej mapy. Chunk bliżej kamery niż „Detail distance” × jego rozmiar (domyślnie 3) jest zastępowany przez czworo
   dzieci, ale dopiero gdy wszystkie są wygenerowane, więc w widoku nie ma dziur. Najpierw generowane są chunki, które zostawiłyby dziurę, potem najbliższe (rodzic przed dziećmi),
   więc po zmianie parametru teren pojawia się od zgrubnego do szczegółowego. Korzeń = najmniejszy poziom większy od zasięgu (najwyżej 10).
   Szczeliny między poziomami zakrywają skirts: patche wzdłuż brzegów chunka, które TES opuszcza o `skirtDepth` (1% `heightScale` na poziomie 0, podwajane z poziomem, maks. 50%).
   Siatka jest jedna (poziom 0), skalowana w XZ uniformem `meshScale` w VS, więc `model` i `normalMatrix` zostają jednorodne; mapa pochodnych liczona z krokiem
   `worldUnitsPerTexel · 2^L`, więc normalne, nachylenie i krzywizna są w jednostkach świata. TES nie bierze już normalnej z patcha (skirt jest pionowy), tylko dodaje wysokość do y.
   „Render distance” do 50000 (suwak logarytmiczny); bliska płaszczyzna odsuwa się do 1/20000 zasięgu, żeby nie brakło precyzji głębi.
   Odległość do chunka liczona w XZ, bez wysokości kamery. Zostało: teselacja wg rozmiaru na ekranie, morfowanie między poziomami (przejścia są skokowe), uwzględnienie wysokości kamery.
5. **Culling:** frustum culling chunków na CPU + odrzucanie patchy w TCS (`gl_TessLevelOuter = 0`), opcjonalnie
   `glMultiDrawArraysIndirect` dla wszystkich chunków jednym wywołaniem.

   ✅ 2026-11-08 (bez `glMultiDrawArraysIndirect`): `terrainGeneration/Frustum.h` (6 płaszczyzn z `projection * view * model`, test AABB, testy w `tests/FrustumTests.cpp`).
   `ChunkWorld::Plan` dostaje `View` (`ViewFrom(camera, settings)`) i rysuje tylko chunki w polu widzenia; pudełko chunka obejmuje każdą możliwą wysokość
   (±1,5 · `heightScale`, niżej jeszcze skirt), bo wysokości chunka CPU nie zna. Kolejność generacji: dziura w widoku, dziura poza widokiem, chunk w widoku, poza widokiem,
   a w każdej grupie najbliższe — zamyka „priorytet frustum” z pkt 3. Chunki poza widokiem nadal są generowane i trzymane w cache, więc obrót kamery ich nie odsłania.
   TCS ustawia poziomy teselacji na 0 dla patchy, których pudełko (te same granice wysokości) leży w całości po złej stronie jednej płaszczyzny; działa też dla skończonej mapy i w widoku 2D.
   W widoku 2D chunki nie są przycinane na CPU (pokazywany jest cały zasięg). Output: „drawn of in range”. Liczba trójkątów w panelu nadal jest liczona przed przycinaniem.
6. **Inkrementalna regeneracja:** dirty-flagi per warstwa (zmiana szumu temperatury nie przelicza wysokości),
   podgląd w niskiej rozdzielczości podczas przeciągania suwaka, pełna jakość po puszczeniu.
   Kandydat do zrobienia wcześniej, jeszcze na CPU: `TerrainGenerator::GenerateTerrain` liczy trzy szumy od nowa dla każdego
   piksela (`Evaluate`), choć te same wartości leżą już w mapach `continentalnessNoise` / `mountainousnessNoise` /
   `weirdnessNoise` (dla originu 0,0). Gdyby teren czytał gotowe mapy, zmiana samego splajnu lub metody łączenia
   przeliczałaby się prawie natychmiast (bez szumu), a zmiana jednego szumu kosztowałaby 1 szum zamiast 3. Wymaga pilnowania,
   czy mapy szumów są aktualne względem configu (wersja/dirty-flaga per szum) i osobnej ścieżki dla originu ≠ 0 (tryb infinite).
7. **CPU fallback:** ten sam algorytm w C++ (FastNoise2) dla zapytań `GetHeightAt(x,z)`, fizyki, eksportu i testów
   porównawczych GPU vs CPU (tolerancja ~1e-4).

**Kryterium:** przelot kamerą 200 m/s bez dziur i przycięć; zmiana dowolnego parametru szumu widoczna w < 50 ms
na obszarze widoku; stabilne 60 FPS przy dystansie widzenia 4+ km.

Pomiar (2026-11-08): przy 1 tekselu = 1 m i skali modelu 1 jest 200 m/s = 800 jednostek/s („Camera speed”), a 4 km = 16000 jednostek („Render distance”).
Output → „Record flight”: prędkość (średnia i najwyższa w 1 s), FPS, czas klatki (99% i najgorszy), klatki > 20 ms (`FlightRecord::slowFrameMs`) i klatki z dziurą w widoku
(chunk w widoku bez własnych map i bez gotowego zastępstwa; teren pod spodem jest wtedy niebem). W trybie infinite Output pokazuje też czas od zmiany parametru do klatki,
w której każdy rysowany chunk jest aktualny, i do pełnej szczegółowości (`ChunkWorld::ImGuiUpdateTimes`).

Wynik przelotu właściciela (TerrainGen z biomami, render distance 4 km, 31 s, 7545 klatek): 762 j/s (≈190 m/s), średnio ≈243 FPS, najgorsza klatka 13,86 ms,
0 klatek > 20 ms; rysowane 99 z 440 chunków w zasięgu, generacja 1,4 ms GPU na klatkę; zmiana parametru widoczna po 30 ms, pełna szczegółowość też po 30 ms.
Jedyne odstępstwo: 4 klatki z dziurą — chunki wchodzące w zasięg na jego granicy przed wygenerowaniem. Poprawka: `ChunkWorld` planuje chunki do 1,15 × zasięgu
(`prefetchFactor`), rysuje i liczy dziury tylko w zasięgu. Ponowny przelot po poprawce: 0 klatek z dziurą.

✅ **Faza 2 zamknięta 2026-11-08** — kryterium spełnione. Pkt 6 (inkrementalna regeneracja) i 7 (CPU fallback) nie były potrzebne do kryterium:
generacja na GPU mieści się w 30 ms od zmiany do pełnego widoku, a generatory CPU już istnieją (benchmark, `--gpu-check`, testy). Zostają jako możliwe usprawnienia,
podobnie jak z pkt 4: teselacja wg rozmiaru na ekranie, morfowanie między poziomami LOD, wysokość kamery w wyborze poziomu; z pkt 3: budżet w ms, floating origin.

### Teren 2.0 — przed Fazą 3 (decyzja właściciela 2026-11-08)
Biomy (Faza 3, pkt 3–4) opierają się na wysokości i znaczeniu szumów C/M/W, więc kształt terenu poprawiamy przed nimi.

0. ✅ 2026-11-08 **Usunięcie generacji na CPU** (decyzja właściciela): każda nowa operacja wymagałaby wersji C++ i GLSL. Usunięte `SimplexNoiseClass`,
   `TerrainGenerator`, `BiomeGenerator`, `SurfaceDerivatives`, `Parallel.h`, tryb `--benchmark` i checksumy w CI oraz ich testy (ostatnia wersja z nimi: `56eae39`).
   Zostały configi, tablica permutacji (`makePermutation`) i `tk::spline` do tablicy splajnu dla GPU, tabela biomów i jej testy. Pkt 7 Fazy 2 (CPU fallback) odpada.
   `--gpu-check` sprawdza teraz same wyniki GPU: zakresy i skończoność dla każdej opcji, determinizm, biomy, chunki/LOD bit w bit, pochodne na mapach analitycznych.
   Strata: CI nie sprawdza generacji (brak GPU na runnerach); ewentualnie później Mesa llvmpipe (programowy GL 4.5 z compute shaderami).
1. ✅ 2026-11-08 **Operacje na szumie** (nowe pola `NoiseConfigParameters`, domyślnie wyłączone): domain warping (`warpStrength`, `warpScale`; przesunięcie
   współrzędnych 4-oktawowym szumem z tej samej tablicy permutacji), fBm tłumiony gradientem (`slopeErosion`; analityczne pochodne `simplex2Grad`, oktawa dzielona
   przez 1 + k·|∇ niższych oktaw|²), tarasy (`terraceSteps`, `terraceSharpness`; na końcu, po redystrybucji). Przy wyłączonych warpie i tłumieniu działa stara pętla,
   więc istniejące światy są identyczne bit w bit (sprawdza `--gpu-check`, sekcja `shape:`). Warp i tłumienie nie działają z `symmetrical`.
2. ✅ 2026-11-08 **Metoda warstwowa** (`EvaluationMethod::LAYERED`, zastąpiła nieużywane `C`): `h = podstawa · (1 − mountainShare) + ląd · amplituda · kształt · mountainShare`,
   podstawa = splajn C, amplituda = splajn M, kształt = nowy `peaksSpline` po W, ląd = `smoothstep(seaLevel, seaLevel + coastWidth, podstawa)`. Fałd PV
   `1 − |3|W| − 2|` to dokładnie Ridge szumu weirdness z gain 3 i offset 1 (domyślny), więc shader go nie powtarza. Wynik zostaje w [0, 1], bez zmian
   `terrainRange` i `heightBound`. `seaLevel` jest w jednostkach splajnu podstawy; powierzchnia wody leży na wysokości `seaLevel · (1 − mountainShare)` (pod wodę w Fazie 5 i biomy w Fazie 3).
   Edytor splajnów: podpisy osi i przycisk „Peaks & valleys” zależne od metody (`TerrainConfig::MethodSpline`). Doliny rzeczne (odejmowanie wysokości) — nie, do pkt 3 lub Fazy 5.
3. ➡️ Przeniesione do Fazy 5 (decyzja właściciela 2026-11-08): zagnieżdżone splajny — razem z rzekami, które i tak wymagają obniżania terenu.
4. ✅ 2026-11-08 **Edytory TerrainGen** (poza planem, na prośbę właściciela):
   - Edycja szumu składowego na żywo: widok zawsze pokazuje teren wynikowy, a edytowany szum widać jako mapę 2D w panelu (`NoisePreview`, ≤ 512 tekseli,
     do 220 px wysokości); edytory szumu, splajnów i biomów otwarte jednocześnie (osobny wybrany składnik dla szumu i splajnu). „Sampling resolution” na żywo.
     Panele boczne mają stały pasek przewijania — inaczej treść o szerokości panelu (podgląd) migotała.
   - Edytor splajnów: zmiany na żywo w configu (bez „Set new spline points”), wykres pokazuje krzywą z `SampleSpline` (tą samą co GPU), dodawanie punktu
     dwuklikiem, usuwanie prawym, checkbox „Smooth (cubic)”; przy metodzie Linear przyciski zmiany metody. Domyślna metoda terenu: LAYERED.
   - Splajn kubiczny = monotoniczny PCHIP (Fritsch–Carlson, `SplineSampling.cpp`): nie wychodzi poza wartości sąsiednich punktów, więc wynik zostaje w [0, 1]
     (dawny naturalny splajn przestrzeliwał: odwrócone garby w metodzie Spline, doły w Layered). tk::spline usunięty z `vendor/`.
   - Nie zrobione: znacznik wartości szumu pod kamerą na wykresie splajnu (wymaga odczytu z GPU).

### Faza 3 — biomy 2.0 (2–3 tygodnie)
Cel: realistyczne, płynne przejścia i biomy wpływające na kształt terenu.

**Kolejność ustalona z właścicielem 2026-11-08:** pkt 1 + 2 (zrobione) → pkt 6 edytor biomów (zrobione 2026-11-09) → pkt 4 klimat zależny od wysokości (zrobione 2026-11-09) →
pkt 3 biomy modyfikują teren (zrobione 2026-11-09) → pkt 5 teksturowanie (zrobione 2026-11-10). Po każdym kroku nowe przypadki w `--gpu-check`.
Pkt 6 w szczegółach: lista biomów (dodaj/usuń, nazwa, kolor, 5 zakresów suwakami), diagram Whittakera (temperatura × wilgotność, prostokąty biomów w ImPlot),
podgląd mapy biomów 2D; tabela nie może być pusta (ostatniego biomu nie da się usunąć); id = indeks, więc usunięcie przesuwa id kolejnych biomów.
Pkt 4 wymaga, żeby shader biomów liczył też wysokość terenu (splajny w buforze jak w `height_compute.shader`).

1. ✅ 2026-11-08 **Ciągła przestrzeń parametrów** zamiast dyskretnych poziomów: biom = zakresy wartości 5 szumów (C, M, W, T, H) w `BiomeConfig::table`,
   wybór = najbliższy biom (odległość = długość wyjścia poza zakresy, remis → pierwszy w tabeli), jak multi-noise Minecraft 1.18. Brak dziur w pokryciu
   i biomu „zastępczego”; id = indeks w tabeli. Domyślne biomy przeliczone z dawnych poziomów. Tabela i `blendWidth` w presetach (wersja 2, decyzja właściciela:
   format można było zmienić, starych presetów nie ma). Edytor poziomów usunięty.
2. ✅ 2026-11-08 (kolory) **Blending:** kolor miesza wszystkie biomy z wagą `1 − smoothstep((d − d_min) / blendWidth)` — ciągłe także wtedy, gdy biomy zamieniają się
   kolejnością. Mapa id zostaje ostra (nazwa biomu pod kamerą). Czas generacji biomów bez zmian. Tekstury K najbliższych id + wag dojdą razem z pkt 3 / 5,
   kiedy będą potrzebne (modyfikatory terenu, splat mapping).
3. ✅ 2026-11-09 **Biomy modyfikują teren:** `biome::TerrainModifier` w każdym biomie (amplitude, offset, detailAmplitude, detailSize, detailRidge),
   `biome::ModifyHeight` (CPU, testy) = `modifyHeight` w `common/biomes.glsl`. Pętlę teren ↔ biom przerywa wysokość bazowa: h0 z C/M/W → klimat(h0) → wagi biomów →
   h = clamp(water + (h0 − water) · Σw·amplitude + Σw·offset + Σw·detailAmplitude · detail, 0, 1) (wagi znormalizowane). Biomy i kolory dalej z h0.
   Wagi = `BlendWeight` z osobnym `BiomeConfig::terrainBlendWidth` (> 0, domyślnie 0,15), więc przy ostrych kolorach teren i tak przechodzi płynnie.
   Detail noise: 3 oktawy simplex, długość fali `detailSize` tekseli, ridged = 1 − 2|n|, jedna tablica permutacji (`BiomeConfig::detailSeed`).
   Teren z modyfikatorami liczy wszystkie 5 szumów i tabelę biomów w `height_compute.shader` (osobno od `biome_compute.shader`, ~1,5× czasu terenu);
   oba shadery dzielą `common/world_input.glsl` (jeden bufor wejściowy) i `common/biomes.glsl`. Działa tylko przy włączonych biomach i `modifyTerrain`;
   zmiana biomu przelicza wtedy też teren. Domyślne: równiny 0,8, śnieg 0,9, wydmy na pustyni (0,012 / 10 tekseli, ridged), góry 1,15 + skała (0,015 / 20, ridged).
   UI: sekcja „Terrain modifiers” (włącznik, blend width, seed) i „Terrain modifier” zaznaczonego biomu w edytorze. Tarasowanie per biom odłożone.
   `--gpu-check`: sekcja `biome modifiers:` (neutralne, amplitude 0, offset, wyłączone, blending zmniejsza skoki, domyślna tabela) + chunk z modyfikatorami bit w bit.
   **Do zrobienia później:** teren i biomy w jednym przebiegu (oba liczą to samo), wtedy łączny koszt jak dziś same biomy.
4. ✅ 2026-11-09 **Wpływ wysokości na klimat:** `biome::Climate` w `BiomeConfig::climate` (presety: `biomes.climate`), `biome::ApplyClimate` (CPU, testy) =
   `applyClimate` w `biome_compute.shader`. above = max(h − `TerrainConfig::WaterHeight()`, 0) (LAYERED: seaLevel · (1 − mountainShare), inne metody: 0);
   T −= lapseRate · above, H += coastHumidity · (1 − smoothstep(0, coastRange, above)), oba przycięte do [-1, 1]. Domyślnie 1,0 / 0,3 / 0,05.
   Wysokość liczy shader biomów sam, tą samą funkcją co teren (`common/terrain.glsl`, splajny w `BiomeInput`), bez czytania tekstury wysokości —
   C/M/W i tak liczy, więc koszt to tylko splajny. Dlatego biomy zależą od całego `TerrainConfig`: `RequestTerrain` przy włączonych biomach przelicza też biomy.
   Wilgotność przy wybrzeżu liczona z wysokości nad wodą, nie z continentalness (w LAYERED podstawa terenu = splajn C, więc wychodzi na to samo).
   Nowy domyślny biom „Snowy peaks” (C ≥ 0,3, M ≥ 0,05, T ≤ -0,5) przed „Mountain” (teraz T ≥ -0,5). UI: sekcja „Climate” w ustawieniach biomów.
   `--gpu-check`: sekcja `climate:` (skrajny lapse rate → nad wodą tylko biomy z T = -1, skrajna wilgotność → przy brzegu tylko biomy z H = 1, klimat zmienia biomy).
5. ✅ 2026-11-10 **Teksturowanie** (PR #38): nowy tryb wyświetlania `TEXTURED` (6, `MODE_TEXTURED` w `Terrain_fragment.shader`).
   - **Materiały:** lista w `res/textures/terrain_materials.json` (kolejność = id materiału w shaderach, limit 16): nazwa, asset, `tileSize` (jednostki świata
     na powtórzenie), `fallbackColor`, `cliffMaterial` (materiał na stromych zboczach). Domyślnie 11: trawa, sucha trawa, ściółka, ziemia, piasek, plaża,
     skała, piaskowiec, piarg, śnieg, tundra. Tekstury (CC0, Poly Haven / ambientCG) nie są w gicie: pobiera je `tools/fetch_textures.py [--resolution 1k|2k|4k]`
     do `res/textures/terrain/<asset>/`. Materiał bez tekstur rysuje się kolorem `fallbackColor`, więc aplikacja działa i bez nich.
   - **`TerrainMaterials`** (`opengl/`): wczytuje listę i tekstury (dekodowanie równoległe), pakuje je do dwóch `GL_TEXTURE_2D_ARRAY` (jednostka 4: albedo + wysokość,
     5: normalna XY + roughness + AO) i parametrów w bloku `std140` (binding 0, `common/materials.glsl`). `Bind` raz na klatkę w `TerrainGenApp::Draw`.
     Sekcja UI „Terrain materials”: materiał bez biomów, skala tekstur, siła normal map, progi klifów i triplanaru, height blending, specular, anti-tiling.
     `TextureClass` obsługuje teraz tablice (`_layers`).
   - **Reguły per biom:** każdy biom ma do 4 warstw materiałów (`biome::MaterialLayer`: materiał + waga, normalizowane). `biome::MixMaterials` (CPU, testy) =
     `mixMaterials` w `common/biomes.glsl`: materiały biomów mieszane wagą `BlendWeight` z `BiomeConfig::materialBlendWidth`, potem szum plam
     (score = c·(log w + k·patch), softmax; przy `materialPatchiness` = 1 materiał zajmuje ok. swój udział powierzchni, `materialPatchContrast` wyostrza brzegi,
     `materialPatchSize` = długość fali w tekselach, seed = `detailSeed`). Wynik to splat map (`GL_RGBA8`, tablica 4 warstw po 4 materiały), którą liczy
     `biome_compute.shader` przy `GenerateBiomes(..., splat)`; mapa skończona trzyma ją w `TerrainView::SplatTexture` (jednostka 6), chunki w `Chunk::splat`.
     UI: sekcja „Surface materials” w ustawieniach biomów i warstwy materiałów zaznaczonego biomu w edytorze. Presety: pola `material*` w `biomes`
     i `materials` w każdym biomie (bez podbicia wersji: starsze pliki dostają wartości domyślne).
   - **Shader fragmentów:** wagi ze splat mapy (bez biomów: jeden materiał), na zboczach między `cliffSlopeStart` a `cliffSlopeEnd` waga przechodzi na `cliffMaterial`,
     najsilniejsze materiały mieszane height-based (score = waga + `heightInfluence` · wysokość tekstury, widoczne te w `blendDepth` od najlepszego).
     Triplanar między `triplanarStart` a `triplanarEnd`, poniżej tylko rzut z góry. Normal mapy, AO, roughness + specular słońca.
     Anti-tiling: druga skala UV mieszana z odległością, średni kolor materiału w dali, wahania jasności/odcienia szumem o niskiej częstotliwości.
     Tekstury przyklejone do terenu przez `TerrainPos` (pozycja przed `modelScale`, z TES).
   - `--gpu-check`: sekcja `materials:` (wagi sumują się do 1, tylko materiały z tabeli, jeden materiał wszędzie, plamy zmieniają mieszankę, blending zmniejsza skoki)
     i `chunk: material splat` (chunk = fragment pełnej mapy bit w bit).
   - **Nie zrobione z planu:** osobne detail normal maps (na bliską odległość) i reguły materiałów zależne od wysokości (wysokość działa pośrednio, przez klimat i biomy).
6. ✅ 2026-11-09 **Edytor biomów w UI:** `utility/BiomeEditor.*`, sekcja „Biome table” w „Biome generation settings” (szumy T/H w zwijanej sekcji obok).
   Lista biomów (kolor + nazwa), „Add copy” (kopia pod zaznaczonym — nic nie zmienia, bo oryginał wygrywa remisy), „Remove” (ostatniego nie da się usunąć),
   strzałki kolejności (remis → wyższy na liście), nazwa, kolor i 5 zakresów (`DragFloatRange2`, [-1, 1], min ≤ max). Diagram Whittakera (ImPlot, T × H):
   prostokąty biomów, klik zaznacza najmniejszy pod kursorem, zaznaczony przeciągany `DragRect` (krawędzie, rogi, całość zatrzymuje się na brzegu).
   Każda zmiana poza nazwą od razu przelicza biomy. Limit `biome::maxBiomes` = 64 wspólny z presetami; zapis do JSON przez presety (było).
   Podgląd mapy biomów 2D w panelu pominięty (decyzja właściciela): widok główny „Biomes” + „2D map” pokazuje to samo.
   **Odłożone** (decyzja właściciela, wrócić przy rozbudowanej liście biomów z sensownymi wartościami):
   - Diagram to tylko rzut na T × H, więc biomy niezależne od klimatu (Ocean, Sand, Mountain — pełne T i H) zakrywają cały wykres. Pomysł: wybór osi
     (2 z 5 parametrów) i przekrój — suwaki wartości pozostałych 3 parametrów, na wykresie tylko biomy obejmujące ten punkt (opcja „pokaż wszystkie”).
   - Nakładanie się biomów w 5D (wspólny obszar → wygrywa wyższy na liście): raczej ostrzegać niż blokować (blokada psuje „Add copy”, przeciąganie
     zacinałoby się na niewidocznej osi, nakładanie bywa celowe). Pomysł: „Overlaps with: …” przy zaznaczonym, ⚠ na liście, opcjonalnie „Resolve” (przycięcie).

**Kryterium:** brak ostrych granic biomów; zmiana definicji biomu widoczna natychmiast; konfiguracja biomów ładowana z pliku.

✅ **Faza 3 zamknięta 2026-11-12** — kryterium spełnione: kolory, teren i materiały przechodzą płynnie (`blendWidth`, `terrainBlendWidth`, `materialBlendWidth`;
`--gpu-check` sprawdza, że blending zmniejsza skoki), każda zmiana w edytorze od razu przelicza biomy, a cała `BiomeConfig` z tabelą jest w presetach JSON.
Zostają jako możliwe usprawnienia: teren i biomy w jednym przebiegu compute (pkt 3), detail normal maps (pkt 5), wybór osi / przekrój diagramu i ostrzeżenia
o nakładaniu się biomów (pkt 6), tarasowanie per biom.

### Faza 4 — roślinność i obiekty (2–4 tygodnie)
Poprzednia implementacja (usunięta w A9, do wglądu w historii gita: `fb8b840` — ostatnia działająca wersja,
`b7f7720` „Fixed trees”): Poisson-disk per chunk (vendor `PoissonGenerator.h`) z gęstością z biomu
(`Biome::vegetationLevel`) i odrzucaniem pod poziomem morza, typy drzew per biom z prawdopodobieństwem (edytor ImGui),
model `.obj` (tiny_obj_loader) rysowany `glDrawElementsInstanced`. Opierała się na siatce liczonej na CPU — nie do
przeniesienia wprost, ale reguły rozmieszczenia i UI są dobrym punktem wyjścia.

1. **Rozmieszczenie:** Poisson-disk / blue-noise per chunk z seedem z `hash(chunkX, chunkZ, worldSeed)` — deterministyczne
   i bez szwów (uwzględnij sąsiadów przy krawędziach). Najlepiej na GPU (compute: jittered grid + odrzucanie),
   do bufora instancji (pozycja, rotacja, skala, typ).
2. **Reguły:** gęstość i typy z biomu (wagi z Fazy 3) × maska slope × wysokość × wilgotność × szum klastrujący (lasy/polany);
   minimalne odległości per gatunek; brak drzew pod wodą/na skałach.
3. **Rendering:** instancing + `glDrawElementsIndirect`, frustum + distance culling w compute,
   LOD: mesh → uproszczony mesh → impostor (billboard/octahedral) → brak; trawa jako osobny system (geometry/compute, wiatr w VS).
4. **Assety:** loader glTF (cgltf) zamiast/obok tiny_obj_loader; kilka prostych modeli drzew/krzewów/skał.
5. **UI:** per-biom tabela roślinności (gatunek, gęstość, zakres slope/height), licznik instancji, suwak dystansu.
6. **Wspólny UBO kamery i światła** (przeniesiony z Fazy 1 pt 5): gdy dojdą shadery roślinności/wody, `view`/`projection`/
   pozycja kamery/światło w jednym `std140` bloku ustawianym raz na klatkę; tryb 2D jako osobny bufor albo podmiana bloku.

**Kryterium:** 100k+ instancji drzew i 1M+ źdźbeł trawy przy 60 FPS; roślinność zgodna z biomami i spójna między chunkami.

### Faza 5 — realizm i narzędzia (otwarte)
- **Erozja na GPU:** hydrauliczna (pipe model / virtual pipes lub kroplowa w compute z atomics) + termiczna;
  tryb „bake” dla obszaru (erozja jest nielokalna — dla chunków: erozja na większym regionie z marginesem i zszywanie).
- **Rzeki i jeziora:** flow accumulation / priority-flood na mapie niskiej rozdzielczości, rzeźbienie koryt, maska wilgotności dla biomów.
- **Zagnieżdżone splajny** (z „Teren 2.0”, pkt 3): punkty splajnu C są splajnami po M (i dalej po PV), wypiekane do tekstury LUT 2D/3D próbkowanej jednym odczytem.
  Daje klify tam, gdzie góry dochodzą do morza, płaskie plaże gdzie indziej, doliny wcięte poniżej podstawy. Najwięcej pracy: edytor UI (lista punktów C z wykresem po M, podgląd C×M jako mapa ciepła).
- **Woda:** płaszczyzna oceanu z refleksją/refrakcją, głębia, piana przy brzegu.
- **Atmosfera:** fog zależny od wysokości, skybox/atmospheric scattering, cykl dnia, cienie (cascaded shadow maps).
- **Eksport:** heightmap 16-bit/EXR, splat mapy, mesh OBJ/glTF (z LOD), lista instancji roślinności (JSON) → import do Unity/Unreal/Godot.
- **Tryb „Imported map”:** ✅ 2026-11-03 podgląd mapy z pliku (PNG 1/2/4/8/16-bit, kolorowy zamieniany na szary, oraz R16/RAW): `HeightmapImport.*` +
  `subSystems/MapImportSys.*`, wczytywanie w tle. Dalszy ciąg w Fazie 6.
- **Edytor ręczny:** pędzle (podnieś/obniż/wygładź/spłaszcz) jako warstwa delta nad proceduralną bazą.
- **Node graph** (imnodes) do składania szumów i operacji zamiast sztywnego continentalness/mountainousness/weirdness.

### Faza 6 — operacje na wczytanej mapie (otwarte)
Rozwinięcie trybu „Imported map”, który dziś tylko pokazuje mapę z pliku.

1. **Operacje na mapie:** erozja i inne modyfikacje wykonywane w tle, na kopii `MapImportSys::map`, jak w pozostałych trybach.
2. **Eksport wyniku:** zapis zmienionej mapy przez `ExportPanel`, z zakresem wartości wybranym przy wczytaniu.
3. **Kolejne formaty:** np. EXR, TIFF; nowy format to czytnik i rozszerzenie w `HeightmapImport.cpp`.

---

## 5. Kolejność i priorytety

| Priorytet | Zadania | Dlaczego |
|-----------|---------|----------|
| P0 | Faza 0 (B13, B3, B2, B1, B11) | Bez tego każda dalsza praca jest na niepewnym gruncie |
| P1 | Faza 1: ~~B8 (szum)~~ ✅, ~~A2/B5 (RAII + width/height)~~ ✅, ~~A3/A4 (rozdzielenie warstw)~~ ✅, ~~B6 (shadery)~~ ✅, ~~B15 (tekstury)~~ ✅, ~~wielowątkowość (7a)~~ ✅, ~~generacja asynchroniczna (7b)~~ ✅, ~~pomiary~~ ✅ | Fundament — bez rozdzielenia warstw nie da się przenieść generacji na GPU |
| P1 | Faza 2.1–2.3 (compute height + chunki) | Największy zysk dla „real-time” |
| P2 | Faza 2.4–2.7, Faza 3 | Jakość i skalowanie |
| P3 | Faza 4, Faza 5, Faza 6 | Feature'y budowane na stabilnym fundamencie |

Sugerowany pierwszy krok po Fazie 0: **zmierzyć** (Tracy) czas `TerrainGenerator::GenerateTerrain` dla 512²,
naprawić B8 i zmierzyć ponownie — da to konkretny obraz, ile zostało do zyskania na CPU zanim przejdziemy na GPU.
