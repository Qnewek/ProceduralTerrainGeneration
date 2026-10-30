# CLAUDE.md — Procedural Terrain Generator

Kontekst dla nowej instancji Claude. Projekt: desktopowa aplikacja C++20 / OpenGL 4.5 do interaktywnego
generowania terenu (szum Simplex, erozja hydrauliczna, multi-noise terrain jak w Minecraft 1.18+, biomy).
UI w Dear ImGui + ImPlot. Projekt dyplomowy autora, rozwijany dalej. Właściciel pisze po polsku.

Plan napraw i rozwoju: `docs/ROADMAP.md` (ID zadań `B*`, `A*`, `F*` są tam zdefiniowane).

## Build i uruchomienie

- CMake (`CMakeLists.txt` + `CMakePresets.json` w katalogu głównym) + vcpkg w trybie manifestu (`vcpkg.json`,
  `builtin-baseline` = commit vcpkg z wersjami bibliotek). Tylko Windows x64, MSVC (VS 2022), generator Ninja,
  triplet `x64-windows-static` (statyczny CRT, jeden exe bez DLL). Presety: `x64-debug`, `x64-release`.
  Build trafia do `out/build/<preset>/` (ignorowane). Pierwszy configure buduje zależności vcpkg (~3 min).
- Build z CLI wymaga środowiska MSVC (Developer PowerShell / `VsDevCmd.bat -arch=x64`) i `VCPKG_ROOT`
  (VS ustawia go na swój vcpkg):
  ```
  cmake --preset x64-release
  cmake --build --preset x64-release
  ```
  W VS: *Open Folder* na katalogu repo — VS czyta presety sam.
- Targety: `ptg_vendor` (Simplex, stb_image impl., bez ostrzeżeń), `ptg_core` (`terrainGeneration/`, bez GL/ImGui —
  pod testy i benchmark), `ProceduralTerrainGenerator` (exe). `/W4 /WX /permissive-` dla `ptg_core` i exe;
  nagłówki zależności są `SYSTEM` (`/external:W0`), więc ich ostrzeżenia nie przerywają buildu.
- Shadery/tekstury czytane z `res/...` względem katalogu roboczego; jeśli go tam nie ma, `main` przechodzi do
  `PTG_RESOURCE_DIR` (= `ProceduralTerrainGenerator/` w źródłach, wpisany przez CMake). Hot-reload działa na źródłach.
- CI: `.github/workflows/build.yml` — Windows, oba presety, vcpkg na commicie z baseline (cache binarek
  w `actions/cache`), w Release porównanie checksum benchmarku. Brak testów jednostkowych (pkt 9 ROADMAP).
- Benchmark CPU bez okna: `out\build\x64-release\ProceduralTerrainGenerator.exe --benchmark`.
  Wypisuje medianę czasu szumów, terenu i biomów dla 256²/512²/1024² oraz **checksumy** heightmapy, map szumów
  i mapy biomów — przy refaktorach generacji wszystkie muszą się zgadzać (CI to sprawdza). Stan 2026-10-30
  (x64, 6 wątków), 512²: teren ≈ 37 ms, checksumy terrain `9ecec06113f1ff1d`, noises `d602f1e24734666d`,
  biomes `94a3305aac6164b0` (identyczne jak w dawnym buildzie Win32).
- **Wielkość liter ścieżek = dokładnie taka jak w gicie** (`git ls-files`): `src/opengl`, `src/subSystems`,
  `src/terrainGeneration`, `src/utility`, `src/vendor`, `res/shaders`, `res/textures`, `light_source_*.shader`.
  Dotyczy kodu (`#include`, `"res/..."`), ścieżek w `CMakeLists.txt` i nazw katalogów na dysku.
  Windows i git (`core.ignorecase=true`) nie wyłapią rozjazdu — wyjdzie dopiero na Linuksie.
- Nowy `.cpp` trzeba dopisać do odpowiedniego targetu w `CMakeLists.txt` (lista jawna, bez glob).
  Kod obcy, trzymany w repo (`src/vendor/`), idzie do `ptg_vendor` — tam nie ma `/W4 /WX`.
- Nowa biblioteka: dopisać do `vcpkg.json` i `find_package` + `target_link_libraries` w `CMakeLists.txt`.
  Aktualizacja wersji bibliotek = zmiana `builtin-baseline` (commit z github.com/microsoft/vcpkg).

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
      Grid.h, Parallel.h         mapy Grid<T>; ParallelFor po wierszach
      GenerationJob.h            jedno zadanie generacji w tle (std::async + TryTake)
    opengl/                      cienkie wrappery: Shader, VertexArray/Buffer, IndexBuffer, TextureClass, Renderer
      TerrainView.*              siatka + shader + tekstury (TERRAIN/OVERLAY/BIOMES), wspólny dla obu trybów
    utility/
      Camera.*                   kamera FPS (WASD, Space/Ctrl, M = mysz, Esc = zwolnij mysz)
      LightSource.*              punktowe światło + sześcian debug
      utilities.*                helpery ImGui (NoiseImGui, DisplayModeImGui...), siatka patchy, kolory biomów
    vendor/                      SimplexNoise (z przeciążeniami perm), tk::spline, implementacja stb_image
  res/shaders/
    TerrainShaders/              VS → TCS → TES → FS, jedyny zestaw dla terenu (oba tryby)
CMakeLists.txt, CMakePresets.json, vcpkg.json   build; GLFW, GLEW, glm, ImGui, ImPlot, stb z vcpkg
.github/workflows/build.yml      CI (Windows x64 Debug/Release + checksumy)
```

## Przepływ danych (jak to działa)

1. CPU generuje mapę `float[width*height]` (`SimplexNoiseClass::GenerateFractalNoise` lub
   `TerrainGenerator::GenerateTerrain`, który dla każdego piksela woła `Evaluate` trzech szumów); wiersze równolegle.
   Generacja idzie w tle (7b): UI ustawia flagi w `WorldRequest` / `NoiseRequest`, `Update()` subsystemu (co klatkę,
   przed `Draw`) startuje zadanie na kopii map i configu, a gotowy wynik podmienia mapy subsystemu (`generated`).
2. Subsystem przekazuje mapę do `TerrainView::SetLayer`, który wgrywa ją (`glTextureSubImage2D`) do istniejącej tekstury
   `GL_R32F` warstwy; nową teksturę (niezmienny storage, bez mipmap) tworzy tylko przy braku lub zmianie rozmiaru mapy.
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
  UI (immediate-mode w subsystemach: `ImGuiLeftPanel/RightPanel/Output`) edytuje config i zgłasza żądanie (`Request*`),
  subsystem generuje w tle i przekazuje wynik do `TerrainView`. Stan edytorów (wybrany szum, wciśnięte przyciski) to nie config.
- Includy własne po nazwie pliku (`#include "Noise.h"` — katalogi `src/*` są w include path targetów).
  Zależności z vcpkg bez podkatalogu (`"imgui.h"`, `"implot.h"`, `"stb_image.h"`, `<GL/glew.h>`, `"glm/glm.hpp"`),
  kod z `src/vendor` z podkatalogiem (`"Simplex/SimplexNoise.h"`, `"Splines/spline.h"`).
- Komentarze po angielsku, w stylu `//@param`.

## Pułapki — przeczytaj zanim zaczniesz zmieniać

- **Rozmiary zawsze jako `(width, height)`** (ujednolicone w B5) — nowe API też tak. Mapy niekwadratowe
  powinny działać; zmieniając generację sprawdź też taki przypadek.
- Każdy `SimplexNoiseClass` ma własną tablicę permutacji (`perm`), budowaną leniwie w `PointNoise` przy zmianie
  `config.seed`. Używaj przeciążeń `SimplexNoise::noise(perm, ...)`; globalne `reseed` / `noise(x, y)` zostały
  tylko dla zgodności. `PointNoise` aktualizuje permutację leniwie i **nie jest thread-safe** — w pętlach równoległych
  wołaj `UpdatePermutation(config.seed)` raz przed pętlą, a w środku `Evaluate` (const).
- **Mapy subsystemu (`generated`: `GeneratedWorld` / `NoiseMaps`) zmienia tylko instalacja wyniku zadania.** UI może je
  czytać, ale nie modyfikować bezpośrednio — zmiana zostałaby nadpisana przez wynik trwającego zadania (liczy na kopii
  sprzed zmiany). Każda modyfikacja map = flaga w żądaniu + krok w `RunGeneration`. Kod w `RunGeneration` działa na
  wątku roboczym: bez OpenGL, bez ImGui, bez pól subsystemu (jest `static`).
- Generacja map idzie wierszami przez `ParallelFor` (`terrainGeneration/Parallel.h`, `std::execution::par`).
  Iteracja może pisać tylko do swojego wiersza i czytać współdzielony stan; logowanie i błędy zbieraj po pętli
  (np. `std::atomic<bool>`), nie `return` ze środka.
- Splajny: domyślne są liniowe, a po „Set new spline points” edytowany splajn staje się kubiczny
  (`SplinePoints::cubic`) — zachowane z wersji sprzed A3. Benchmark sprawdza tylko LINEAR_COMBINE.
- Uniformy terenu ustawia wyłącznie `TerrainView::Draw` — subsystemy nie dotykają shadera. Wysokość bierze się
  z warstwy podanej w `Draw` (`heightLayer`: TERRAIN = jednostka 0, OVERLAY = 1 dla podglądu szumu / erozji).
- Oświetlenie w world space: `FragPos = model * p`, normalna z różnic skończonych z krokiem `texelWorldSize`
  (= `worldUnitsPerTexel`) przez `normalMatrix`. Zmieniając skalę siatki, zmień `worldUnitsPerTexel`, nie shader.
- Tryb „2D map” nie ma osobnej gałęzi w shaderze — `TerrainView` podmienia `view`/`projection` na ortogonalną
  kamerę z góry (+X w prawo, −Z w górę) kadrującą `SetFlatViewBounds` (domyślnie cała mapa).
- Shadery obsługują `#include "plik"` (ścieżka względna do pliku zawierającego, każdy plik raz na etap; wspólne
  w `res/shaders/common/`). F5 w oknie aplikacji przeładowuje wszystkie shadery — przy błędzie zostaje stary program,
  a błąd idzie do konsoli. Numery linii w błędach dotyczą pliku, w którym błąd jest, ale nazwa pliku nie jest podawana.
- MSVC/vcpkg nie znoszą ścieżek > 260 znaków — testowy clean build z eksportu repo rób w krótkiej ścieżce
  (np. `%TEMP%\ptg_cc`), nie w scratchpadzie.
- Planowane chunkowanie/LOD jeszcze nie istnieje. Wcześniejsza próba (klasa `Chunk`, nigdy nie scalona) jest pod tagiem
  `archive/infinite-terrain-chunks`; stara roślinność — w historii (`fb8b840`), opis w ROADMAP, Faza 4.

## Kierunek rozwoju (skrót z ROADMAP)

Cel: edycja parametrów w czasie rzeczywistym + maksymalna wydajność + biomy i roślinność.
Kolejność: naprawy krytyczne (Faza 0) → porządki/architektura (Faza 1) → generacja na GPU (compute shaders)
i system chunków z LOD (Faza 2) → biomy ciągłe z blendingiem i teksturowaniem (Faza 3) →
roślinność instancjonowana (Faza 4) → erozja GPU, woda, eksport (Faza 5).
