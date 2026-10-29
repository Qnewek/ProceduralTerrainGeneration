# CLAUDE.md — Procedural Terrain Generator

Kontekst dla nowej instancji Claude. Projekt: desktopowa aplikacja C++20 / OpenGL 4.5 do interaktywnego
generowania terenu (szum Simplex, erozja hydrauliczna, multi-noise terrain jak w Minecraft 1.18+, biomy).
UI w Dear ImGui + ImPlot. Projekt dyplomowy autora, rozwijany dalej. Właściciel pisze po polsku.

Plan napraw i rozwoju: `docs/ROADMAP.md` (ID zadań `B*`, `A*`, `F*` są tam zdefiniowane).

## Build i uruchomienie

- Tylko Windows + Visual Studio 2022 (MSBuild, toolset v143). Brak CMake.
- **Jedyne konfiguracje: `Debug|Win32` i `Release|Win32`** (x64 usunięte w B13 — `glfw3.lib` w repo jest 32-bit;
  x64 wróci z CMake + vcpkg w Fazie 1, A8).
- `.sln`, `.vcxproj`, `.vcxproj.filters` i `Dependencies/GLEW/lib/` są śledzone w gicie — świeży clone się buduje.
  `.gitignore` ma wyjątek `!Dependencies/*/lib/**`, bo reguły `[Rr]elease/`, `Win32/`, `x64/` łapałyby libki.
- Build z CLI:
  ```
  "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ProceduralTerrainGenerator.sln /p:Configuration=Debug /p:Platform=x86 /m
  ```
  Stan na 2026-10-28: `/W4 /WX` — każde ostrzeżenie kompilatora w `src/` (poza vendor) przerywa build.
  Zostają 2 ostrzeżenia linkera z binarek GLEW/GLFW (LNK4099 brak PDB, LNK4098 konflikt CRT) — do A8.
- Working directory przy uruchomieniu musi być `ProceduralTerrainGenerator/` — ścieżki do `res/...` są względne.
- Brak testów automatycznych i CI. Weryfikacja = build + ręczne uruchomienie.
- Benchmark CPU bez okna: `..\Release\ProceduralTerrainGenerator.exe --benchmark` (uruchamiane z katalogu `ProceduralTerrainGenerator/`).
  Wypisuje medianę czasu szumów i terenu dla 256²/512²/1024² oraz **checksum heightmapy** — przy refaktorach
  generacji checksum musi się zgadzać (stan 2026-10-28: 512² teren ≈ 227 ms, checksum `9ecec06113f1ff1d`).
- **Wielkość liter ścieżek = dokładnie taka jak w gicie** (`git ls-files`): `src/opengl`, `src/subSystems`,
  `src/terrainGeneration`, `src/utility`, `src/vendor`, `res/shaders`, `res/textures`, `light_source_*.shader`.
  Dotyczy kodu (`#include`, `"res/..."`), wpisów w `.vcxproj`/`.filters` i nazw katalogów na dysku.
  Windows i git (`core.ignorecase=true`) nie wyłapią rozjazdu — wyjdzie dopiero na Linuksie/CI.
  VS pokazuje w Solution Explorerze nazwy z `.vcxproj`, nie z dysku.
- Dodając nowy `.cpp`/`.h` trzeba go ręcznie dopisać do `.vcxproj` (i `.filters`).
  Nowy `.cpp` z `src/vendor/` musi dostać `<WarningLevel>TurnOffAllWarnings</WarningLevel>`, inaczej `/WX` go wywali.

## Struktura

```
ProceduralTerrainGenerator/
  src/
    Application.cpp              main(): new TerrainGenApp → Initialize → Start
    subSystems/
      TerrainGenApp.*            okno GLFW, pętla główna, layout 4 paneli ImGui, przełączanie trybów
      NoiseBasedGenerationSys.*  tryb "Noise heightmap": jeden szum + erozja (podgląd obok)
      TerrainGenerationSys.*     tryb "TerrainGen": 3 szumy + splajny + biomy, edytory ImPlot
    terrainGeneration/
      WorldConfig.h              same dane: WorldConfig (TerrainConfig + BiomeConfig), NoiseMapConfig, ErosionConfig
      Noise.*                    noise::SimplexNoiseClass + NoiseConfigParameters (fBm, ridge, island, redistribution)
      TerrainGenerator.*         continentalness/mountainousness/weirdness → heightMap (LINEAR lub SPLINE)
      BiomeGenerator.*, Biome.*  temperatura/wilgotność + 3 szumy terenu → poziomy (int) → id biomu
      Erosion.*                  erozja kroplowa (CPU, lista jednokierunkowa kropel)
      VegetationGenerator.*      PUSTE — stara implementacja zakomentowana
    opengl/                      cienkie wrappery: Shader, VertexArray/Buffer, IndexBuffer, TextureClass, Renderer
      TerrainView.*              siatka + shader + tekstury (TERRAIN/OVERLAY/BIOMES), wspólny dla obu trybów
    utility/
      Camera.*                   kamera FPS (WASD, Space/Ctrl, M = mysz, Esc = zwolnij mysz)
      LightSource.*              punktowe światło + sześcian debug
      utilities.*                helpery ImGui (NoiseImGui, DisplayModeImGui...) + dużo martwego kodu CPU-mesh
    vendor/                      glm, imgui, ImPlot, SimplexNoise, tk::spline, PoissonGenerator, stb_image, tiny_obj_loader
  res/shaders/
    TerrainShaders/              VS → TCS → TES → FS, jedyny zestaw dla terenu (oba tryby)
Dependencies/                    GLFW (lib vc2022), GLEW (static)
```

## Przepływ danych (jak to działa)

1. CPU generuje mapę `float[width*height]` (`SimplexNoiseClass::GenerateFractalNoise` lub
   `TerrainGenerator::GenerateTerrain`, który dla każdego piksela woła `PointNoise` trzech szumów).
2. Subsystem przekazuje mapę do `TerrainView::SetLayer`, który tworzy **nową** teksturę `GL_R32F` przy każdej zmianie (B15).
3. Siatka (`TerrainView`): stała siatka `mapResolution × mapResolution` (50×50) patchy-quadów o rozmiarze
   `width * worldUnitsPerTexel` (= 4, `GetWorldWidth()`), rysowana `GL_PATCHES` (4 wierzchołki).
4. TCS: poziom teselacji 4–64 zależnie od odległości w przestrzeni oka. TES: próbkuje `heightMap`,
   przesuwa wierzchołek o `height * heightScale`, liczy normalną z różnic skończonych, koloruje wg `displayMode`
   (0 grey, 1 topo, 2 mono, 3 biomy z `biomeMap`).
5. Tryb „infinite”: przy każdej zmianie XZ kamery regeneruje **całą** mapę z originem = pozycja kamery,
   a kamera jest „zakotwiczona” w (0, y, 0) (`Camera::CameraAnchor`).

Wartości szumów są w [-1, 1]. Konwencja indeksowania: `map[y * width + x]`.

## Konwencje kodu

- Klasy PascalCase, metody PascalCase, pola camelCase, parametry ctor/Initialize z prefiksem `_` (`_width`).
- Wzorzec: konstruktor domyślny + `bool Initialize(...)` + `Resize(...)`; błędy jako `return false` +
  `std::cout << "[ERROR] ..."`; logi `"[LOG] ..."`.
- Mapy jako `Grid<T>` (`terrainGeneration/Grid.h`: `HeightField`, `BiomeMap`) — bez surowych `new[]/delete[]`.
  Do OpenGL przez `.Data()`. OpenGL obiekty w `std::unique_ptr`.
- Warstwy (A3): parametry świata to same dane w `WorldConfig.h`, trzymane przez subsystem (`world` / `config`).
  Generatory (`SimplexNoiseClass`, `TerrainGenerator`, `BiomeGenerator`, `Erosion`) **nie trzymają configu** —
  dostają `const XxxConfig&` w każdej metodzie generującej, a same przechowują tylko wyniki (mapy).
  UI (immediate-mode w subsystemach: `ImGuiLeftPanel/RightPanel/Output`) edytuje config, subsystem woła generację
  i przekazuje wynik do `TerrainView`. Stan edytorów (wybrany szum, wciśnięte przyciski) to nie config.
- Includy względem katalogów z `AdditionalIncludeDirectories` (własne: `#include "Noise.h"`) albo
  `ExternalIncludePath` (`src/vendor`, GLFW, GLEW: `"imgui/imgui.h"`) — te drugie są kompilowane z `/external:W0`.
- Komentarze po angielsku, w stylu `//@param`.

## Pułapki — przeczytaj zanim zaczniesz zmieniać

- **Rozmiary zawsze jako `(width, height)`** (ujednolicone w B5) — nowe API też tak. Mapy niekwadratowe
  powinny działać; zmieniając generację sprawdź też taki przypadek.
- Każdy `SimplexNoiseClass` ma własną tablicę permutacji (`perm`), budowaną leniwie w `PointNoise` przy zmianie
  `config.seed`. Używaj przeciążeń `SimplexNoise::noise(perm, ...)`; globalne `reseed` / `noise(x, y)` zostały
  tylko dla zgodności. Przed zrównolegleniem `PointNoise` wywołaj `UpdatePermutation(config.seed)` raz przed pętlą
  (leniwa aktualizacja w środku nie jest thread-safe).
- Splajny: domyślne są liniowe, a po „Set new spline points” edytowany splajn staje się kubiczny
  (`SplinePoints::cubic`) — zachowane z wersji sprzed A3. Benchmark sprawdza tylko LINEAR_COMBINE.
- Uniformy terenu ustawia wyłącznie `TerrainView::Draw` — subsystemy nie dotykają shadera. Wysokość bierze się
  z warstwy podanej w `Draw` (`heightLayer`: TERRAIN = jednostka 0, OVERLAY = 1 dla podglądu szumu / erozji).
- Tryb „2D map” (`flatten`) w TES ignoruje macierz `model` — przesunięcia i skala nie działają w 2D (do B6).
- `FragPos` w TES jest w clip space, a pozycja światła w world space — oświetlenie jest błędne (B6).
- MSBuild nie znosi ścieżek > 260 znaków — testowy clean build rób w krótkiej ścieżce (np. `%TEMP%\ptg_cc`), nie w scratchpadzie.
- Branch `lodChunked` == `master` (brak własnych commitów) — planowane chunkowanie/LOD jeszcze nie istnieje.

## Kierunek rozwoju (skrót z ROADMAP)

Cel: edycja parametrów w czasie rzeczywistym + maksymalna wydajność + biomy i roślinność.
Kolejność: naprawy krytyczne (Faza 0) → porządki/architektura (Faza 1) → generacja na GPU (compute shaders)
i system chunków z LOD (Faza 2) → biomy ciągłe z blendingiem i teksturowaniem (Faza 3) →
roślinność instancjonowana (Faza 4) → erozja GPU, woda, eksport (Faza 5).
