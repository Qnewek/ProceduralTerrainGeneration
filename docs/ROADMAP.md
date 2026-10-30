# Procedural Terrain Generator — analiza i plan rozwoju

Diagnoza na 2026-10-27 (branch `lodChunked` = `master`, commit `fbd32ab`); postęp odhaczany na bieżąco, ostatnia aktualizacja 2026-10-30
(Faza 0 zamknięta; z Fazy 1 zrobione: A2/B5/B9, B8, A3, A4, B6 + shadery, B15, 7a/7b — generacja równoległa i asynchroniczna).
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
| B7 | `TerrainGenerationSys::Draw`, `NoiseBasedGenerationSys::Draw` | Tryb infinite: regeneracja **całej** mapy + nowa tekstura z mipmapami przy każdym ruchu kamery, w wątku renderującym; biomy i podgląd szumów zawsze z originem (0,0); `MakeIsland` zależy od rozmiaru mapy | Spadki FPS, biomy nie pasują do terenu po przesunięciu, wyspa „jedzie” z kamerą |
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

### 1.2 Problemy architektury i jakości (A)

| ID | Problem | Dlaczego przeszkadza w celu projektu |
|----|---------|--------------------------------------|
| A1 | **Generacja wyłącznie na CPU, jednowątkowo, synchronicznie w pętli renderującej** | Edycja parametrów w czasie rzeczywistym jest możliwa tylko dla małych map; każdy suwak blokuje klatkę |
| ~~A2~~ ✅ | Ręczne zarządzanie pamięcią (`new[]`/`delete[]`, raw pointers), brak rule-of-5 | Błędy B9, trudne refaktory — **Zrobione 2026-10-29:** mapy w `Grid<T>` (`std::vector`) |
| ~~A3~~ ✅ | Logika generacji, stan UI i rendering wymieszane w subsystemach (`TerrainGenerationSys` = 420 linii UI+GL+logika) | Nie da się przenieść generacji na wątek/GPU ani testować bez okna — **Zrobione 2026-10-29:** config w `WorldConfig.h`, generatory bezstanowe względem configu, GL w `TerrainView` |
| ~~A4~~ ✅ | Duplikacja: dwa subsystemy z identycznym `Initialize/Draw/Resize`, dwa zestawy shaderów różniące się kilkoma liniami | Każdą poprawkę (np. B6) trzeba robić 2× — **Zrobione 2026-10-29:** wspólny `TerrainView`, jeden zestaw shaderów |
| A5 | Brak modelu „świata”: mapa = jeden prostokąt `width×height` przypięty do kamery; brak chunków, LOD, cache | Infinite terrain i duże światy są niewykonalne wydajnie |
| A6 | Biomy oparte o dyskretne poziomy `int` i pierwszy pasujący biom z `unordered_map` (kolejność nieokreślona!) | Ostre granice, niedeterministyczny wybór przy nakładaniu się zakresów, brak blendingu, trudne do konfiguracji |
| A7 | Brak serializacji (presety, konfiguracje biomów, eksport) — „Saving” to placeholder | Nie da się zapisać efektu pracy ani porównać ustawień |
| A8 | Brak testów, CI, profilera; brak menedżera zależności (binarki w repo) | Regresje niewykrywalne; aktualizacje bibliotek ręczne |
| A9 | Martwy kod: `VegetationGenerator` (pusty), CPU-mesh w `utilities.cpp` (`MapToVertices`, `PerformErosion`, `PaintVerticesByHeight`, `MeshIndicesStrips`, `CalculateHeightMapNormals`), `TINYOBJLOADER_IMPLEMENTATION` bez użycia, `DrawTriangleStrips`, gałęzie git bez scalenia | Szum poznawczy |
| A10 | Higiena repo: śledzony `imgui.ini`, commity „sda/asd/x”, 17 zdalnych branchy, README z pustymi sekcjami | Trudno wrócić do projektu po przerwie (jak teraz) |

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

1. **A8 Build system:** CMake (+ `CMakePresets.json`), zależności przez vcpkg manifest lub FetchContent
   (glfw, glew lub glad, glm, imgui, implot, stb). Usunąć binarki z repo. GitHub Actions: build Windows (+ Linux opcjonalnie).
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
8. **Pomiar:** Tracy profiler (CPU + GPU zones) lub przynajmniej `GL_TIME_ELAPSED` queries + panel statystyk
   (czas generacji, czas klatki, liczba trójkątów). Częściowo ✅: `--benchmark` (headless, z checksumem) i czas
   generacji w panelu Output. Brak: GPU timing, liczba trójkątów, Tracy.
9. **Testy:** doctest/Catch2 — determinizm szumu (ten sam seed → te same wartości), zakresy [-1,1],
   wybór biomu dla zadanych parametrów, regresja height = f(x,z).
10. **Serializacja (A7):** presety JSON (nlohmann/json) dla `WorldConfig`; load/save w UI; eksport heightmapy do PNG 16-bit / RAW / EXR.
11. **A9/A10:** usuń martwy kod, posprzątaj branche (zostaw `master` + aktywny feature branch).

**Kryterium ukończenia:** mapa 1024² regeneruje się po ruchu suwaka bez blokowania UI; wszystkie testy zielone w CI.

---

## 4. Plan rozwojowy

### Faza 2 — generacja na GPU + chunki/LOD (główny skok wydajności, 2–4 tygodnie)
Cel: **edycja w czasie rzeczywistym przy 60+ FPS** i nieskończony świat.

1. **Compute shader height pass:** port szumu (fBm, ridged, domain warping) i ewaluacji splajnów do GLSL
   (splajny jako 1D LUT-tekstury `R32F`, 256–1024 próbek — przeliczane na CPU przy edycji punktów).
   Wyjście: `R32F` heightmap per chunk. Mapa 2048² w < 2 ms na średnim GPU → suwaki działają „na żywo”.
2. **Normal/derived pass:** compute liczący normal map + slope + curvature (potrzebne do teksturowania i roślinności).
3. **ChunkManager:** siatka chunków (np. 256×256 próbek, 1 próbka = 1 m), współrzędne świata, generacja tylko nowych chunków
   w kolejce priorytetowej (odległość od kamery + frustum), cache LRU, budżet ms/klatkę.
   Usuwa „kotwiczenie” kamery i regenerację całej mapy (**B7**, **A5**). Do rozważenia: floating origin dla dużych współrzędnych.
4. **LOD:** quadtree chunków (lub geometry clipmaps) + teselacja w TCS oparta o rozmiar krawędzi w pikselach ekranu
   (zamiast stałych `MIN/MAX_DISTANCE`); morfowanie/skirts przeciw szczelinom między poziomami.
5. **Culling:** frustum culling chunków na CPU + odrzucanie patchy w TCS (`gl_TessLevelOuter = 0`), opcjonalnie
   `glMultiDrawArraysIndirect` dla wszystkich chunków jednym wywołaniem.
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

### Faza 3 — biomy 2.0 (2–3 tygodnie)
Cel: realistyczne, płynne przejścia i biomy wpływające na kształt terenu.

1. **Ciągła przestrzeń parametrów** zamiast dyskretnych poziomów: każdy biom = punkt/obszar w przestrzeni
   (temperature, humidity, continentalness, erosion/mountainousness, weirdness); wybór = najbliższy biom
   (jak multi-noise Minecraft 1.18) — deterministyczne, bez zależności od kolejności w `unordered_map` (**A6**).
2. **Blending:** wagi K najbliższych biomów (np. 4) zapisywane do tekstury `RGBA8` (ID) + `RGBA8` (wagi);
   płynne przejścia kolorów/tekstur/parametrów wysokości.
3. **Biomy modyfikują teren:** per-biom modyfikatory (amplituda, offset, ridging, tarasowanie, wydmy)
   mieszane wagami — np. pustynia z wydmami, góry ostrzejsze, bagna spłaszczone.
4. **Wpływ wysokości na klimat:** temperatura spada z wysokością (lapse rate), wilgotność rośnie przy wybrzeżu
   (odległość od oceanu z continentalness) → śnieg na szczytach, plaże przy morzu bez osobnych reguł.
5. **Teksturowanie:** splat mapping (texture arrays: trawa, skała, piasek, śnieg, ziemia), reguły per biom +
   slope/height (skała na stromych zboczach), triplanar mapping dla klifów, detail normal maps, mieszanie height-based.
6. **Edytor biomów w UI:** tabela biomów (dodaj/usuń/edytuj), wizualizacja Whittaker-diagramu (ImPlot: temp × humidity),
   podgląd mapy biomów 2D, zapis do JSON.

**Kryterium:** brak ostrych granic biomów; zmiana definicji biomu widoczna natychmiast; konfiguracja biomów ładowana z pliku.

### Faza 4 — roślinność i obiekty (2–4 tygodnie)
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
- **Woda:** płaszczyzna oceanu z refleksją/refrakcją, głębia, piana przy brzegu.
- **Atmosfera:** fog zależny od wysokości, skybox/atmospheric scattering, cykl dnia, cienie (cascaded shadow maps).
- **Eksport:** heightmap 16-bit/EXR, splat mapy, mesh OBJ/glTF (z LOD), lista instancji roślinności (JSON) → import do Unity/Unreal/Godot.
- **Edytor ręczny:** pędzle (podnieś/obniż/wygładź/spłaszcz) jako warstwa delta nad proceduralną bazą.
- **Node graph** (imnodes) do składania szumów i operacji zamiast sztywnego continentalness/mountainousness/weirdness.

---

## 5. Kolejność i priorytety

| Priorytet | Zadania | Dlaczego |
|-----------|---------|----------|
| P0 | Faza 0 (B13, B3, B2, B1, B11) | Bez tego każda dalsza praca jest na niepewnym gruncie |
| P1 | Faza 1: ~~B8 (szum)~~ ✅, ~~A2/B5 (RAII + width/height)~~ ✅, ~~A3/A4 (rozdzielenie warstw)~~ ✅, ~~B6 (shadery)~~ ✅, ~~B15 (tekstury)~~ ✅, ~~wielowątkowość (7a)~~ ✅, ~~generacja asynchroniczna (7b)~~ ✅, profiler | Fundament — bez rozdzielenia warstw nie da się przenieść generacji na GPU |
| P1 | Faza 2.1–2.3 (compute height + chunki) | Największy zysk dla „real-time” |
| P2 | Faza 2.4–2.7, Faza 3 | Jakość i skalowanie |
| P3 | Faza 4, Faza 5 | Feature'y budowane na stabilnym fundamencie |

Sugerowany pierwszy krok po Fazie 0: **zmierzyć** (Tracy) czas `TerrainGenerator::GenerateTerrain` dla 512²,
naprawić B8 i zmierzyć ponownie — da to konkretny obraz, ile zostało do zyskania na CPU zanim przejdziemy na GPU.
