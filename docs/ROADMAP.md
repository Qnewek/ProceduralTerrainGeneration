# Procedural Terrain Generator — analiza i plan rozwoju

Diagnoza na 2026-10-27 (branch `lodChunked` = `master`, commit `fbd32ab`); postęp odhaczany na bieżąco, ostatnia aktualizacja 2026-11-13
(Fazy 0–3 i Teren 2.0 zamknięte: fundament i porządki, generacja wyłącznie na GPU, chunki z LOD, biomy ciągłe z edytorem, klimatem, modyfikatorami terenu
i teksturowaniem, kontynenty, tabela 18 biomów i rzeźba terenu; następna Faza 4 — roślinność).
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
| ~~A10~~ ✅ | Higiena repo: śledzony `imgui.ini`, commity „sda/asd/x”, 17 zdalnych branchy, README z pustymi sekcjami | Trudno wrócić do projektu po przerwie (jak teraz) — **Zrobione 2026-10-30:** `imgui.ini` nieśledzony (od B13), scalone branche usunięte lokalnie i z GitHuba (zostaje `master`), jedyny nie scalony commit (`infiniteTerrain`, klasa `Chunk`) zachowany jako tag `archive/infinite-terrain-chunks` (tag usunięty 2026-11-12 na prośbę właściciela, chunki zrobione od nowa w Fazie 2); README przepisany (funkcje, build, sterowanie, benchmark) — brakuje screenshotów |

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
   ✅ 2026-11-27 CI sprawdza generację: krok „Generation check (llvmpipe)” w Release uruchamia `--gpu-check --no-timings` na Mesa llvmpipe 26.3.1
   (mesa-dist-win, przypięta wersja + SHA256, cache). Wszystkie sprawdzenia przechodzą na llvmpipe bez zmian w nich (lokalnie, łącznie z porównaniami bit w bit).
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
pkt 3 biomy modyfikują teren (zrobione 2026-11-09) → pkt 5 teksturowanie (zrobione 2026-11-10; strojenie wartości domyślnych na bieżąco). Po każdym kroku nowe przypadki w `--gpu-check`.
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
   ✅ 2026-11-12 **Teren i biomy w jednym przebiegu** (branch `perf/single-pass-generation`): `GpuHeightGenerator::GenerateWorld` = `biome_compute.shader`
   z `writeHeight`, szumy raz i jedna pętla po biomach (każda odległość liczona raz zamiast ~6 razy: nearest, kolor, modyfikatory, materiały). Wyniki bit w bit
   jak dwa przebiegi (`--gpu-check`, sekcja `world:`). Czas na RTX 2060 (teren z modyfikatorami + biomy ze splatem → jeden przebieg): 512² 1,77 → 1,35 ms,
   1024² 6,25 → 4,47, 2048² 18,19 → 11,84, 4096² 73,3 → 51,4 ms (−30%). Jeden przebieg kosztuje tyle co same biomy ze splatem.
   Z pomiaru: szum plam materiałów (2 oktawy simplex na każdy obecny materiał) to ok. 40% przebiegu (4096²: 51,4 → 31,6 ms bez niego) — kandydat na następny krok,
   ale tańszy szum zmienia wygląd plam (decyzja właściciela).
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
     5: normalna XY + roughness + AO; mipmapy, anizotropia) i parametrów w bloku `std140` (binding 0, `common/materials.glsl`). `Bind` raz na klatkę w `TerrainGenApp::Draw`.
     Sekcja UI „Terrain materials”: materiał bez biomów, skala tekstur, siła normal map, progi klifów i triplanaru, height blending, specular, anti-tiling.
     To ustawienia wyświetlania, nie świata — nie trafiają do presetów.
     `TextureClass` obsługuje teraz tablice (`_layers`).
   - **Reguły per biom:** każdy biom ma do 4 warstw materiałów (`biome::MaterialLayer`: materiał + waga, normalizowane). `biome::MixMaterials` (CPU, testy) =
     sumy w `biome_compute.shader` + `finishMaterials` w `common/biomes.glsl`: materiały biomów mieszane wagą `BlendWeight` z `BiomeConfig::materialBlendWidth`, potem szum plam
     (score = c·(log w + k·patch), softmax; przy `materialPatchiness` = 1 materiał zajmuje ok. swój udział powierzchni, `materialPatchContrast` wyostrza brzegi,
     `materialPatchSize` = długość fali w tekselach, seed = `detailSeed`). Wynik to splat map (`GL_RGBA8`, tablica 4 warstw po 4 materiały), którą liczy
     `biome_compute.shader` przy `GenerateBiomes(..., splat)`; mapa skończona trzyma ją w `TerrainView::SplatTexture` (jednostka 6), chunki w `Chunk::splat`.
     UI: sekcja „Surface materials” w ustawieniach biomów i warstwy materiałów zaznaczonego biomu w edytorze. Presety: pola `material*` w `biomes`
     i `materials` w każdym biomie (bez podbicia wersji: starsze pliki dostają wartości domyślne).
   - **Shader fragmentów:** wagi ze splat mapy (bez biomów: jeden materiał), na zboczach między `cliffSlopeStart` a `cliffSlopeEnd` waga przechodzi na `cliffMaterial`,
     4 najsilniejsze materiały mieszane height-based (score = waga + `heightInfluence` · wysokość tekstury, widoczne te w `blendDepth` od najlepszego).
     Triplanar między `triplanarStart` a `triplanarEnd` (whiteout normal blend, `textureGrad`), poniżej tylko rzut z góry. AO w ambient, specular Blinn-Phong z roughness.
     Anti-tiling: druga skala UV mieszana z odległością, kolor średni z ostatniego mipa w dali, wahania jasności/odcienia szumem o niskiej częstotliwości.
     Tekstury przyklejone do terenu przez `TerrainPos` (pozycja przed `modelScale`, z TES).
   - `--gpu-check`: sekcja `materials:` (wagi sumują się do 1, tylko materiały z tabeli, jeden materiał wszędzie, plamy zmieniają mieszankę, blending zmniejsza skoki)
     i `chunk: material splat` (chunk = fragment pełnej mapy bit w bit).
   - **Do zrobienia później:** oświetlenie liniowe (sRGB), detail normal maps z bliska, pomiar kosztu FS przy przelocie. Reguły materiałów zależne od wysokości
     nie są osobne — wysokość działa pośrednio, przez klimat i biomy.
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
Zostają jako możliwe usprawnienia: koszt szumu plam materiałów (pkt 3, po połączeniu przebiegów 2026-11-12), oświetlenie sRGB, detail normal maps i pomiar kosztu FS (pkt 5), wybór osi / przekrój diagramu i ostrzeżenia
o nakładaniu się biomów (pkt 6), tarasowanie per biom.

✅ **Domknięcie Fazy 3 2026-11-13** — po zamknięciu kryterium dopracowane wartości domyślne świata i układ UI (sekcje niżej, PR #41–#44): zakładki w panelach,
kontynenty z ~60% wody, tabela 18 biomów z parametrem ELEVATION i umiarkowanym klimatem, rzeźba terenu (`heightScale` 1024, wysokie góry, rzeźba per biom).
Przeniesione do kolejnych faz: bagna (priorytet biomu w regułach) i własne materiały Jungle/Taiga — Faza 4; łagodniejsze pagórki i większy domyślny render distance
wobec gór ~900 jednostek — do decyzji przy przelotach w Fazie 4.

### Układ UI — przed Fazą 4 (decyzja właściciela 2026-11-12)
✅ 2026-11-12 (branch `feat/vertical-tabs`) Zamiast jednej długiej listy zwijanych sekcji w każdym panelu: zakładki (`utility/PanelTabs.*`) w rzędach na górze panelu,
zawijane do kolejnego rzędu, gdy się nie mieszczą (pierwsza próba, pionowa kolumna z boku, zastąpiona na prośbę właściciela), treść zakładki pod nimi,
przewijana osobno (stały pasek, więc podgląd szumu nie migocze). Lewy panel = widok: Display (tryb, isolinie,
generacja skończona/nieskończona + chunki), Camera, Sun & sky, Materials, Presets, Saving (Presets/Saving tylko w trybach z nimi), About. Prawy = generacja trybu:
TerrainGen — Terrain (rozmiar, metoda, sampling), Noises (C/M/W + podgląd), Splines, Biomes (włącznik, blend, klimat, modyfikatory, materiały), Biome table,
Biome noises (T/H); Noise heightmap — Noise, Erosion; Imported map — Load. Checkboxy „Edit component noise” / „Edit spline” i przyciski „Done” usunięte:
otwarta zakładka = edytowany element, jej szum dostaje podgląd 2D; zawsze jest wybrany składnik (domyślnie C i T). Nagłówki zwijane usunięte z sekcji, które są całymi zakładkami.

### Kontynenty — więcej oceanu (decyzja właściciela 2026-11-12)
✅ 2026-11-12 (branch `feat/continents`) Cel: ~60% wody i ląd w formie kontynentów, bez nowego mechanizmu generacji — samą kalibracją.
Pomiar: `LandStats` (udział wody, masy lądu, część lądu w masach ≥ 1% próbki) na próbce 512² co 64 teksele; „Land statistics” w zakładce Terrain i sekcja `land:` w `--gpu-check`.
Przed: 35,6% wody, jedna masa lądu z jeziorami (98% lądu razem; podstawowa fala C ≈ 1000 tekseli). Po: 60,8% wody, 59 mas, 82% lądu w kontynentach,
największy ≈ 19% lądu. Zmiany domyślnych: szum C `scale` 0,5 → 0,1, `persistance` 0,5 → 0,4, punkty `continentalnessSpline` przesunięte o +0,35
(wybrzeże przy C = 0,15); progi C biomów: Ocean < 0,15, Sand 0,15–0,2 (wąska plaża, ~8% lądu — samo przesunięcie dałoby 34%), ląd ≥ 0,2, góry ≥ 0,5 (~45% lądu jak wcześniej).
Warp i mniej oktaw sprawdzone: warp rozbija ląd na drobne masy, liczba oktaw prawie bez wpływu. Stare presety zachowują swoje wartości.
Mapa skończona (250²) to teraz wycinek jednego wybrzeża — kontynenty widać w trybie nieskończonym.
Biomy wody i plaży zależne od wysokości nad lustrem wody: zrobione w „Nowa tabela biomów” niżej.

### Nowa tabela biomów (decyzja właściciela 2026-11-13)
✅ 2026-11-13 (branch `feat/biome-table`) Pomiar starej tabeli (7 biomów) na domyślnym świecie: 42% lądu poza wszystkimi zakresami (biom „najbliższy”) —
brak biomu dla ciepłego i wilgotnego klimatu, dla średniej rzeźby i dla pagórków; 68% „Sand” leżało w głębi lądu; świat zimny (lapse rate 1,0: mediana T na lądzie −0,26,
Snow + Snowy peaks = 31% lądu); weirdness nieużywane.
- Nowy, szósty parametr biomu `ELEVATION` (wysokość względem lustra wody, [-1, 1]); presety bez zmiany wersji (brak zakresu = cały przedział).
- Klimat umiarkowany: `lapseRate` 1,0 → 0,5 (mediana T na lądzie −0,13).
- 18 biomów dzielących całą przestrzeń parametrów (0% lądu poza biomami): Deep ocean (E < −0,4), Ocean; wybrzeże E 0–0,045: Beach, Stony shore (M ≥ 0,3), Snowy shore;
  niziny (M < 0,05) wg T × H: Tundra, Snowfields, Taiga, Steppe, Plains, Forest, Desert, Savanna, Jungle; rzeźba (M ≥ 0,05): Hills (E < 0,32), Mountain,
  Rocky peaks (W ≥ 0,6), Snowy peaks (T < −0,78). Progi T: −0,78 / −0,42 / 0,25; H: −0,45 / 0,2 (ciepłe: −0,3 / 0,3).
- Udział w lądzie domyślnego świata: wybrzeże 5,5%, mroźne ~10%, umiarkowane niziny 33% (Plains 9,8, Forest 8,7, Taiga 7,4, Steppe 6,9), ciepłe 13%
  (Savanna 5,3, Jungle 4,2, Desert 3,4), Hills 17,7%, Mountain 14,0%, Rocky peaks 6,9%, Snowy peaks 5,1%. `GenerateWorld` 2048²: 17,8 → ~18–19 ms.
- Bez bagien: wymagałyby nakładania zakresów (dwa biomy o wadze 1 mieszają kolory pół na pół) — potrzebny priorytet biomu w regułach.
- Jungle i Taiga używają istniejących tekstur (grass, forest floor, tundra); własne materiały razem z Fazą 4.

### Rzeźba terenu (decyzja właściciela 2026-11-13)
✅ 2026-11-13 (branch `feat/terrain-relief`) Pomiar (próbki co 4 teksele, nachylenie w stopniach przy skali wyświetlania): przy `heightScale` 256 Mountain średnio
108 jednostek nad wodą (nachylenie 9°), wszystkie niziny 55–70 jednostek i 3–4°, pagórki niżej niż równiny — kontynent ~20 000 jednostek szeroki, szczyt maks. 256.
- `heightScale` TerrainGen 256 → 1024 (`TerrainGenApp::worldHeightScale`; Noise heightmap i Imported map zostają przy 256), suwak „Height scale” w Display (tylko widok).
- LAYERED: `mountainShare` 0,6 → 0,8; `mountainousnessSpline` prawie zero rzeźby przy niskim M, stromo do pełnej wysokości w pasmach; `peaksSpline` od 0,15 (dolina zachowuje
  część wysokości pasma); szum C 7 → 5 oktaw (drobne oktawy tylko szorstkowały niziny).
- Modyfikatory: sąsiednie biomy lądowe mają podobną amplitudę (0,85–1) — różnica amplitud robiła rampy na granicach (pierwsza próba z równinami 0,45 dawała nizinom 7–9°);
  charakter daje szum szczegółu (nachylenie rośnie z `detailAmplitude / detailSize`). Góry z amplitudą 1, więc szczyty nie są ścinane na wysokości 1.
- Progi elewacji przeliczone (rozkład E się zmienił): wybrzeże do 0,022, góry od 0,22 — udziały biomów jak w poprzedniej tabeli (wybrzeże 6%, Hills 16%, góry 28%).
- Wynik (średnio nad wodą / nachylenie): Plains 93 / 4,6°, Steppe 99 / 4,4°, Snowfields 83 / 5,0°, Savanna 100 / 5,8°, Tundra 105 / 6,4°, Taiga 98 / 6,7°, Forest 93 / 6,9°,
  Desert 92 / 10° (wydmy), Jungle 99 / 11°, Hills 148 / 25°, Mountain 366 / 37° (p95 627), Snowy peaks 369 / 39° (p95 745), Rocky peaks 518 / 47° (p95 862); ścięte szczyty 0,2%.
- Do rozważenia: pagórki (25°) są dość strome — ich rzeźba to głównie splajn M w zakresie 0,05–0,25; render distance (5000) jest mały wobec gór ~900 jednostek.

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

**Decyzje właściciela 2026-11-13:** modele CC0 glTF z Poly Haven pobierane skryptem (jak tekstury); najpierw drzewa, krzewy i kamienie, trawa osobno później;
roślinność tylko w świecie nieskończonym (TerrainGen infinite). Poly Haven ma wyłącznie fotoskany — drzewa 150 tys. – 17 mln trójkątów, kamienie 6–28 tys. —
więc uproszczenie siatek i impostory są konieczne od początku. Lekkie modele low-poly (Kenney, Quaternius) odrzucone: tylko ręczne pobieranie i styl niepasujący do fototekstur.
Skala: 1 m = 4 jednostki świata (1 teksel = 1 m), modele glTF są w metrach.

Kroki:
1. **Assety i LOD** (branch `feat/vegetation-assets`): lista modeli `res/vegetation/vegetation_models.json`, `tools/fetch_models.py` (glTF 1k + mapy alpha liści),
   wczytywanie glTF (cgltf), LOD-y przy wczytaniu (meshoptimizer dla pni i skał; liście/igły jako karty — usuwanie części kart i powiększanie pozostałych),
   cache uproszczonych siatek na dysku, tekstury w tablicach (albedo + alpha, normal + roughness + AO), wczytywanie w tle. Podgląd modelu w panelu (FBO, wybór LOD, liczba trójkątów).
   ✅ 2026-11-13 (do oceny właściciela): `terrainGeneration/VegetationMesh.*` (ptg_core, testy `VegetationMeshTests.cpp`), `opengl/VegetationModels.*`,
   `res/shaders/VegetationShaders/`, zakładka „Vegetation” w lewym panelu, `--vegetation-preview [folder]` (PNG z LOD-ami każdego wariantu + tabela). Nowe zależności vcpkg: cgltf, meshoptimizer.
   - 14 modeli (ok. 730 MB pobrania): jodła (`fir_tree_01`, 3 warianty), sadzonki jodły i sosny, drzewo liściaste, island tree, searsia, quiver tree, martwy pień,
     2 krzewy, paproć, 3 kamienie. Pełnej sosny (`pine_tree_01`, 905 MB) nie ma.
   - Warianty: Poly Haven trzyma kilka wariantów w jednym pliku obok siebie. Wariant = węzły stojące na ziemi o nakładających się podstawach; węzły wiszące (korona, liście) dołączają do pnia pod sobą.
   - Liście to karty z alfą, ale bardzo gęste (drzewo liściaste 1,9 mln, jodła 6,7 mln trójkątów igieł), więc 16 tys. na LOD0 dawało łyse drzewa.
     Budżety: drzewa 150 000 / 30 000 / 6 000, krzewy i kamienie 4 000 / 1 000 / 250; liście dostają co najmniej 60% poziomu.
     Gałęzie: najpierw odpadają najmniejsze kawałki, potem zwijanie krawędzi (meshoptimizer); vertex clustering tylko dla brył (kamień, pień) — wcześniej robił z gałęzi odłamki.
     Karty liści: zostaje równomiernie rozłożona część kawałków, powiększona (≤ 4×) do podobnej powierzchni.
   - Pierwsze wczytanie (budowa LOD-ów) ok. 6–7 s w tle, najdłużej jodła; potem z cache ok. 0,1–0,2 s na model. Cache: `res/vegetation/cache/*.mesh` (klucz: rozmiar i data plików + budżety).
   - Shader: alpha test z podbiciem alfy na dalszych mipach, oba boki liści z przeświecaniem, normal mapy z ramki z pochodnych (bez tangentów), oświetlenie i mgła jak terenu.
   - Znane: trzecia jodła ma czarny pień (materiał `trunk_c`, do sprawdzenia mapowanie UV); LOD2 drzew jest rzadki — zastąpią go impostory (krok 4).
     (Pień naprawiony 2026-11-27, punkt 7.)
2. **Rozmieszczenie i rendering:** compute per kafel świata (jittered grid, seed z kafla — deterministyczne i bez szwów), wysokość i biom liczone tą samą funkcją co teren,
   reguły na sztywno (gęstość z biomu, maska nachylenia i wody), instancing z wyborem LOD po odległości.
   - Wynik (2026-11-12, w toku): zamiast compute rozmieszczenie na CPU z map chunków poziomu 0 czytanych z GPU (≤ 4 chunki na klatkę) — te same mapy co teren,
     więc wysokość i biom zgadzają się co do teksela; prostsze do testów. Siatka z jitterem 8 m (drzewa, skały) i 4 m (krzewy), szum klastrujący lasy i polany,
     gatunki po nazwie biomu (`vegetation::DefaultRules`). Rysowanie: instancje w zasięgu i we frustum, posortowane w grupy model/wariant/LOD, jeden SSBO na klatkę.
     Przeniesienie do compute — w kroku 4 razem z cullingiem na GPU, jeśli odczyt map okaże się za wolny.
3. **Reguły per biom:** tabela roślinności w `BiomeConfig` (model, gęstość, zakres nachylenia/wysokości, skala, klastrowanie), edytor, presety.
   - Wynik (2026-11-14, do oceny właściciela): `biome::Plant` w `Biome::plants` (≤ 16 na biom), sekcja „Vegetation” w edytorze biomów (dodawanie, kolejność,
     udział komórek warstwy z ostrzeżeniem > 100%, „Default plants”), pole `plants` w presetach bez nowej wersji — biom bez niego dostaje `DefaultPlants` po nazwie.
     Warstwa z rodzaju modelu (`kind` w `vegetation_models.json`), nie z reguły. Roślina rośnie tylko w swoim zakresie elewacji i nachylenia; te, które w danej komórce
     nie mogą rosnąć, nie zabierają udziału pozostałym. Modele jeszcze wczytywane zachowują swoje komórki (puste), więc reszta nie przeskakuje po wczytaniu.
     Zmiana roślin nie regeneruje terenu; chunki są rozmieszczane od nowa, a do tego czasu widać stare instancje.
4. **Impostory i culling na GPU:** impostory pieczone przy wczytaniu (octahedral), frustum/distance culling w compute, `glMultiDrawElementsIndirect`; pomiar 100k instancji.
   - Wynik (2026-11-14, do oceny właściciela): impostory (8×8 klatek hemi-octahedral, atlas 1024² na wariant, od 150 m, checkbox „Impostors”), drzewa także na grubszych
     chunkach (daleki las do końca zasięgu widoku, tylko impostory), instancje na GPU na stałe, culling i wybór LOD w compute, rysowanie pośrednie, rozmieszczanie na wątkach.
     Pierwszy pomiar pokazał, że 100k drzew było nieosiągalne konstrukcyjnie: drzewa stały tylko na chunkach poziomu 0 (~770 m), maks. ~42 tys. rozmieszczonych.
   - Pomiar `--vegetation-bench` (1920×1080, dżungla, oko 30 m, kamera w stronę lądu; GPU roślinności, siatki LOD2 → impostory):

     | zasięg, gęstość | rozmieszczone | w kadrze | siatki | impostory | CPU rysowania |
     |---|---|---|---|---|---|
     | 1250 m (domyślny), ×1 | 22 tys. | 5,5 tys. | 8,3 ms | 1,2 ms | 0,10 ms |
     | 4 km, ×1 | 50 tys. | 9,4 tys. | 13,6 ms | 0,8 ms | 0,10 ms |
     | 8 km, ×3 | 509 tys. | 64 tys. | 84 ms | 1,4 ms | 0,11 ms |

     Kryterium 100k+ drzew przy 60 FPS spełnione z zapasem (pół miliona w świecie, 64 tys. w kadrze za 1,4 ms). Teren 0,3–0,6 ms.
   - Sprawdzone 2026-11-14: klatki 70–110 ms to tylko 2 pierwsze klatki (jednorazowe przygotowanie generacji); potem przy domyślnym zasięgu żadna klatka ładowania
     nie przekracza 16,7 ms (rozmieszczanie ≤ 5 ms). Dłuższe klatki przy 8 km były w fazie benchmarku z siatkami (GPU 84 ms/klatkę, odczyt map na to czeka) — PBO niepotrzebne.
   - Granica 150 m: impostor ma średnio kolor pełnego modelu LOD0 (pas koron 45/53/27 wobec 44/53/28; AO już nie wpieczone w albedo, tylko stała okluzja światła otoczenia).
     Różnicę robiło za rzadkie LOD2 (ok. 1 na 40 liści, powiększanie ograniczone do 4×): limit podniesiony do 8× (`maxCardScale`, cache LOD-ów v6).
     Jodła w LOD2 nadal rzadsza (gałązki z igłami słabo wypełniają koronę po powiększeniu) — od 2026-11-27 LOD2 jodły ma 12k trójkątów (punkt 7).
5. **Trawa:** osobny system blisko kamery (compute + wiatr w VS).
   - 2026-11-14: `GrassLayer` — źdźbła z compute co klatkę wokół kamery według materiałów splat mapy (`grass` w `terrain_materials.json`), jedno rysowanie pośrednie,
     wiatr w VS; ~8 tys. źdźbeł w dżungli za 0,05 ms GPU. Do oceny wyglądu właściciela (gęstość, kolory, łąki).
6. **Wspólny UBO kamery i światła:** ✅ 2026-11-14 `FrameUniforms` + `common/frame.glsl` (teren, roślinność, impostory, trawa; niebo ma nadal własne uniformy).
7. **Pozostałości** (2026-11-27, branch `fix/vegetation-leftovers`):
   - ✅ Czarny pień trzeciej jodły: prymityw `trunk_c` w pliku Poly Haven nie ma `TEXCOORD_0` (w Blenderze współrzędne generowane), więc UV = (0, 0) wszędzie,
     pochodne UV w FS = 0 i rama tangentów z pochodnych = 0 · ∞ = NaN (czarne cieniowanie). Teraz `vegetation::CylinderUvs` daje takiemu prymitywowi UV
     cylindryczne (oś za środkiem wierzchołków na każdej wysokości — pień jest pochylony, całkowita liczba kafli 1 m dookoła, v = wysokość + wystawanie
     korzeni ponad zwykły promień, wierzchołki na szwie kopiowane), a `cotangentFrame` przy zerowych pochodnych bierze dowolną ramę wokół normalnej. Cache LOD-ów v7.
   - ✅ Szczelina pod pniami na stromych zboczach: model stał najniższym punktem na wysokości terenu w środku, więc po stronie w dół zbocza o nachyleniu g
     podstawa wisiała o promień · g. Każdy wariant ma `sinkPerSlope` (największa odległość od osi wierzchołków dolnych 10% modelu: grunt r m w dół zbocza
     leży r · g niżej, więc obniżenie o g · r nie dodaje szczeliny, której model nie ma na płaskim — liniowe w g, dokładne przy każdym nachyleniu),
     `PlaceChunk` obniża instancję o `sinkPerSlope` · skala · g (`PlacementSettings::gradientUnitsPerMetre`: gradient na jednostkę świata albo na metr przy
     nachyleniu z wysokości mapy w `vegexport`). `ComputeNormals` liczy już tylko normalne nowego prymitywu (wcześniej nadpisywał normalne z pliku
     wcześniejszych prymitywów tego samego materiału). Jodła 0,68–0,73 m, kamienie 0,12–0,35 m, paproć 0,2–0,5 m
     (liście przy ziemi), leżący pień 0,95 m (kolumna „sinks” w `--vegetation-preview`). Ten sam wynik w eksporcie i w Imported map.
   - ✅ Rzadki LOD2 jodły: własne `lods` w `vegetation_models.json` = 150k / 30k / 12k. `--vegetation-bench 1 30`: z impostorami bez zmian (1,76 mln trójkątów),
     dalekie drzewa jako siatki 33,5 → 45,7 mln trójkątów, GPU roślinności 9,5 → 11,1 ms.
   - Świadomie bez zmian (decyzja właściciela): szum plam materiałów. Pomiar `--gpu-check` (jeden przebieg teren + biomy + splat): 4096² 58 ms, bez szumu plam 45 ms
     (ok. 23%), 512² 1,65 / 1,27 ms; chunk 131² to ułamek milisekundy. Rozważane: wspólne 2 pola szumu (odrzucone — materiały o bliskich kątach dostają
     prawie ten sam szum i znikają plamy między nimi), jedna oktawa (połowa kosztu), wspólna siatka simplex z osobnym hashem gradientów (zysk niepewny).
   - Do sprawdzenia w aplikacji: pień trzeciej jodły z bliska (szew tekstury, korzenie); drzewa, krzewy i kamienie na stromym zboczu (bez szczeliny od strony
     w dół, niezbyt głęboko od strony w górę), paprocie na zboczu; jodła w zasięgu LOD2 (przed 150 m) z wyłączonymi impostorami i z włączonymi.

### Faza 5 — realizm i narzędzia (otwarte)
- **Erozja na GPU:** hydrauliczna (pipe model / virtual pipes lub kroplowa w compute z atomics) + termiczna;
  tryb „bake” dla obszaru (erozja jest nielokalna — dla chunków: erozja na większym regionie z marginesem i zszywanie).
  ✅ 2026-11-16 (`GpuErosion`, decyzja właściciela: najpierw ta sama metoda na GPU, potem rury; wersja CPU do `legacy/cpuErosion/`, niekompilowana,
  zapisywanie ścieżek kropel usunięte): (1) **krople** — algorytm wersji CPU 1:1 (kroki w lockstepie, mapa w buforze stałoprzecinkowym + `atomicAdd`);
  na tej samej mapie statystyki zgodne z CPU w granicach rozrzutu między uruchomieniami (200k kropel: wymyte 2331–2335, osadzone 1953–1958). Jedyna różnica
  zamierzona: komórka poniżej 0 nie jest już podnoszona do 0 przy wymywaniu. 20k kropel na 1024²: 4,2 ms (GPU), 1M: 177 ms.
  (2) **rury wirtualne + erozja termiczna** (Mei i in. 2007), deterministyczne; zmiany względem artykułu: osad płynie rurami z wodą (masa zachowana,
  strata = osad wyniesiony za krawędź, ok. 1,5% na mapie testowej), pojemność ∝ przepływ (prędkość × głębokość) zamiast samej prędkości (inaczej cienka
  warstwa wody unosiła więcej materiału niż sama miała i strata sięgała 69%), wymywanie nie schodzi poniżej najniższego sąsiada (inaczej doły „+”).
  Poprawka tego samego dnia (zgłoszenie właściciela: linie spiczastych kolców): osad niesiony cienkimi strużkami spadał naraz na pojedyncze teksele —
  głównie przy końcowym zrzucie (ok. 85% kolców), reszta przy gwałtownym spadku przepływu. Teraz odkładanie i końcowy zrzut nie podnoszą teksela ponad
  najwyższego sąsiada + wysokość kąta zsypu (nadmiar płynie dalej / ginie, +0,2–0,3 pkt proc. straty), a po iteracjach deszcz ustaje i woda wysycha
  powoli (parowanie 1, do 1% wody; szybsze wysychanie dawało więcej kolców). Kolce na mapach testowych: 255–444 (do 10 tekseli) → ok. 30, ≤ 0,72 teksela;
  test „pipes: no spikes” w `--gpu-check`. 300 iteracji (+ ok. 90 wysychania): 1024² 330 ms, 2048² 1,3 s — liczone w jednej klatce. Wybór metody w zakładce Erosion, w presetach `erosion.method` i `erosion.pipes`.
  Dalej: erozja rozłożona na klatki (podgląd na żywo, bez przycięcia przy dużych mapach), chunki / „bake” obszaru, erozja w trybie TerrainGen.
- **Rzeki i jeziora:** flow accumulation / priority-flood na mapie niskiej rozdzielczości, rzeźbienie koryt, maska wilgotności dla biomów.
  Decyzja właściciela 2026-11-16: obie metody do wyboru w UI, tylko w TerrainGen; etap 1 = hydrologia, mapa rzek na GPU, żłobienie koryt, UI + presety;
  etap 2 = woda z wysokością na piksel i przepływem, biomy/roślinność przy wodzie.
  🟡 2026-11-16 etap 1 (zakładka „Rivers”, preset `rivers`, domyślnie bez rzek): (A) **Region** — `Hydrology.*` (CPU, testy): próbka terenu bez rzek
  (`SampleTerrain`, kwadrat `regionSize` wokół środka mapy co `cellSize`), priority-flood + ε (Barnes 2014), D8 po wypełnionej powierzchni, akumulacja,
  rzeka od `sourceArea`, szerokość ~ √(powierzchnia zlewni), poziom wody nie rośnie w dół rzeki, meandry + wygładzanie biegu; koryta jako odcinki rasteryzowane do
  `RiverMap` (wektor do najbliższego punktu biegu — dokładny przy próbkowaniu dwuliniowym — odległość, szerokość, poziom rzeki i jeziora). Liczone w tle, teren
  regenerowany po przyjściu sieci; zanika przy granicy regionu. 512² komórek: 69 ms, 705 komórek rzek na domyślnym świecie. (B) **Noise** — kanały wzdłuż zerowych linii
  szumu (odległość = |n| / |∇n|) do poziomu morza, tylko na nizinach (`maxElevation`); lokalne, więc działa w nieskończonym świecie. Profil koryta wspólny
  (`common/rivers.glsl`, po modyfikatorach biomów): dno U pod lustrem, brzegi `bankSlope`, wygaszanie na `bankWidth`; nigdy nie podnosi terenu.
  `--gpu-check` sekcja `rivers:` (zakres, jeden przebieg = dwa, chunki bit w bit). Ograniczenia etapu 1: wody rzek i jezior nad poziomem morza nie widać
  (rzeki B leżą na poziomie morza, więc mają wodę), biomy wybierane z wysokości przed rzekami. Po przeglądzie (właściciel): rzeki B nie żłobią dna morza, kończą się na wybrzeżu.
  🟡 2026-11-16 etap 2: (4) woda rzek i jezior metody A — siatka z sieci (`BuildWaterMesh`: quad między środkami komórek, gdzie choć jeden jest mokry; suche narożniki na
  najwyższym poziomie quadu), linia brzegu z testu głębi z terenem, głębokość z kopii głębi sceny, fale niesione przepływem (flow map, prędkość 0,4–4 m/s ze spadku).
  (5) wilgotność biomów przy korycie (`humidity`, `humidityRange`), materiał dna w splat (`bedMaterial`, domyślnie Scree), roślinność nie stoi w wodzie rzek i jezior
  (`WaterLevelAt`). Zostaje: woda rzek B bez przepływu (leży na poziomie morza), pionowe progi (schodkowy profil rzeki).
  ✅ 2026-11-27 wodospady i kaskady (bez zmiany generacji): stroma tafla rzeki (nachylenie rysowane 0,3–0,7, na domyślnym świecie ok. 1–3% komórek koryta,
  maks. ok. 0,8–1,1) = nieprzezroczysty biały strumień smugowany w dół spadku, oświetlony z nachylenia tafli; poniżej spadku kotłująca się piana zanikająca na ok. 12 m
  (też na tafli jeziora przy ujściu). TerrainGen (REGION) i Imported map. Do sprawdzenia w aplikacji: rzeka schodząca z gór / odpływ jeziora po stromym zboczu,
  z bliska i z daleka (bez migotania), Height scale większy i mniejszy (więcej / mniej białej wody), widok 2D, mapa DEM z rzekami.
  ✅ 2026-11-27 próg powierzchni jezior: `RiverConfig::minLakeArea` (teksele², w Imported map px², suwak „Min lake area”, presety i projekt), grupa
  4-spójnych komórek jeziora mniejsza od progu = ląd (rzeka przechodzi przez nią z wodą na poziomie przelewu). Domyślnie 2500: region 4096² domyślnego
  świata (komórka 8) miał 371 jezior, z czego 354 poniżej 10 komórek; zostają 4. Do sprawdzenia w aplikacji: TerrainGen z rzekami Region — drobne
  jeziorka zniknęły, „Min lake area” 0 przywraca je; Imported map — suwak przelicza sieć po puszczeniu; preset i projekt zapamiętują wartość.
- **Zagnieżdżone splajny** (z „Teren 2.0”, pkt 3): punkty splajnu C są splajnami po M (i dalej po PV), wypiekane do tekstury LUT 2D/3D próbkowanej jednym odczytem.
  Daje klify tam, gdzie góry dochodzą do morza, płaskie plaże gdzie indziej, doliny wcięte poniżej podstawy. Najwięcej pracy: edytor UI (lista punktów C z wykresem po M, podgląd C×M jako mapa ciepła).
  ✅ 2026-11-27 (decyzje właściciela): nowa metoda NESTED obok LAYERED (domyślny świat bez zmian), trzy poziomy C → M → W z opcjonalnym zagnieżdżeniem
  (punkt = liczba albo krzywa poziom niżej, krzywe W tylko liczby), wynik = wysokość w [0, 1], własny „Sea level”. Bez tablicy LUT: shader liczy drzewo wprost
  (`terrain.glsl`, tylko węzły potrzebne segmentowi), reguła też na CPU (`NestedSpline.*`), `--gpu-check` porównuje (różnica ≤ 2,1e-7, chunki bit w bit).
  Koszt (4096², domyślne drzewo z LAYERED): teren 19,2 ms vs 14,9 ms LAYERED, jeden przebieg z biomami 65,3 vs 60,9 ms. Pierwsze przełączenie na Nested buduje drzewo
  z krzywych LAYERED („Copy from Layered” robi to na żądanie). Edytor w zakładce splajnów: krzywa otwartego poziomu ze ścieżką, Make sub-curve / Make constant,
  mapa ciepła C × M przy wybranym W z linią brzegu. Presety: pole `terrain.nested`.
  Do sprawdzenia w aplikacji: przełączenie na Nested — świat wygląda jak Layered; przeciąganie punktów na każdym poziomie — teren nadąża na żywo; sub-curve
  i klif przy górskim wybrzeżu; usunięcie punktu z otwartą podkrzywą; Sea level przesuwa wodę, brzeg i biomy; zapis / odczyt presetu z Nested;
  Layered → Nested → Layered → Nested zachowuje drzewo; `seaLevel` w paczce eksportu; Send to Imported map.
- **Woda:** płaszczyzna oceanu z refleksją/refrakcją, głębia, piana przy brzegu.
  🟡 2026-11-14 pierwsza wersja (`WaterSurface`): quad na chunkach gruntu, głębokość z mapy wysokości chunka (kolor, przezroczystość, piana),
  odbicie nieba + słońca z Fresnelem, fale z sinusów wygaszanych `fwidth`. Też na mapie 2D (z góry).
  ✅ 2026-11-14 dokończona: kopia sceny przed wodą → refrakcja (dno przesunięte falami, osłabiane wzdłuż promienia widzenia) i odbicia SSR (śledzone dla gładkiej
  tafli, fale marszczą odczyt; poza ekranem niebo); woda także na skończonej mapie. Koszt ok. 0,5 ms. Ograniczenie SSR: nie odbija tego, czego nie ma na ekranie.
- **Atmosfera:** fog zależny od wysokości, skybox/atmospheric scattering, cykl dnia, cienie (cascaded shadow maps).
  ✅ 2026-11-14 (`Sky`): niebo z rozpraszania (Hillaire 2020: LUT transmitancji raz, LUT widoku nieba co klatkę w compute; Rayleigh, Mie, ozon, przybliżone wielokrotne
  rozpraszanie), kolory słońca i ambientu z tego samego modelu, zamglenie wysokościowe (`fog.glsl`) zamiast samej mgły odległości, pora dnia + cykl dnia (noc 3× szybciej),
  gwiazdy. Brak: perspektywy powietrznej z pełnego modelu (zamglenie jest artystyczne).
  ✅ 2026-11-27 księżyc (checkbox „Moon” + „Moon brightness” w Sun & sky): pełnia naprzeciw słońca na jego torze przechylonym o 25°; gdy słońce zejdzie
  poniżej −4°, światło kierunkowe (`light.direction`: cienie, specular, odblask na wodzie, światło chmur) staje się księżycem, jego światło rośnie do −12°
  (`MOON_LIGHT`, ambient nocy × 0,6), a niebo, mgła i ambient idą za słońcem (`FrameBlock::skySun`, `common/light.glsl`). Tarcza z poświatą przygasza gwiazdy.
  Bez faz. Koszt bez zmian (bench). Do sprawdzenia w aplikacji: zachód słońca bez przeskoku światła i cieni, noc z cieniami drzew i odblaskiem na wodzie,
  tarcza księżyca wschodzi o zachodzie, „Moon” wyłączony = noc jak dawniej, dzień bez zmian, kamera nad chmurami nocą.
  ✅ 2026-11-14 chmury (`Clouds`): wolumetryczne, ray march w compute w połowie rozdzielczości (szumy 3D Perlin-Worley + mapa pogody, wiatr), światło słońca i nieba,
  cienie chmur na terenie i roślinach, odbicia w wodzie, kamera nad chmurami (składanie po scenie). Ok. 1 ms. Brak: chmur wysokich (cirrus), deszczu.
  ✅ 2026-11-27 akumulacja czasowa chmur („Temporal smoothing” w sekcji Clouds, domyślnie włączone): marsz każdej klatki (+ średnia odległość rozpraszania)
  → `cloud_resolve_compute.shader`: reprojekcja macierzą poprzedniej klatki, historia przycięta do min/max 3×3 marszu, 10% nowej klatki; osobna historia
  dla nieba i dla składania po scenie, przesunięcie kroków złotą proporcją. Koszt ok. +0,07 ms (bench). Do sprawdzenia w aplikacji: ziarno chmur z bliska
  znika (porównać z checkboxem), szybki obrót kamery i przeskok (Places) bez smug, kamera nad podstawą chmur, zmiana rozmiaru okna.
  ✅ 2026-11-14 cienie: `ShadowMap`, do 4 stabilizowanych kaskad 2048², teren i roślinność rzucają, wszystko (teren, rośliny, impostory, trawa, woda) przyjmuje;
  PCF 3×3 na terenie, 1 próbka sprzętowa na roślinach. Koszt w dżungli (bench 1, oko 2 m): 3,3 ms przy wysokim słońcu, 5,5 ms przy 12°; teren 0,5 ms, reszta rośliny.
  ✅ 2026-11-27 mieszanie granic kaskad: na ostatnich 10% głębokości każdej kaskady (poza ostatnią) cień = mieszanka z następną kaskadą (`smoothstep`, `CASCADE_BLEND` w `common/shadow.glsl`), bez skoku ostrości
  na granicy; koszt bez zmian w granicach pomiaru (bench). Do sprawdzenia w aplikacji: teren z cieniami drzew przy wysokim i niskim słońcu, 1–4 kaskady, „Cascade split” 0 i 1 — granic kaskad nie widać.
  ✅ 2026-11-27 cień w trawie: grunt pod źdźbłami ciemnieje wg udziału trawy materiałów (splat × `grass`, tryby TEXTURED i BIOMES, w zasięgu trawy z jej wygaszaniem),
  światło kierunkowe mocniej przy niskim słońcu; dolne części źdźbeł tracą światło kierunkowe zależnie od wysokości słońca (zamiast stałego przyciemnienia).
  Bench (las, podłoże `grass` 0,25): oświetlony grunt ok. 15% ciemniejszy, teren GPU 0,42 → ~0,5 ms. Do sprawdzenia w aplikacji: łąka (Grass, `grass` 1) przy niskim
  i wysokim słońcu, brak pierścienia na granicy zasięgu trawy, klify bez przyciemnienia, „Grass” wyłączona / 2D / inne tryby wyświetlania = jak dawniej, noc z księżycem.
  ✅ 2026-11-27 ostatnia kaskada co drugą klatkę (checkbox „Farthest cascade every other frame”, domyślnie włączony, przy ≥ 2 kaskadach): w klatce pominiętej zostaje
  mapa i macierz słońca z klatki, która ją narysowała (odbiorcy rzutują nią), granica kaskady liczona co klatkę; rysowana od nowa po zmianie światła o > 0,25°, rozmiaru wycinka (teksel: zasięg cieni, podział, FOV, okno), liczby
  kaskad, rozdzielczości, skali modelu / wysokości i po wyłączeniu cieni. Bench (las, impostory): cienie 5,19 → 4,53 ms (słońce 12°), 3,10 → 2,53 ms (40°), maks.
  bez zmian. Próbowane i odrzucone: rośliny dalszych kaskad tylko jako impostory — kaskady 2–3 i tak rysują impostory (`shadowDetail` 0,3 → od 45 m), zysk 0,2 ms
  tylko z kaskady 1, a cień drzew w 15–40 m wyraźnie się zmieniał; koszt to wypełnianie liści z testem alfa, nie trójkąty. Do sprawdzenia w aplikacji: szybki lot
  i obrót kamery — daleki cień (100–300 m) bez skoków i bez pasa na granicy kaskad, przyspieszony cykl dnia, zmiana na księżyc, 1 kaskada = jak dawniej.
- **Eksport:** heightmap 16-bit/EXR, splat mapy, mesh OBJ/glTF (z LOD), lista instancji roślinności (JSON) → import do Unity/Unreal/Godot.
  Kolejność (2026-11-20, właściciel zostawił ją do zaproponowania): (1) paczka + metadane + EXR, (2) mapy materiałów i biomów, (3) siatka glTF/OBJ z LOD
  + woda, (4) instancje roślinności, (5) opcjonalnie skrypty importu Unity/Godot.
  🟡 2026-11-20 krok 1: eksport to paczka `exports/<nazwa>/` — mapy wysokości (PNG 16-bit, R16, nowy EXR float32 w metrach: własny zapis bez kompresji,
  jeden kanał „Y”) + `<nazwa>.json` (`ExportManifest.*`): siatka (teksele, 1 m na teksel, rozmiar w metrach, osie, kolejność wierszy), wysokości w metrach
  (metry na jednostkę z „Height scale”, min/max mapy), kodowanie każdego pliku, poziom morza (TerrainGen, LAYERED), wskazówki importu (Unreal: skala Z
  i położenie, Unity: rozmiar terenu, wymóg 2^n + 1), parametry generacji (preset). Usunięty przycisk-zaślepka „Save heightMap as an 3d object”.
  🟡 2026-11-20 krok 2 (`MaterialExport.*`, checkboxy „Material maps” / „Biome maps”, tylko TerrainGen z biomami): splat z GPU → `_splat_N.png` (RGBA, 4 materiały,
  alphamapy Unity) i `_weight_ID_nazwa.png` (szare, po jednej na materiał obecny na mapie — warstwy Unreal, Godot), udziały znormalizowane do sumy 255
  na teksel (największe reszty; GPU gubi do kilku jednostek przy zaokrągleniu do 8 bitów); `_biome_ids.png` (8 bit) i `_biome_colors.png` (RGB).
  W JSON lista materiałów (id, nazwa, plik splat + kanał, plik wag, pokrycie) i biomów (id, nazwa, kolor, udział).
  🟡 2026-11-20 krok 3 (`TerrainMesh.*`, `MeshFile.*`, `MeshExport.*`, „Terrain mesh” / „Levels of detail” / „Tile size” / „Water” w Saving): siatka terenu
  w metrach (Y w górę, osie glTF), GLB (własny zapis, walidowany w testach przez cgltf) albo OBJ; plik na LOD, kafle jako węzły; LOD 0 = każdy teksel,
  kolejne przez `meshopt_simplify` (¼ trójkątów, z poprzedniego poziomu), bez fałd dzięki upraszczaniu kopii ze ściśniętą wysokością; normalne z pełnej
  rozdzielczości; UV = mapy paczki (w glTF tekstura kolorów biomów). Woda: quad morza + siatka rzek i jezior (REGION). Budowa w tle. Mapa 1024² (fBm,
  góry ~150 m): siatka 2,1 mln trójkątów; LOD1 523 tys. / 1,1 s / błąd 1,1 m, LOD2 131 tys. / 0,5 s / 1,9 m, LOD3 33 tys. / 0,2 s / 5,9 m (maks. różnica
  pionowa). Pierwsza wersja (bez ściśnięcia) zostawiała odwrócone trójkąty na stromych zboczach (9 z ok. 370 na małej mapie testowej; przy ściśnięciu
  do nachylenia 0,5 jeszcze 39 na mapie 1024² ze zboczami ~17, przy 4× mocniejszym 0) — testy „no folds”.
  🟡 2026-11-20 krok 4 (`VegetationExport.*`, „Vegetation” / „Vegetation models” w Saving, tylko TerrainGen z biomami i wczytanymi modelami): roślinność
  skończonej mapy rozmieszczana tym samym `vegetation::PlaceChunk` co świat nieskończony (chunki 128 tekseli z marginesem wyciętym z pełnej mapy, ten sam
  seed `detailSeed`, gęstość z suwaka, poziom wody, rzeki), więc te same drzewa co w aplikacji — test porównuje chunk paczki z chunkiem aplikacji (inny
  `mapOrigin`, 4 jednostki na teksel). `_vegetation.json`: lista modeli (nazwa, rodzaj, warstwa, liczba wariantów i instancji, plik i węzły) + wiersze
  `[x, y, z, yaw, scale, model, variant]` w metrach (pozycja = podstawa modelu na terenie, yaw w radianach wokół +Y jak w shaderze). Modele: `vegetation/<asset>.glb`,
  węzeł `<nazwa>_<wariant>_LOD<n>` = LOD-y aplikacji (z cache `res/vegetation/cache`, `vegetation::CacheKey` przeniesiony do `ptg_core`; bez cache budowane
  od nowa), materiały PBR (albedo, normal, ORM; alpha-test = `MASK`, liście dwustronne), tekstury kopiowane do `vegetation/textures/`, osobna mapa alfa
  łączona z albedo w RGBA PNG. `MeshFile`: węzeł ma teraz kilka prymitywów (po jednym na materiał). Pierwsza wersja zapisywała siatkę źródłową — jodła
  363 MB; z LOD-ami aplikacji 29 MB (LOD0 150 tys. trójkątów). Wszystkie 14 modeli: test „downloaded models” (`--no-skip`), z cache jodła 0,6 s
  (bez cache 13 s), krzewy ~0,3 s (kodowanie PNG z alfą). Mapa pochodnych skończonej mapy różni się od chunków tylko na samej krawędzi mapy (nachylenie
  z obciętej różnicy), więc tam pojedyncze rośliny mogą się różnić.
  🟡 2026-11-25 krok 5 (decyzja właściciela: „założenie narzędzia, które pozwoli na opracowanie terenu i wykorzystanie go w zewnętrznym silniku,
  ma być spełnione”; plan docs/superpowers/plans/2026-11-25-engine-import-scripts.md): paczka dostaje to, czego potrzebują silniki — „Material
  textures” (`EngineExport.*`: albedo, normal w konwencji OpenGL, roughness materiałów obecnych na mapie → `materials/<asset>/`; w manifeście
  `materials.list[].textures` z `tileMetres` = tileSize · Texture scale / 4 · piksel i zawsze `fallbackColor`), w `_vegetation.json` `lodRanges`
  warstw (odległości LOD aplikacji ze „Detail distance”), „Engine import scripts” (kopia `res/engine/` do `engine/` + zawsze plik R16 mapy głównej,
  bo oba importery czytają wysokości z niego). Importery:
  **Unity** (`engine/unity/Editor/PtgPackImporter.cs`, menu Tools → PTG → Import pack…, 2022.3 / 6): Unity Terrain w kaflach ≤ 4097 (rozdzielczość
  przeliczana dwuliniowo do 2^n + 1, sąsiedzi połączeni), warstwy z tekstur (brak tekstury = kolor zastępczy), alphamapy z `_splat_N.png`,
  Terrain Trees z prefabów z LODGroup (węzły `<nazwa>_<wariant>_LOD<n>`, wysokości ekranowe z `lodRanges`), morze jako płaszczyzna, rzeki
  i jeziora z `_water.glb`. GLB wymaga pakietu glTFast (`com.unity.cloud.gltfast`) — bez niego teren, warstwy i morze są, reszta z ostrzeżeniem.
  Osie: Unity jest lewoskrętne, więc pozycja (x, y, z) paczki trafia w (x, y, rozmiarZ − z) — z góry mapa wygląda jak w aplikacji; obrót roślin
  π − yaw i woda glTF obrócona o 180° zakładają, że glTFast odwraca X.
  **Godot 4.3+** (`engine/godot/addons/ptg_import`, Project → Tools → Import PTG pack…): wymaga siatki terenu glTF; kopiuje do `res://ptg/<nazwa>/`
  tylko pliki do importu (GLB, `vegetation/`, kolory biomów), buduje scenę `<nazwa>.scn`: kafle wszystkich LOD z `visibility_range` (LOD n od
  błędu · 500 m), shader `ptg_terrain.gdshader` (do 16 materiałów z map splat, tablice tekstur 1024² zapisane jako `.res`), kolizja
  `HeightMapShape3D` z R16 (wysokości w tekselach, węzeł skalowany jednolicie pikselem), woda z `_water.glb` (albo płaszczyzna) z
  `ptg_water.gdshader`, rośliny jako `MultiMeshInstance3D` na model, wariant, LOD i kwadrat (Godot liczy zasięg widoczności do węzła, nie do
  rośliny, więc każdy LOD ma własną siatkę kwadratów ≤ połowy odległości, gdzie się kończy; na mapach > ~4 km kwadraty rosną do mapa/64),
  transformacje wpisywane jednym buforem (`MultiMesh.buffer`). Unity: alphamapa ≤ 2048² na kafel, liczona blokami po 256 wierszy.
  Sprawdzenie: `ptg_tests --no-skip -tc="Engine sample packs"` pisze 3 paczki (`out/engine_samples`: full 257 × 193 z morzem, materiałami
  i roślinami, dem 301 × 211 po 30 m bez modeli roślin, minimal 129² same wysokości), `tools\check_godot_import.bat <Godot console exe>`
  importuje je w Godocie bez okna i sprawdza rozmiar terenu, LOD-y, kolizję, wodę, liczbę roślin i powtórny import (zielone na Godot 4.7.2;
  bez okna Godot zgłasza `img.is_null()` przy tablicach tekstur — atrapa renderera nie trzyma danych, uruchomienie z oknem jest czyste).
  Skryptu Unity nie dało się tu uruchomić (brak Unity; Roslyn: bez błędów składni).
  Do sprawdzenia w aplikacji: eksport z TerrainGen (biomy, siatka glTF, rośliny + modele, wszystkie opcje) — w paczce `materials/`, `engine/`,
  `_height*.r16`, w manifeście `textures`, w `_vegetation.json` `lodRanges`; to samo z Imported map (piksel 30 m).
  Do sprawdzenia w Unity (2022.3 lub 6): import paczki z TerrainGen bez glTFast (teren, warstwy, morze, ostrzeżenie) i z glTFast (drzewa, rzeki);
  mapa nie jest lustrzana (linia brzegu jak w widoku 2D aplikacji); drzewa asymetryczne obrócone jak w aplikacji; rzeki leżą w korytach;
  mapa Imported map 8192² → kafle 2 × 2 bez szwów, czas i pamięć importu; powtórny import zastępuje obiekt i zasoby; LOD-y drzew przełączają się
  w odległościach jak w aplikacji, drzewa nie znikają w oddali (ustawienia Terrain: Tree Distance, Max Mesh Trees).
  Do sprawdzenia w Godocie: Project Settings → Plugins → włączenie, import przez menu (czekanie na import GLB, otwarcie sceny — sprawdzenie bez okna
  tego nie przechodzi), przełączanie LOD przy oddalaniu (też rośliny na granicy kwadratów), kolizja z CharacterBody3D,
  powtarzanie tekstur jak w aplikacji, czas importu dużej paczki (2 mln roślin parsowanych w GDScript).
- **Mapa częściowa jako wycinek świata:** 🟡 2026-11-20. Stan przed: woda (morze, rzeki, jeziora) była na mapie częściowej, roślinności, trawy i cieni
  nie było (tylko na chunkach), a mapa zawsze leżała w początku świata (teksele 0..W). Teraz: `mapOffset` (oba tryby z „Infinite generation”) — wyjście
  ze świata nieskończonego centruje mapę pod kamerą, Display → „Partial map”: offset ręcznie, „Centre on camera”, „World origin”, „Follow camera”
  (TerrainGen: nowy środek, gdy kamera odejdzie o ćwierć mapy). Generacja z `MapArea::origin`, wyspa nadal w początku świata, region rzek i statystyki
  lądu wokół mapy; `--gpu-check` „chunk: moved map” = bit w bit. TerrainGen: roślinność, trawa i cienie na mapie częściowej z chunków ograniczonych do
  jej prostokąta (`ChunkWorld::SetBounds`), więc te same rośliny co w świecie nieskończonym (checkbox „Plants, grass and shadows”). Eksport:
  `grid.worldOrigin` w manifeście, roślinność i woda rzek względem teksela (0, 0) mapy (test „moved over the world”). Do sprawdzenia w aplikacji:
  koszt chunków przy edycji na mapie częściowej; cień terenu tuż za krawędzią mapy (chunk na krawędzi rzuca go w całości).
  **Do zrobienia (2026-11-20, kolejność zalecana; punkt 1 zrobiony, punkt 2 porzucony — kolejny zapytać przed startem):**
  1. **Położenie mapy w presecie / zakładki / „Load from pack”** ✅ 2026-11-20 (`d08b101`), oba tryby, zakładka Presets.
     (a) preset: opcjonalne `"mapOffset": [x, y]` (stare presety bez niego czytają się jak dotąd), checkbox „Map position” (zapis i stosowanie przy
     wczytaniu; w świecie nieskończonym zapisuje mapę pod kamerą); (b) „Places”: nazwane miejsca (kamera przed Model scale + yaw/pitch + offset)
     w `places/world.json` / `places/noise.json`, „Go” / dwuklik = kamera i mapa na miejsce, ta sama nazwa = zastąpienie bieżącym widokiem;
     (c) „Export packs” → „Load from pack”: paczki z `exports/<nazwa>/<nazwa>.json`, parametry z `generation` (sprawdzany tryb paczki), położenie
     z `grid.worldOrigin` (paczki sprzed niego = początek świata). Preset i paczka bez kamery: kamera wysoko nad środkiem mapy (`LookDownAtMap`).
     Do sprawdzenia w aplikacji: czy kadr po wczytaniu jest wygodny; miejsce zapisane z „Follow camera” wyłączonym, a wczytane z włączonym, przesuwa mapę pod kamerę.
  2. ~~**Eksport kafli N × M**~~ ❌ porzucony 2026-11-20 decyzją właściciela: większy obszar = większa mapa częściowa (do 8192 × 8192 tekseli, region rzek
     domyślnie też 8192), jedna paczka bez szwów. Gdyby wrócił: region rzek zamrożony na środku całej siatki (dziś liczony od nowa przy każdym przesunięciu
     mapy → różne rzeki w sąsiednich kaflach), krok `W − 1`, maszyna stanów na wiele klatek (generacja w `Update`, siatki i roślinność w tle), a siatki LOD 1+
     potrzebują `meshopt_SimplifyLockBorder` (dziś krawędzie sąsiednich paczek upraszczają się różnie → szczeliny).
  3. **Region rzek podążający za kamerą w świecie nieskończonym** 🟡 2026-11-20 (branch `feat/rivers-follow-camera`, wariant A z trzech: skokowe
     przesuwanie / kafle sieci z zakładką / przenikanie dwóch sieci — odrzucone, bo daje podwójne płytkie koryta). Region (`regionSize`, domyślnie 8192 teksele)
     w świecie nieskończonym przeskakuje, gdy kamera odejdzie od jego środka dalej niż krok siatki w X lub Z; nowy środek = pozycja mapy pod kamerą
     przyciągnięta do stałej siatki (krok = ¼ regionu w dół do całych komórek, `hydrology::RegionStep` / `SnapRegionCenter` / `LeftRegion`, testy
     „Region centre”), więc to samo miejsce daje tę samą sieć, a po przeskoku kamera jest ≤ pół kroku od środka (bez skakania tam i z powrotem).
     Sieć liczy się w tle, po przyjściu chunki i roślinność odświeżają się istniejącym mechanizmem (od najbliższych, stare rysowane do podmiany).
     Mapa częściowa bez zmian (region wokół jej środka). Rivers → Region: „Follow camera (infinite world)” (domyślnie wł., nie w presecie), „Region centre”.
     Do sprawdzenia w aplikacji: czas przeskoku (Rivers: czas sieci + Output: czas do odświeżenia chunków po zmianie) i czy różnice rzek w części
     wspólnej regionów są akceptowalne; na granicy chunków starych i nowych rzeka może się chwilę urywać.
  4. 🟡 **Erozja na mapie częściowej TerrainGen** (ta sama `GpuErosion`). Erozja w świecie nieskończonym ❌ odrzucona 2026-11-20 decyzją właściciela:
     erozja to symulacja, nie ma jej w deterministycznej generacji świata proceduralnego, spowalnia generowanie i psułaby szwy chunków (krople wędrują,
     rury propagują przez iteracje, krople nie są bit w bit powtarzalne).
     Zrobione 2026-11-20: zakładka Erosion (metoda i parametry wspólne z Noise heightmap, „Erode map” kumuluje, „Reset”), parametry w presecie świata;
     kopia po erozji obok mapy (jak w Noise heightmap, podpisy map, widok 2D kadruje obie) z biomami i materiałami mapy, z morzem z wysokości kopii
     i rzekami/jeziorami sieci; rośliny rozmieszczane na kopii od nowa w tle (te same reguły co świat, więc nic nie stoi w nowych żlebach ani na stromiznach;
     powyżej 2048² tylko drzewa); eksport dopisuje `_height_eroded_<metoda>`. Każda zmiana terenu kasuje kopię.
     Nie objęte: trawa i cień terenu na kopii (z chunków), przeliczenie biomów z terenu po erozji.
     Do sprawdzenia w aplikacji: domyślne parametry przy skali 1024 (kroplom może brakować siły, rurom `Height scale` = 256), czas rozmieszczania roślin
     na dużej mapie, woda rzek wisząca nad korytem obniżonym przez erozję.
- **Tryb „Imported map”:** ✅ 2026-11-03 podgląd mapy z pliku (PNG 1/2/4/8/16-bit, kolorowy zamieniany na szary, oraz R16/RAW): `HeightmapImport.*` +
  `subSystems/MapImportSys.*`, wczytywanie w tle. Dalszy ciąg w Fazie 6.
- **Edytor ręczny:** pędzle (podnieś/obniż/wygładź/spłaszcz) jako warstwa delta nad proceduralną bazą.
- **Node graph** (imnodes) do składania szumów i operacji zamiast sztywnego continentalness/mountainousness/weirdness.

### Faza 6 — operacje na wczytanej mapie (otwarte)
Rozwinięcie trybu „Imported map”, który dziś tylko pokazuje mapę z pliku, w edytor terenu, także dla map ze świata rzeczywistego (decyzje właściciela
2026-11-20). Zasada: widoczna jest jedna mapa — kopia robocza, na której działają wszystkie zmiany (inaczej niż porównanie metod w Noise heightmap);
oryginał tylko w pamięci i w projekcie („Wróć do oryginału”), plus Cofnij/Ponów. Kolejność A → B1 → B2 → C, każdy etap osobny plan i branch.

- **A. Import map rzeczywistych (DEM):** GeoTIFF przez libtiff (vcpkg; 16/32 bit, kompresja, kafle; rozmiar piksela i NoData ze znaczników GeoTIFF),
  `.hgt` (SRTM) i `.asc` (ESRI ASCII) bez bibliotek; wysokości w metrach, rozmiar piksela z pliku lub ręcznie, prawdziwe proporcje + przewyższenie;
  NoData wypełniane z sąsiadów; wycinek i zmniejszenie dla plików ponad 8192²; mapy w stopniach — ostrzeżenie, bez przeliczania układów.
  🟡 2026-11-20 (branch `feat/import-dem`): `HeightmapImport` czyta `.tif/.tiff/.hgt/.asc` (`Inspect` = sam nagłówek, `ImportOptions` = zakres, wycinek,
  zmniejszenie), `DemTools` (NoData, wycinek, zmniejszenie, piksel w metrach); zakładka Load z wycinkiem, zmniejszeniem, rozmiarem piksela i zakresem
  „Custom (metres)” dla PNG/R16; Display: „Vertical exaggeration” + „True proportions”. Do sprawdzenia w aplikacji: prawdziwy kafel Copernicus/SRTM
  (GeoTIFF i `.hgt`), czas wczytania 3601², wygląd proporcji i przewyższenia.
- **B1. Projekt i przenoszenie:** plik projektu `.ptgmap` (nagłówek JSON + kopia robocza i oryginał float32 skompresowane zlib), Save (Ctrl+S) /
  Save as / Open project / Import map, gwiazdka i pytanie o niezapisane zmiany; „Send to Imported map” w TerrainGen i Noise heightmap (bieżąca mapa
  częściowa w metrach z poziomem morza, osobno kopia po erozji); zakładka Saving eksportuje kopię roboczą w metrach.
  🟡 2026-11-21 (branch `feat/map-project`): `MapProject.*` (format z testami: ucięty/uszkodzony plik, nowsza wersja, NaN), zakładka Project w prawym panelu
  Imported map (decyzja właściciela: nie pasek menu) z listą `projects/`, okno „Unsaved changes” przy Open/Load/przyjęciu mapy/zamknięciu okna (w każdym trybie),
  „Send map” / „Send eroded copy” w Saving obu trybów (mapa jak w eksporcie, przełącza tryb, kamera z góry), Saving w Imported map z prawdziwym pikselem
  (`metresPerTexel` stał się polem paczki: manifest, siatki; rozmiar kafla siatki w tekselach, z metrami w etykiecie). Poziom morza tylko zapisywany (woda w B2).
  Do sprawdzenia w aplikacji: czas zapisu/odczytu i rozmiar pliku 8192², kadr po Send/Open, okno „Unsaved changes” przy zamykaniu z innego trybu.
- **B2. Operacje, woda, rzeki:** erozja (parametry w metrach na piksel), wygładzanie, przeskalowanie wysokości, każda operacja krokiem historii
  (limit pamięci); poziom wody w metrach; rzeki i jeziora `hydrology::Compute` na kopii w tle, liczone od nowa po zmianie (bez rzeźbienia koryt).
  🟡 2026-11-21 (branch `feat/map-edit`): `MapEdit.*` (wygładzanie, skala, historia z limitem, testy), zakładka Edit w Imported map (erozja obiema metodami
  w jednostkach „piksel / 256”, wygładzanie, skala + przesunięcie w metrach, „Revert to original”, Undo/Redo + Ctrl+Z/Ctrl+Y, limit historii 256–8192 MB,
  poziom morza w metrach z wodą), `hydrology::ComputeOnMap` (rzeki i jeziora całej mapy w tle, wysokości w metrach), woda rzek w eksporcie paczki,
  ustawienia rzek w projekcie (`.ptgmap` wersja 2). Do sprawdzenia w aplikacji: wygląd erozji na DEM 30 m i na mapie wysłanej z TerrainGen, czas
  wygładzania i rzek na 8192², woda bez morza, Cofnij po erozji.
- **C. Modelowanie ręczne:** pędzle GPU (podnieś, obniż, wygładź, spłaszcz, szum) kursorem w 3D, promień/siła/zanik, pociągnięcie = krok historii.
  Wymaganie właściciela (2026-11-21): przy edycji potrzebne jest drugie sterowanie kamerą, jak w edytorach silników gier (Unity): obracanie widoku
  myszką bez przełączania klawiszem M (przytrzymany prawy przycisk = rozglądanie, z WASD = lot), orbita wokół punktu pod kursorem (Alt + lewy),
  przesuwanie (środkowy), przybliżanie kółkiem; lewy przycisk zostaje dla pędzla. Dzisiejsze sterowanie (M = mysz kamery) zostaje.
  🟡 2026-11-21 (branch `feat/map-sculpt`): `Sculpt.*` (pędzle na CPU = wzorzec, rzut kursora, testy), `GpuSculpt` + `sculpt_compute.shader` (kwadrat wokół
  pędzla, sekcja `sculpt:` w `--gpu-check` porównuje z CPU), sekcja „Sculpt” w zakładce Edit (5 pędzli, promień 1–512 px, siła w m/s lub tempo, twardość,
  skala szumu; LMB maluje w 3D i na mapie 2D, Ctrl odwraca podnieś/obniż, Shift + kółko = promień, kółko pędzla na terenie). Kamera edytora we wszystkich
  trybach (`EditorCamera` + `CameraMath.*` z testami; checkbox „Editor camera (mouse)” w zakładce Camera). Do sprawdzenia w aplikacji: odczucie pędzli
  (tempo, twardość, szum) na DEM i mapie z TerrainGen, Cofnij po pociągnięciu, kółko pędzla w 2D, orbita/przesuwanie/kółko kamery w każdym trybie,
  FPS malowania na 8192² (każda klatka liczy całą mapę pochodnych od nowa).
  Otwarte: mapa pochodnych tylko w kwadracie pędzla.
  ✅ 2026-11-27 kamera edytora w widoku 2D (wszystkie tryby): kółko = przybliżenie wokół kursora (od 32 tekseli na krótszym boku do 1,25 mapy), środkowy / prawy
  przycisk = przeciąganie, F albo „Fit 2D map” (Display) = cała mapa; świat nieskończony (decyzja użytkownika): kadr jak dotąd z ustawień renderowania, tylko
  przybliżenie w jego obrębie, bez przesuwania kamery. Powiększenie osobne w każdym trybie, reset przy nowym rozmiarze kadru (zmiana mapy, kopia po erozji,
  porównanie metod, wejście/wyjście ze świata nieskończonego, „Render distance”). Do sprawdzenia w aplikacji: pędzle i kropki roślin w 2D po przybliżeniu
  (Imported map), podpisy map w Noise, woda i rzeki w 2D, zmiana rozmiaru okna przy przybliżeniu, świat nieskończony w locie (przybliżenie zostaje).
- **D. Biomy i roślinność — decyzja po C.** Propozycja „klimat z terenu”: temperatura i wilgotność to szumy zależne tylko od położenia (da się je policzyć
  nad każdą mapą), brakujące kontynentalność/górzystość liczone z mapy wysokości (wysokość nad wodą / odległość od brzegu, lokalna rzeźba), weirdness
  neutralne; dalej istniejąca tabela, materiały i `vegexport::PlaceMap`. Alternatywa: import mapy pokrycia terenu (np. ESA WorldCover) jako id biomów.
  Decyzje właściciela (2026-11-22): oba źródła (klimat z terenu i plik pokrycia terenu) + pędzel biomów (biom z listy) + edytor biomów z TerrainGen;
  warstwy: źródło automatyczne przeliczane po każdej zmianie terenu/tabeli + malowana warstwa nadpisań na wierzchu (gumka = automat); roślinność
  z limitem (> 2 mln instancji → tylko drzewa, drzew > 2 mln → brak, z komunikatem); nowy `map_biome_compute.shader` obok `biome_compute.shader`.
  Etapy: D1 (klimat z terenu, zakładka Biomes, TEXTURED, projekt v3, eksport map biomów i materiałów) → D2 (pędzel + historia) → D3 (pokrycie terenu
  z georeferencją) → D4 (roślinność).
  🟡 D1 2026-11-22 (branch `feat/map-biomes`): `MapBiomes.*` (ustawienia z JSON, C = odległość od brzegu / „Coast distance”, M = rzeźba w promieniu,
  wysokość dla klimatu z metrów, testy; 8192² w ~3 s na CPU w tle), `map_biome_compute.shader` + `GpuHeightGenerator::GenerateMapBiomes` (sekcja
  `mapbiomes:` w `--gpu-check` porównuje z CPU), zakładka Biomes w Imported map (wspólne panele z TerrainGen `BiomeSettingsPanel`, edytor tabeli bez
  modyfikatorów terenu, „Load biomes from preset”), mapa wysłana z TerrainGen dostaje jego biomy, `.ptgmap` wersja 3, mapy splat/biomów w paczce eksportu.
  Do sprawdzenia w aplikacji: DEM z morzem i bez (czy suwaki C/M dają sensowne biomy), mapa wysłana z TerrainGen (biomy podobne do TerrainGen), TEXTURED,
  Cofnij po pędzlu przelicza biomy, Save → Open zachowuje ustawienia, eksport paczki z biomami (`_splat_*`, `_biome_ids`, `_biome_colors`, manifest).
  🟡 D2 2026-11-22 (branch `feat/map-biome-paint`): pędzel biomów, sekcja „Paint” zakładki Biomes. `BiomePaint.*` (paleta nazw ≤ 255, na teksel slot +
  siła 0–255, miękka krawędź `sculpt::Falloff`, B na A najpierw wygasza A, gumka = Ctrl, testy), mieszanie w `map_biome_compute.shader` (kolor i materiały
  wg siły, id od siły ≥ 128; nazwa spoza tabeli = automat; `--gpu-check`: paint matches / follows names / region equals the whole map), przebieg GPU
  tylko na prostokącie daba, pociągnięcie = krok historii (`mapedit::Step::paint`), „Clear painting”, `.ptgmap` wersja 4 (paleta w nagłówku, blok zlib).
  Do sprawdzenia w aplikacji: malowanie miękkie i ostre, A → B, gumka, Ctrl+Z/Y, zmiana nazwy i kolejności biomu w tabeli, Save → Open, eksport
  paczki z malowanymi biomami, płynność na 8192².
  🟡 D3 2026-11-22 (branch `feat/map-land-cover`): biomy z pliku pokrycia terenu. `GeoReference.*` (stopnie + Transverse Mercator: UTM 326xx/327xx,
  ETRS89 258xx, PUWG 1992 2180, PUWG 2000 2176–2179; szereg Krügera, testy), georeferencja z GeoTIFF / `.hgt` / `.asc` + `.prj`, ustawiana też po
  imporcie („Take georeference from file…”, system w combo), `.ptgmap` wersja 5. `LandCover.*`: klasa piksela mapy = najczęstsza z k × k próbek
  (k 1–4), kilka plików (pierwszy wygrywa), bez georeferencji / w obcym systemie rozciągnięcie pierwszego pliku. Klasa ogranicza biomy (maska),
  klimat wybiera najbliższy z dozwolonych (`biome::Nearest/MixColor/MixMaterials` z maską = `map_biome_compute.shader`, `--gpu-check`: land cover limits
  the biomes / empty masks / paint over land cover), malowanie na wierzchu. Sekcja „Land cover” zakładki Biomes (źródło, pliki ze stanem, tabela klas
  WorldCover z biomami), liczenie w tle, ścieżki plików też względne do projektu, tryb wyświetlania LAND COVER.
  Do sprawdzenia w aplikacji: WorldCover na styku dwóch kafli, NMT w PUWG 1992 z WorldCover (EPSG:4326), `.asc` bez `.prj` (system z combo), stary
  projekt + „Take georeference from file”, tryb LAND COVER (kolory klas, zmiana koloru), eksport paczki z biomami z pokrycia, czas mapy klas na 8192².
  🟡 D4 2026-11-23 (branch `feat/map-vegetation`): roślinność. Decyzje właściciela: prawdziwa skala piksela (komórki 8 / 4 / 8 m i rozmiar drzew
  w metrach, na DEM 30 m drzewa małe względem piksela i liczne) i siatka komórek w metrach. Rdzeń (`VegetationPlacement`): `ChunkMaps::step` = metry
  na teksel map (TerrainGen 1 / 2^L — bit w bit jak przedtem, testy z przypiętym wynikiem), przeliczenie wysokości dla elewacji roślin i pozycji/poziomów
  sieci rzek, `PreparePlants` raz na mapę. `vegexport::PlaceMap`: piksel dowolny (chunki 128 · ⌊piksel⌋ m), nachylenie z wysokości bez `surfaceMap`,
  wiersze chunków na wątkach, `PlaceMapLimited` z limitem 2 mln (najpierw drzewa; drzew więcej → brak, razem więcej → same drzewa; przerwanie po
  progu). Imported map: własny `VegetationLayer` (zestaw „extra”, `SetUnitsPerMetre` = 4 / piksel dla LOD i zasięgów), sadzenie w tle po każdym
  przebiegu biomów / nowej sieci rzek / zmianie roślin tabeli, Density, „Plants” (nic w trakcie pociągnięcia; wynik starszej wersji odrzucany),
  elewacja jak w biomach mapy, miejsce komórek z „Climate origin” (mapa z TerrainGen = te same drzewa), rysowane po odczycie punktu pod kursorem;
  sekcja „Vegetation” zakładki Biomes; `.ptgmap` wersja 6 („Plants”, Density); eksport `_vegetation.json` tym samym wejściem i limitem.
  Bez cieni i trawy (wymagają `ChunkWorld`), nie w 2D.
  Do sprawdzenia w aplikacji: mapa wysłana z TerrainGen (1 m) — drzewa jak w TerrainGen w tym miejscu; wycinek DEM 30 m 512² — małe drzewa w lasach
  biomów, czas sadzenia; pełny kafel SRTM — komunikat limitu i czas do niego; Sculpt / Paint lasu → rośliny wracają po puszczeniu; Undo; morze w górę
  → nic pod wodą; rzeki wł./wył.; Save → Open zachowuje „Plants” i Density; eksport paczki — liczba roślin jak w statusie Biomes, pozycje do (W−1)·piksel.
  Dalej (2026-11-23, prośba właściciela): własna zakładka „Vegetation” (po Biomes) z roślinami automatycznymi, pędzlem roślin i listą modeli z podglądem
  3D (ta sama sekcja co w lewym panelu). Pędzel: zaznaczone modele, Count 1 = jedna roślina pod kursorem, więcej = tyle prób w kole na każde
  dotknięcie; promień i odstęp w metrach, skala min–max, Ctrl gumka, Shift + kółko promień, jedno pociągnięcie = jeden krok Undo, „Clear placed plants”.
  Rośliny z pędzla to osobna warstwa (`placedplants::Layer`: paleta nazw modeli + pozycje w metrach), niezależna od przesadzania automatycznych,
  bez limitu 2 mln; stoją na bieżących wysokościach (po Sculpt na nowym terenie), w historii, w `.ptgmap` wersja 7 i w eksporcie (`Input::extra`).
  Do sprawdzenia: jedna roślina kliknięciem; rozsypanie 3 modeli; gumka; Undo/Redo pędzla między krokami Sculpt; Sculpt pod roślinami; Save → Open;
  eksport — liczba = automatyczne + z pędzla; rośliny z pędzla przy wyłączonych „Plants” i biomach; nieznany model w projekcie nie psuje wczytania.
  Poprawki drobnych usterek D4 i pędzla (2026-11-23): pociągnięcie pędzla roślin nad panelem nie przerywa sadzenia; reload tego samego pliku (zakres
  wartości) zostawia rośliny z pędzla; w widoku 2D rośliny z pędzla jako zielone kropki; uszkodzona liczba roślin w nagłówku projektu odrzucana przed
  alokacją (zlib ≤ ~1032 : 1); zamknięcie okna przerywa sadzenie automatyczne; sortowanie 2 mln roślin w zadaniu w tle, nie w klatce; chunki
  `ChunkMetres` ≈ 128 pikseli także przy pikselu < 1 m (zdjęcia z drona); pozycje względem teksela (0, 0) mapy (`PlacementSettings::gridOrigin`) — centymetry
  także tysiące km od początku świata. Hash bitowy „pinned output of a map of 1 m pixels” przypięty od nowa (ostatnie bity floatów), hash pozycji zaokrąglonych
  do cm (jak w pliku) bez zmian; rośliny TerrainGen bit w bit bez zmian.
  Do sprawdzenia: przeciągnięcie pędzla roślin przez prawy panel; zmiana zakresu wartości PNG przy roślinach z pędzla i pustej historii; kropki w 2D
  (przesuwanie, zoom, Undo); zamknięcie okna w trakcie sadzenia na 8192²; płynność klatki, gdy kończy się sadzenie na 8192².
  Odłożone drobne usterki, paczka A (2026-11-25, branch `fix/deferred-minors`, plan docs/superpowers/plans/2026-11-25-deferred-minors-a.md):
  projekt `.ptgmap` — blok za mały na mapę / malowane biomy i suma bloków większa niż plik odrzucane przed alokacją, brak pamięci = komunikat,
  niesprawdzony `seaLevel` (inf) odrzucany; edycja, która nie powiedzie się w połowie (brak pamięci), nie zostawia pustego kroku Cofnij
  (`mapedit::History::DropLast`), wygładzanie / skala / „Revert to original” bez pamięci już nie zamykają aplikacji; eksport z Imported map czeka na aktualną
  sieć rzek (woda rzek i rośliny automatyczne); przeładowanie tego samego pliku (zakres wartości) zachowuje przewyższenie; „Eroding…” nie wisi, gdy erozja
  trafi na trwającą operację; pędzle: lewy przycisk przy wciśniętym prawym / środkowym (kamera edytora) nie maluje, Esc przerywa pociągnięcie rzeźby
  (mapa sprzed niego, bez kroku historii); TerrainGen: rośliny kopii po erozji przesadzane po zmianie gęstości / modeli / roślin (`VegetationLayer::SettingsVersion`);
  zamknięcie okna chowa je od razu, zamknięcie zminimalizowanego z niezapisanym projektem przywraca okno z pytaniem; Godot: brak pliku splat = ostrzeżenie
  i równa mieszanka materiałów zamiast czarnego terenu, brakujące tekstury bez błędów silnika, ostrzeżenie o modelach tylko, gdy jakichś brakuje; Unity:
  powtórny import znajduje też wyłączony obiekt główny.
  Do sprawdzenia: Esc w trakcie rzeźbienia i następne pociągnięcie; lewy przycisk podczas rozglądania prawym; eksport zaraz po edycji z rzekami (notka pod
  „Water”, potem woda w paczce); zmiana zakresu PNG przy przewyższeniu ≠ 1; suwak Density przy pokazanej kopii po erozji; zamknięcie z paska zadań
  zminimalizowanego okna z niezapisanym projektem; zamknięcie w trakcie wczytywania modeli.

  Odłożone drobne usterki, paczka B — duże mapy (2026-11-25, branch `fix/big-map-perf`, plan docs/superpowers/plans/2026-11-25-big-map-perf-b.md):
  rośliny kopii po erozji biorą nachylenie z wysokości zamiast odczytu mapy pochodnych (16 B/teksel = 1 GB przy 8192²); odczyty z GPU bez czekania
  (`AsyncRead`: bufor pikseli + fence) — próbka terenu rzek REGION przy skoku regionu (2048²: 12,1 ms w klatce → 1,8 ms na samo wysłanie) i głębia
  pod kursorem pędzli Imported map (wcześniej co klatkę `glReadPixels` czekał na narysowanie klatki; teraz teksel z odczytu sprzed 1–2 klatek);
  mapa pochodnych przy rzeźbieniu liczona tylko w kwadracie dabu poszerzonym o 2 (`GpuSurfacePass::RunRegion`, `TerrainView::LayerTextureRegion`):
  4096² cała 1,40 ms, dab 257² 0,04 ms; wciśnięcie pędzla rzeźby nie kopiuje mapy (stan sprzed pociągnięcia = mapa CPU, `Busy()` jej nie zmienia);
  pokrycie terenu z pliku zapisanego jednym wielkim pasem (> 16 MB) czytane wierszami zamiast dekodowania całego pasa.
  Bez zmian (świadomie): chunki przy skoku regionu rzek (zasięg widoku < region, więc w widoku i tak wszystko się zmienia), szczyt pamięci erozji
  w Imported map (wymaga przebudowy historii), odczyt głębi kamery edytora (tylko przy wciśnięciu przycisku).
  Do sprawdzenia: rzeźbienie dużej mapy (płynność, obrys pędzla nadąża, krawędź mapy bez artefaktów cieniowania), Cofnij po pociągnięciu, Esc w trakcie;
  skok regionu rzek przy latającej kamerze (bez szarpnięcia, potem nowa sieć); rośliny kopii po erozji omijają strome zbocza jak wcześniej;
  pokrycie terenu z dużego pliku GeoTIFF bez kafli.

  Odłożone drobne usterki, paczka C (2026-11-26, branch `fix/deferred-minors-c`): Cofnij / Ponów kroku samego morza (i malowania) nie kopiuje
  mapy w metrach ani nie wgrywa jej na GPU (`mapedit::History::UndoHoldsMap/RedoHoldsMap`; morze = jak `SetSeaLevel`); rośliny automatyczne
  nie rosną na pikselach < 1 cm (`vegexport::minPlantPixelMetres`, komunikat w zakładce Vegetation — okno chunka miało ponad 800 pikseli na wątek);
  tekstury materiałów dzielących folder kopiowane do paczki i wpisywane do manifestu raz; obrócony / pochylony GeoTIFF (ModelTransformation)
  mówi, czemu nie ma georeferencji (zakładka Load, plik pokrycia terenu: „rotated or sheared GeoTIFF”); F5 w TerrainGen przeładowuje shadery
  erozji; „Region centre” w Rivers pokazuje sieć na ekranie, a liczona sieć osobno („Computing the network at…”); mapa wysłana z TerrainGen/Noise
  przełącza tryb dopiero po przyjęciu (Cancel w pytaniu o niezapisane zmiany zostawia w bieżącym trybie); suwak „Height scale” w Imported map
  obejmuje prawdziwą skalę mapy (płaski DEM 90 m ma ok. 2); „WorldCover defaults” pyta przed zastąpieniem zmienionych klas; kropki roślin
  z pędzla w 2D przerzedzane tylko spośród widocznych (przybliżony widok pokazuje wszystkie); importer Unity ustawia `treeDistance` z `lodRanges`.
  Odrzucone po przeczytaniu: slot palety pędzla biomów po kliknięciu bez zmiany i paleta, która nie maleje (tabela ma ≤ 64 biomy, paleta 255
  nazw); stare pliki `.res` Godota z poprzedniego importu (nic ich nie używa, a kasowanie plików w projekcie użytkownika jest groźniejsze).
  Do sprawdzenia: Cofnij zmiany morza na dużej mapie (bez przycięcia, rzeki i klimat od nowa); F5 przy kopii po erozji; skok regionu rzek
  (etykieta); „Send map” z niezapisanym projektem — Cancel / Discard / Save; płaski DEM 90 m i suwak Height scale; WorldCover defaults po
  edycji klasy; przybliżony widok 2D wielu roślin z pędzla; import paczki w Unity (rośliny znikają na odległości z aplikacji).

---

## 5. Kolejność i priorytety

| Priorytet | Zadania | Dlaczego |
|-----------|---------|----------|
| P0 | Faza 0 (B13, B3, B2, B1, B11) | Bez tego każda dalsza praca jest na niepewnym gruncie |
| P1 | Faza 1: ~~B8 (szum)~~ ✅, ~~A2/B5 (RAII + width/height)~~ ✅, ~~A3/A4 (rozdzielenie warstw)~~ ✅, ~~B6 (shadery)~~ ✅, ~~B15 (tekstury)~~ ✅, ~~wielowątkowość (7a)~~ ✅, ~~generacja asynchroniczna (7b)~~ ✅, ~~pomiary~~ ✅ | Fundament — bez rozdzielenia warstw nie da się przenieść generacji na GPU |
| P1 | Faza 2.1–2.3 (compute height + chunki) | Największy zysk dla „real-time” |
| P2 | ~~Faza 2.4–2.7~~ ✅ (2.6, 2.7 niepotrzebne), ~~Faza 3~~ ✅ | Jakość i skalowanie |
| P3 | Faza 4, Faza 5, Faza 6 | Feature'y budowane na stabilnym fundamencie |

Sugerowany pierwszy krok po Fazie 0: **zmierzyć** (Tracy) czas `TerrainGenerator::GenerateTerrain` dla 512²,
naprawić B8 i zmierzyć ponownie — da to konkretny obraz, ile zostało do zyskania na CPU zanim przejdziemy na GPU.
