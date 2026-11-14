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
- Targety: `ptg_vendor` (Simplex, stb_image impl., bez ostrzeżeń), `ptg_core` (`terrainGeneration/`: configi, presety, erozja,
  pliki map, bez GL/ImGui — pod testy), `ProceduralTerrainGenerator` (exe). `/W4 /WX /permissive-` dla `ptg_core` i exe;
  nagłówki zależności są `SYSTEM` (`/external:W0`), więc ich ostrzeżenia nie przerywają buildu.
- Shadery/tekstury czytane z `res/...` względem katalogu roboczego; jeśli go tam nie ma, `main` przechodzi do
  `PTG_RESOURCE_DIR` (= `ProceduralTerrainGenerator/` w źródłach, wpisany przez CMake). Hot-reload działa na źródłach.
- CI: `.github/workflows/build.yml` — Windows (`windows-2022` = VS 2022, jak lokalnie), oba presety, vcpkg na commicie z baseline
  (cache binarek w `actions/cache`), testy jednostkowe. Generacji CI nie sprawdza: działa tylko na GPU, a runnery GPU nie mają.
- Testy: `ctest --preset x64-release` (doctest, target `ptg_tests`, kod w `ProceduralTerrainGenerator/tests/`, `ptg_core` + nagłówki z `utility/`).
  Szczegóły doctesta: `out\build\x64-release\ptg_tests.exe`. Mapy testowe są niekwadratowe. Znany błąd = test z `doctest::should_fail()`
  i ID z ROADMAP; po naprawie błędu test zacznie zgłaszać niepowodzenie, wtedy usuń oznaczenie.
- **Test generacji: `out\build\x64-release\ProceduralTerrainGenerator.exe --gpu-check`** (ukryte okno, potrzebny GPU z OpenGL 4.5; uruchamiaj po każdej
  zmianie shaderów generacji). Sprawdza: wartości skończone i w zakresie opcji (każda opcja, ridge, wyspy, metody terenu), determinizm i wpływ seeda,
  zgodność id i kolorów biomów, chunki i poziomy LOD bit w bit z pełną mapą, mapę pochodnych na mapach o znanych pochodnych, udział wody domyślnego świata
  (55–65%, ≥ 80% lądu w kontynentach); potem czasy GPU.
  Nowa opcja generacji = nowy przypadek w `CheckRanges` (`GpuHeightCheck.cpp`).
- **Wielkość liter ścieżek = dokładnie taka jak w gicie** (`git ls-files`): `src/opengl`, `src/subSystems`,
  `src/terrainGeneration`, `src/utility`, `src/vendor`, `res/shaders`, `res/textures`.
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
      TerrainGenApp.*            okno GLFW, pętla główna, layout 4 paneli ImGui, przełączanie trybów; lewy panel = zakładki widoku
                                 (Display, Camera, Sun & sky, Materials, Presets, Saving, About), prawy = zakładki generacji subsystemu trybu
      NoiseBasedGenerationSys.*  tryb "Noise heightmap": jeden szum + erozja (podgląd obok)
      TerrainGenerationSys.*     tryb "TerrainGen": 3 szumy + splajny + biomy, edytory ImPlot
      MapImportSys.*             tryb "Imported map": podgląd mapy wysokości z pliku, wczytywanej w tle
    terrainGeneration/
      WorldConfig.h              same dane: WorldConfig (TerrainConfig + BiomeConfig), NoiseMapConfig, ErosionConfig
      Noise.h                    noise::NoiseConfigParameters (fBm, ridge, island, redistribution); sam szum jest tylko w GLSL
      Biome.*                    biom = zakresy 6 parametrów (5 szumów + elewacja) + kolor; najbliższy biom (Nearest) i wagi przejść (BlendWeight) — opis reguł, które liczy biome_compute.shader
      Erosion.*                  erozja kroplowa (CPU, lista jednokierunkowa kropel)
      Grid.h                     mapy Grid<T>
      SplineSampling.*           próbkowanie splajnu do tablicy (dla GPU i wykresu w edytorze)
      Frustum.h                  płaszczyzny frustum z macierzy i test AABB (culling chunków)
      Preset.*                   zapis/odczyt WorldConfig i NoiseMapConfig jako JSON + walidacja
      HeightmapExport.*          zapis mapy jako PNG 16-bit lub RAW R16; FileIO.* = zapis przez plik tymczasowy
      HeightmapImport.*          odczyt mapy z PNG lub R16/RAW; format po rozszerzeniu, nowy = czytnik i rozszerzenie w tym pliku
      GenerationJob.h            jedno zadanie generacji w tle (std::async + TryTake)
      LandStats.*                udział wody i masy lądu (flood fill) z rzadkiej próbki świata (GpuHeightGenerator::SampleTerrain); „Land statistics” w zakładce Terrain i sekcja land: w --gpu-check
    opengl/                      cienkie wrappery: Shader, VertexArray/Buffer, IndexBuffer, TextureClass, Renderer, GpuQuery
      TerrainView.*              siatka + shader + tekstury (TERRAIN/OVERLAY/BIOMES), wspólny dla obu trybów
      GpuHeightGenerator.*       jedyna generacja: szum, teren i biomy w compute shaderach (GenerateWorld = teren + biomy w jednym przebiegu); GpuHeightCheck.* = testy generacji (--gpu-check)
      NoisePreview.*             podgląd edytowanego szumu jako mapa 2D w panelu ImGui (≤ 512 tekseli na dłuższym boku, preview_compute.shader = skala szarości)
      GpuSurfacePass.*           mapa pochodnych (gradient + laplasjan) z mapy wysokości; uruchamia ją TerrainView i ChunkWorld
      ShadowMap.*                cienie słońca: cascaded shadow maps (teren + roślinność), dane kaskad w FrameBlock; WaterSurface.* = woda
      ChunkWorld.*               „Infinite generation”: quadtree chunków 128² z LOD wokół kamery, kolejka wg odległości, cache, skirts, rysowanie shaderem terenu
      TerrainMaterials.*         materiały terenu (tryb TEXTURED): lista z terrain_materials.json, texture arrays (albedo+wysokość, normal+roughness+AO; jednostki 4, 5)
                                 + UBO parametrów FS (binding 0), sekcja „Terrain materials”
    utility/
      Camera.*                   kamera FPS (WASD, Space/Ctrl, M = mysz, Esc = zwolnij mysz)
      LightSource.*              słońce: światło kierunkowe (azymut, wysokość), ambient, przełącznik mgły
      Sky.*                      tło widoku 3D: proceduralne niebo zależne od wysokości słońca + tarcza słońca (fullscreen triangle)
      FrameMetrics.*             pomiary w panelu Output (czas klatki/CPU/GPU, trójkąty); RollingStats.h = średnia i maks. z ostatnich próbek;
                                 FlightRecord.h = „Record flight” (prędkość, klatki > 20 ms, klatki z dziurą), pomiar kryterium Fazy 2
      PresetPanel.*              sekcja „Presets” w ImGui (lista plików, nazwa); zapis i odczyt wykonuje subsystem
      ExportPanel.*              sekcja „Saving” w ImGui; subsystem podaje mapy do zapisu i ich zakres wartości
      BiomeEditor.*              edytor tabeli biomów (lista, zakresy, diagram Whittakera T × H); edytuje tabelę w miejscu, subsystem przelicza biomy
      PanelTabs.*                zakładki panelu: rzędy przycisków na górze (zawijane, gdy się nie mieszczą) + przewijana treść pod nimi; wybór trzymany po nazwie
      FileDialog.*               systemowe okno wyboru pliku (tylko Windows, comdlg32); przy porcie na Linuksa do zastąpienia
      utilities.*                helpery ImGui (NoiseImGui, DisplayModeImGui...), siatka patchy, kolory biomów
    vendor/                      SimplexNoise (z niego tylko makePermutation: tablice permutacji dla GPU), stb_image
  tests/                         testy jednostkowe ptg_core (doctest)
  res/shaders/
    TerrainShaders/              VS → TCS → TES → FS, jedyny zestaw dla terenu (oba tryby)
    ComputeShaders/              height_compute.shader (szum, teren), biome_compute.shader, surface_compute.shader; wspólne: common/noise.glsl, noise_params.glsl, simplex.glsl, surface.glsl,
                                 terrain.glsl (wysokość terenu z C/M/W), world_input.glsl (wspólny bufor wejściowy), biomes.glsl (reguły biomów, klimat, modyfikatory terenu, mieszanie materiałów)
    common/materials.glsl        próbkowanie materiałów w trybie TEXTURED (triplanar, anti-tiling), blok TerrainMaterialBlock = TerrainMaterials::Block
    sky_vertex/fragment.shader   niebo; kolory nieba w common/sky.glsl, z których korzysta też mgła terenu
  res/textures/terrain_materials.json   lista materiałów (kolejność = id); same tekstury w res/textures/terrain/ (poza gitem)
tools/fetch_textures.py          pobiera tekstury materiałów (CC0, Poly Haven / ambientCG), tylko biblioteka standardowa Pythona
tools/fetch_models.py            pobiera modele roślinności i kamieni (CC0 glTF, Poly Haven) z mapami alpha liści
CMakeLists.txt, CMakePresets.json, vcpkg.json   build; GLFW, GLEW, glm, ImGui, ImPlot, stb z vcpkg
.github/workflows/build.yml      CI (Windows x64 Debug/Release, build + testy jednostkowe)
```

## Przepływ danych (jak to działa)

1. Aplikacja generuje mapy wyłącznie na GPU (`GpuHeightGenerator`). UI ustawia flagi w `WorldRequest` / `NoiseRequest`, a `Update()` subsystemu
   (co klatkę, przed `Draw`, w wątku głównym) uruchamia compute shader, który pisze wprost do tekstury warstwy `TerrainView` (`LayerTexture`).
   Biomy liczą w shaderze wszystkie 5 szumów, więc nie ma osobnych tekstur szumów ani zależności między krokami.
   Generacji na CPU nie ma (usunięta 2026-11-08; ostatnia wersja z nią: `56eae39`).
2. Mapy na CPU powstają tylko na żądanie przez `TerrainView::ReadLayer`: eksport i start erozji. Erozja nadal działa na CPU, w tle (`GenerationJob`),
   a wynik wgrywa `TerrainView::SetLayer`; tak samo mapa z pliku w trybie Imported map. Tekstury mają niezmienny storage, nowa powstaje przy zmianie rozmiaru.
3. Siatka (`TerrainView`): stała siatka `mapResolution × mapResolution` (50×50) patchy-quadów o rozmiarze
   `width * worldUnitsPerTexel` (= 4, `GetWorldWidth()`), rysowana `GL_PATCHES` (4 wierzchołki).
4. TCS: w 3D poziom teselacji z długości krawędzi na ekranie (`tessPixels`, suwak „Terrain detail”, 1–64), w widoku 2D po staremu 4–64 od odległości (ma zostać ostry). Normalna i kolor biomów liczone w FS na piksel (z `surfaceMap` / `biomeMap`). TES: próbkuje `heightMap`,
   przesuwa wierzchołek o `height * heightScale`, bierze normalną z `surfaceMap`, koloruje wg `displayMode`
   (0 grey, 1 topo, 2 mono, 3 biomy z `biomeMap`, 4 nachylenie, 5 krzywizna, 6 TEXTURED — materiały liczy FS z `splatMap`). `surfaceMap` (gradient + laplasjan surowej wysokości, `RGBA32F`)
   liczy `TerrainView::Draw` przez `GpuSurfacePass`, tylko gdy warstwa wysokości zmieniła się od ostatniego razu (`LayerTexture` oznacza ją jako nieaktualną).
5. Tryb „infinite” (TerrainGen i Noise heightmap) to `ChunkWorld`: w `Update(camera)` subsystem woła `Plan` (chunki w zasięgu „Render distance”)
   i `GenerateNext` z własną funkcją generującą (`GpuHeightGenerator` z originem chunka i `islandSize` = rozmiar mapy), a `Draw` rysuje chunki zamiast `TerrainView`.
   Każde żądanie (`Request*`) w tym trybie unieważnia wszystkie chunki; skończona mapa jest odświeżana po powrocie do „Partial generation”. Eksport i erozja działają tylko na skończonej mapie.
   Pozycja mapy p (teksele) leży w świecie w `mapOrigin + p * worldUnitsPerTexel`; `mapOrigin` ustawia świat tak, że pokrywa skończoną mapę (`AlignWithMap`).

Wartości szumów są w [-1, 1]. Konwencja indeksowania: `map[y * width + x]`.

## Konwencje kodu

- Klasy PascalCase, metody PascalCase, pola camelCase, parametry ctor/Initialize z prefiksem `_` (`_width`).
- Wzorzec: konstruktor domyślny + `bool Initialize(...)` + `Resize(...)`; błędy jako `return false` +
  `std::cout << "[ERROR] ..."`; logi `"[LOG] ..."`.
- Mapy jako `Grid<T>` (`terrainGeneration/Grid.h`: `HeightField`, `BiomeMap`) — bez surowych `new[]/delete[]`.
  Do OpenGL przez `.Data()`. OpenGL obiekty w `std::unique_ptr`.
- Warstwy (A3): parametry świata to same dane w `WorldConfig.h`, trzymane przez subsystem (`world` / `config`).
  Generatory (`GpuHeightGenerator`, `Erosion`) **nie trzymają configu** — dostają `const XxxConfig&` w każdym wywołaniu.
  UI (immediate-mode w subsystemach: `ImGuiDisplay/Presets/Saving`, `ImGuiRightPanel`, `ImGuiOutput`) edytuje config i zgłasza żądanie (`Request*`),
  subsystem generuje w tle i przekazuje wynik do `TerrainView`. Stan edytorów (wybrany szum, wciśnięte przyciski) to nie config.
- Includy własne po nazwie pliku (`#include "Noise.h"` — katalogi `src/*` są w include path targetów).
  Zależności z vcpkg bez podkatalogu (`"imgui.h"`, `"implot.h"`, `"stb_image.h"`, `<GL/glew.h>`, `"glm/glm.hpp"`),
  kod z `src/vendor` z podkatalogiem (`"Simplex/SimplexNoise.h"`).
- Komentarze po angielsku, w stylu `//@param`.

## Pułapki — przeczytaj zanim zaczniesz zmieniać

- **Rozmiary zawsze jako `(width, height)`** (ujednolicone w B5) — nowe API też tak. Mapy niekwadratowe
  powinny działać; zmieniając generację sprawdź też taki przypadek.
- Tablica permutacji szumu zależy tylko od seeda (`SimplexNoise::makePermutation`, test z ustalonymi wartościami w `PermutationTests.cpp`);
  zmiana tablicy lub tasowania zmienia każdy wygenerowany świat.
- **Mapę po erozji (`NoiseBasedGenerationSys::eroded`) zmienia tylko instalacja wyniku zadania**, bo zadanie liczy na kopii. Kod w `RunErosion` działa na
  wątku roboczym: bez OpenGL, bez ImGui, bez pól subsystemu (jest `static`). OpenGL tylko w wątku głównym, więc `GpuHeightGenerator` działa w `Update()`.
- Odczyt z GPU (`ReadLayer`, `TextureClass::Read`) czeka na zakończenie pracy GPU; nie wołaj go co klatkę. Wyjątek: identyfikatory biomów po każdej ich generacji, dla nazwy biomu pod kamerą.
- `GpuHeightGenerator::BeginMeasure`/`EndMeasure` to `GL_TIME_ELAPSED`, więc nie mogą nachodzić na pomiary sceny; działają, bo `Update()` idzie przed `BeginScene`.
- Splajny: liniowe albo kubiczne wg `SplinePoints::cubic` (checkbox „Smooth (cubic)” w edytorze). Kubiczny = monotoniczny PCHIP (własna implementacja,
  tk::spline usunięty 2026-11-08): nie wychodzi poza wartości sąsiednich punktów, więc splajn z punktami w [0, 1] daje wynik w [0, 1]. GPU dostaje splajn jako tablicę 1024 próbek, a edytor rysuje
  krzywą tą samą funkcją (`SampleSpline`, `SplineSampling.*`). Edytor zmienia splajn w configu na żywo (bez kopii); końce zostają w x = -1 i 1, minimum 3 punkty.
  Domyślna metoda terenu to LAYERED (Linear nie używa splajnów).
- Uniformy terenu ustawia wyłącznie `TerrainView::Draw` — subsystemy nie dotykają shadera. Wysokość bierze się
  z warstwy podanej w `Draw` (`heightLayer`: TERRAIN = jednostka 0, OVERLAY = 1 dla podglądu szumu / erozji w Noise heightmap).
  TerrainGen zawsze pokazuje teren wynikowy; edytowany szum składowy widać tylko w `NoisePreview` w panelu, a zmiany jego parametrów od razu regenerują teren (i biomy, jeśli są włączone).
- Oświetlenie w world space: `FragPos = model * p`, normalna z gradientu `surfaceMap` (krok `worldUnitsPerTexel`)
  przez `normalMatrix`. Zmieniając skalę siatki, zmień `worldUnitsPerTexel`, nie shader. Światło to słońce (kierunek `light.direction`), więc nie zależy od położenia.
  Mgła: teren przechodzi w kolor nieba (`fogColor` z `sky.glsl`) od „Fog start” (część „Render distance”) do całego zasięgu; w widoku 2D mgły i nieba nie ma.
  Kolory ambient i słońca z UI są w shaderze mnożone przez `ambientTint` / `sunlightTint` (`sky.glsl`), zależne od wysokości słońca.
- Po wyłączeniu „Infinite generation” subsystem woła `TerrainView::BringCameraToMap` (kamera przenoszona tylko, gdy nie jest nad mapą), dlatego `Update` dostaje `Camera&`.
- Chunki: tekstura ma margines 1 teksela (`textureSize = chunkSize + 3`, origin = `coord * chunkSize - 1`), a sąsiednie chunki dzielą teksele krawędzi.
  Brak szwów wymaga, żeby generacja była funkcją samej pozycji mapy: `float(texel) + origin` musi być dokładne (liczby całkowite), a nic nie może zależeć od
  rozmiaru tekstury — wyspa używa `islandSize`, nie `mapSize`. `--gpu-check` (sekcja `chunk:`) porównuje chunk z fragmentem pełnej mapy bit w bit.
  Uniformy shadera terenu ustawia `TerrainView::SetFrameUniforms` dla obu (`TerrainView` i `ChunkWorld` używają tych samych plików shaderów).
- Kto pisze do warstwy wysokości, musi wziąć teksturę przez `LayerTexture` / `SetLayer` (one oznaczają `surfaceMap` jako nieaktualną). Zapis inną drogą zostawi
  starą normalną. `surfaceMap` nie zależy od `heightScale`: skala jest stosowana w `surface.glsl`.
- Tryb „2D map” nie ma osobnej gałęzi w shaderze — `TerrainView` podmienia `view`/`projection` na ortogonalną
  kamerę z góry (+X w prawo, −Z w górę) kadrującą `SetFlatViewBounds` (domyślnie cała mapa).
- Shadery obsługują `#include "plik"` (ścieżka względna do pliku zawierającego, każdy plik raz na etap; wspólne
  w `res/shaders/common/`). F5 w oknie aplikacji przeładowuje wszystkie shadery — przy błędzie zostaje stary program,
  a błąd idzie do konsoli. Numery linii w błędach dotyczą pliku, w którym błąd jest, ale nazwa pliku nie jest podawana.
- MSVC/vcpkg nie znoszą ścieżek > 260 znaków — testowy clean build z eksportu repo rób w krótkiej ścieżce
  (np. `%TEMP%\ptg_cc`), nie w scratchpadzie.
- Pomiary GPU: `GL_TIME_ELAPSED` nie może się zagnieżdżać, więc scena i UI mają osobne, kolejne zapytania — nie otwieraj
  kolejnego w ich trakcie. Liczba trójkątów obejmuje też trójkąt nieba i podgląd erozji, i jest liczona przed przycinaniem.
- Biomy (Faza 3): tabela w `BiomeConfig::table` (w presetach), id biomu = indeks w tabeli. Każdy texel dostaje najbliższy biom w przestrzeni (C, M, W, T, H, E),
  odległość = długość wektora wyjścia poza zakresy; kolor miesza wszystkie biomy wagą `BlendWeight` (`blendWidth`), więc mapa id ma ostre granice, a kolory płynne.
  Reguły są dwa razy: `Biome.cpp` (testy) i `common/biomes.glsl` — zmiana w jednym wymaga zmiany w drugim. Tabela nie może być pusta (sprawdza `Check`).
  Parametrów biomu jest 6: pięć szumów i `ELEVATION` = wysokość względem lustra wody w [-1, 1] (`biome::Elevation`, ustawiana w `ApplyClimate`; 0 = lustro,
  -1 = dno na wysokości 0, 1 = wysokość 1). W shaderach pętla po szumach idzie do `NOISE_COUNT`, a `PARAMETER_COUNT` obejmuje elewację; nowy parametr
  = `biome::Parameter` + `parameterNames` + `biomeParameterNames` (`Preset.cpp`) + stałe w `biomes.glsl` + rozmiar `GpuBiomeEntry`. Brakujący zakres w presecie = [-1, 1].
  Domyślna tabela (18 biomów, `DefaultBiomes`) dzieli całą przestrzeń parametrów bez dziur i nakładania (testy „cover the whole parameter space” / „do not overlap”);
  woda, wybrzeże, pagórki i góry są wybierane elewacją, więc tabela nie zależy od splajnu C ani `seaLevel` (C nieużywane). Progi skalibrowane na domyślnym świecie
  (udziały w ROADMAP, sekcja „Nowa tabela biomów”) — po zmianie domyślnego terenu lub klimatu warto je zmierzyć ponownie.
  Modyfikatory terenu: sąsiednie biomy lądowe mają podobną amplitudę (0,85–1), bo różnica amplitud robi rampę na granicy biomów; charakter rzeźby daje szum szczegółu
  (nachylenie ~ `detailAmplitude * heightScale / (4 * detailSize)`). TerrainGen wyświetla wysokość 1 jako 1024 jednostki (`worldHeightScale`), pozostałe tryby 256.
  Przed wyborem biomu T i H zmienia klimat (`biome::ApplyClimate`: lapse rate i wilgotność przy brzegu, od wysokości nad `TerrainConfig::WaterHeight()`);
  shader biomów liczy wysokość sam (`common/terrain.glsl`), więc biomy zależą od całego `TerrainConfig` — `RequestTerrain` przy włączonych biomach przelicza też biomy.
  Modyfikatory terenu (`biome::TerrainModifier`, `ModifyHeight`): przy włączonych biomach i `modifyTerrain` teren zależy od `BiomeConfig` (`GenerateTerrain(..., &biomes)`),
  więc `RequestBiomes` przelicza też teren. Biomy wybiera wysokość przed modyfikatorami (bez pętli). Teren po modyfikatorach jest w [0, 1].
  Teren + biomy razem = `GpuHeightGenerator::GenerateWorld`: jeden przebieg `biome_compute.shader` (`writeHeight`) pisze wysokość, kolory, id i splat, szumy i odległości
  do biomów liczone raz (jedna pętla po biomach dla koloru, modyfikatorów i materiałów). Używają go chunki z biomami i mapa skończona, gdy trzeba obu.
  Ścieżka modyfikatorów w `height_compute.shader` (`modifyHeight`) zostaje jako wzorzec: `--gpu-check` (sekcja `world:`) wymaga, żeby jeden przebieg był bit w bit
  równy dwóm osobnym — sumy w pętli `biome_compute.shader` muszą iść w tej samej kolejności co w `modifyHeight`.
- Teksturowanie (tryb TEXTURED): tekstury materiałów nie są w gicie — `python tools/fetch_textures.py` pobiera je do `res/textures/terrain/` (lista i id = kolejność
  w `res/textures/terrain_materials.json`). Limit 16 = `TerrainMaterials::maxMaterials` = `biome::maxMaterials` = `MAX_MATERIALS` w GLSL — zmiana wymaga wszystkich trzech.
  Brak tekstur nie jest błędem: warstwa tablicy dostaje `fallbackColor`. Biomy i presety zapisują id materiałów, nie nazwy, więc zmiana kolejności w JSON zmienia materiały biomów.
  Każdy biom ma do 4 warstw `biome::MaterialLayer`. Mapę udziałów materiałów (splat, RGBA8 × 4 warstwy) pisze `biome_compute.shader` razem z biomami; mieszanka i plamy to
  `biome::MixMaterials` (CPU, testy) = sumy w pętli `biome_compute.shader` + `finishMaterials` w `common/biomes.glsl` — jak reguły biomów, zmiana w obu. Bez biomów splat mapy nie ma (`useSplat = false`)
  i cały teren ma materiał wybrany w „Terrain materials”.
- Roślinność (Faza 4): modele glTF z Poly Haven nie są w gicie — `python tools/fetch_models.py` pobiera je do `res/vegetation/models/` (lista w `res/vegetation/vegetation_models.json`).
  `VegetationModels` wczytuje je w tle (wątki, bez GL) i instaluje w `Update` na wątku GL; LOD-y budowane przy pierwszym wczytaniu (`vegetation::BuildLods`) i trzymane
  w `res/vegetation/cache/` — zmiana algorytmu LOD-ów lub formatu wymaga podbicia `cacheVersion` w `VegetationMesh.cpp`, inaczej zostaną stare. Modele są w metrach
  (1 m = 4 jednostki świata), podstawa w początku układu; jeden plik = kilka wariantów. Jakość LOD-ów sprawdza `--vegetation-preview [folder]` (PNG).
  Rozmieszczenie (`VegetationPlacement.*`, CPU, testy): siatka komórek z jitterem na warstwę (drzewa/krzewy/skały), komórki w całości w jednym chunku, wszystko
  z hasha pozycji komórki → deterministyczne i bez szwów. Co rośnie, mówią rośliny biomu (`biome::Plant` w `Biome::plants`, ≤ 16: model po nazwie, gęstość, skala,
  maks. nachylenie, zakres elewacji, klastrowanie), edytowane w edytorze biomów i zapisywane w presetach; warstwę (komórki 8 / 4 m) wyznacza `kind` modelu w JSON
  (`vegetation::LayerOfKind`). Domyślne rośliny: `biome::DefaultPlants(nazwa)`; biom z presetu bez pola `plants` dostaje je po nazwie. Zmiana roślin nie regeneruje
  terenu (edytor nie zgłasza jej jako zmiany biomów) — `VegetationLayer` porównuje rośliny sam i rozmieszcza chunki od nowa, rysując stare instancje do czasu podmiany.
  `VegetationLayer` (w `TerrainGenerationSys`) rozmieszcza na liściach quadtree w zasięgu (`ChunkWorld::GroundChunks`: nie nakładają się, teren rysowany
  z tych samych map): poziom 0 = wszystkie warstwy, grubsze (daleko) tylko drzewa (`PlacementSettings::layerMask`, `ChunkMaps::step`; ten sam hash komórki, więc
  te same drzewa co na drobnych chunkach). Mapy czyta wątek GL (≤ 4 chunki na klatkę), `PlaceChunk` idzie na wątku roboczym (`GenerationJob`, ≤ 8 naraz).
  Instancje chunka leżą na GPU na stałe (jeden bufor, `RangeAllocator`), posortowane po blokach 128 m w porządku Morton (`SortByBlock`), więc część grubszego
  chunka zastępuje nierozmieszczone drobniejsze (i odwrotnie). Co klatkę `cull_compute.shader` (3 przebiegi: klasyfikacja frustum/odległość/LOD z licznikami,
  sumy prefiksowe + komendy, rozrzut do grup) i `glMultiDrawElementsIndirect` per (model, materiał) / `glMultiDrawArraysIndirect` dla impostorów.
  Instancje idą do shaderów jako atrybuty z dzielnikiem (GL 4.5 nie ma `gl_BaseInstance`), wariant impostora w danych instancji. Statystyki wracają z GPU klatkę później.
  Bez biomów roślinności nie ma.
  Kolejność rysowania (`TerrainGenerationSys::Draw`): roślinność, trawa, teren — teren za drzewami odpada na teście głębi przed shaderem materiałów;
  chunki terenu rysowane od najbliższego (sort w `ChunkWorld::Plan`). W stałej kolejności patrzenie w stronę −X/−Z podwajało koszt trybu TEXTURED.
  Woda (`WaterSurface`, w `TerrainGenerationSys`, sekcja „Water” w Display): po terenie, quad na poziomie `TerrainConfig::WaterHeight() * heightScale` na każdym
  chunku gruntu, blending premultiplied, bez zapisu głębi. Głębokość = poziom wody − mapa wysokości chunka (kolor, przezroczystość, piana przy brzegu); odbicie
  nieba z `skyColor` (`sky.glsl`) + refleks słońca, Fresnel; fale = normalne z kilku sinusów wygaszanych przez `fwidth`. Tylko tryb infinite; w 2D widziana z góry (`flatView`, kamera z `TerrainView::FrameBlock`), nigdy całkiem przezroczysta.
  Cienie słońca (`ShadowMap`, w `TerrainGenerationSys`, sekcja „Shadows” w Display): cascaded shadow maps, do 4 kaskad w tablicy głębokości (`GL_DEPTH_COMPONENT32F`,
  porównanie sprzętowe), zasięg „Shadow distance”. Kaskada = ortho ze słońca wokół sfery swojego wycinka frustum, środek przyciągany do tekseli (bez migotania),
  zakres głębokości wydłużony ku słońcu o najwyższy teren + `GL_DEPTH_CLAMP`. `ShadowMap::Render` idzie w `Draw` **przed** roślinnością: na kaskadę teren
  (`ChunkWorld::DrawShadow`: wszystkie liście quadtree w frustum słońca, nie tylko widoczne; teselacja 4× rzadsza) i rośliny (`VegetationLayer::CullShadows` +
  `DrawShadow`: przycinanie per kaskada w te same bufory co kamera, krótsze LOD-y `shadowDetail`, krzewy i skały do `shadowSmallRange`; impostory obrócone do słońca).
  Dane kaskad idą przez `FrameBlock` (`shadowMatrices`, `shadowSplits`, `shadowTexels`, `shadowCascades` = 0 → brak cieni), a tekstura na jednostce 15
  (`FrameUniforms::shadowUnit`, `layout(binding = 15)` w `common/shadow.glsl`) — nie używaj jej do niczego innego. `TerrainView::FrameBlock` dokłada cienie z
  `TerrainRenderSettings::shadows` (subsystem ustawia wskaźnik co klatkę). Odbiorcy wołają `sunShadow(FragPos, normalna, pcf)` (offset po normalnej ~ teksel kaskady);
  przebieg głębokości = `FrameBlock::depthPass`: FS terenu wraca od razu, FS roślin robi tylko test alfa. Impostor bierze cień przesunięty ku słońcu o promień
  (płaski quad nie cieniuje sam siebie). Koszt w gęstym lesie ok. 3–5,5 ms (głównie rośliny; połowa to wypełnianie alfa-testowanych liści w 2048²).
  Trawa (`GrassLayer`, w `TerrainGenerationSys`): co klatkę `grass_compute.shader` przechodzi po tekselach wokół kamery na chunkach gruntu i dopisuje do bufora źdźbła,
  które tam rosną (udział trawy materiałów ze splat mapy: pola `grass` i `grassColor` w `terrain_materials.json`, mniej na klifach, rzadsze z odległością)
  i są w kadrze; jedno `glDrawArraysIndirect`, źdźbło budowane z `gl_VertexID`, wiatr w VS. Nic nie jest trzymane między klatkami ani czytane z GPU.
  Kamera, słońce i mgła idą do shaderów terenu, roślinności i trawy przez blok `FrameBlock` (`common/frame.glsl`, std140, binding 1) = `FrameUniforms::Block`
  (pole w pole); każdy renderer ma własny `FrameUniforms` i wgrywa blok przed rysowaniem (widok 2D, pieczenie impostorów i podgląd podmieniają kamerę).
  Impostory (tylko `kind` "tree"): przy instalacji `VegetationModels::BakeImpostors` renderuje LOD0 każdego wariantu z 8×8 kierunków półsfery (hemi-octahedral,
  `Impostor.h` = `common/impostor.glsl`, zmiana w obu; testy `ImpostorTests.cpp`) do tablicy tekstur 1024² (warstwa = wariant): albedo i normalna w przestrzeni modelu,
  tło przezroczyste, więc mipmapy są „premultiplied” (shader dzieli przez A). Rysowane jako czwarty poziom (`VegetationLayer::impostorLod`) od „Impostors from”,
  quad do kamery mieszający 4 najbliższe klatki (`impostor_vertex/fragment.shader`); checkbox „Impostors” = LOD2 zamiast nich. Atlas zapisuje też `--vegetation-preview`.
  Pomiar: `--vegetation-bench [gęstość] [wysokość oka m] [prefiks zrzutów, - = bez] [zasięg widoku m] [wysokość słońca °]` — las domyślnego świata, kamera w stronę największego lądu
  z roślinnością, ten sam układ mierzony z siatkami i z impostorami, czasy CPU też w czasie ładowania, zrzuty PNG.
  Narzędzia z własnym oknem (`--gpu-check`, bench) muszą same ustawić `glPatchParameteri(GL_PATCH_VERTICES, 4)` (w aplikacji robi to `TerrainGenApp`), inaczej teren to śmieci.
  Klify, height blending, triplanar, specular i anti-tiling są w FS (`Terrain_fragment.shader`, `common/materials.glsl`), parametry w UBO `TerrainMaterials::Block`,
  który musi odpowiadać `TerrainMaterialBlock` (std140) pole w pole. Odczyty materiałów w FS przez `textureGrad` (pochodne z `surfaceSampling`, liczone w jednolitym
  przepływie sterowania). W FS uniform izolinii nazywa się `step` i zasłania wbudowane `step()` — nie używaj go w tym shaderze.
  Jednostki tekstur terenu: 0–1 wysokość, 2 biomy, 3 `surfaceMap`, 4–5 materiały, 6 splat.
- Presety (`Preset.cpp`): nowe pole w `WorldConfig.h` trzeba dopisać do zapisu i odczytu oraz do `tests/PresetTests.cpp` (funkcje `Distinct*` / `CheckSame*`).
  Wersja 2 (2026-11-08, tabela biomów zamiast poziomów); wersja 1 nie jest już czytana (`minVersion`).
  Nowe wymaganie generacji wobec wartości configu = nowy warunek w `Check*`, bo plik można edytować ręcznie. Nie zmieniaj nazw pól
  ani enumów w JSON bez podbicia `currentVersion`. Pliki trafiają do `presets/` względem katalogu roboczego (poza gitem).
- Eksport map: zakres wartości jest stały (`heightmap::terrainRange`, `noiseRange`), nie min–max mapy. Zmiana zakresu generacji wymaga zmiany
  tych stałych, inaczej wartości zostaną przycięte. Eksport czyta mapę z tekstury w chwili zapisu, w wątku głównym.
- Import map: plik nie przechowuje zakresu wartości, więc wybiera go użytkownik (`valueRanges` w `MapImportSys.cpp`). `lodepng` nie zamienia koloru na szary
  ani niższej głębi na 16 bitów (błąd 56), dlatego PNG dekodujemy do RGBA o głębi pliku. Mapa żyje w `MapImportSys::map`, stąd pójdą przyszłe operacje na niej
  (jak w innych trybach: w tle, na kopii).
- **Generacja jest tylko w GLSL** (`ComputeShaders/`, `common/noise.glsl`, `common/simplex.glsl`, `common/surface.glsl`). Nowe pole configu szumu trzeba
  dopisać do `NoiseParams` (`noise_params.glsl`), `GpuNoiseParams`/`SetNoise` (`GpuHeightGenerator.cpp`), presetów i UI, a potem uruchomić `--gpu-check`.
- LOD chunków: poziom L bierze co 2^L-tą próbkę (`MapArea::step`, potęga dwójki, więc próbki poziomów się pokrywają). Siatka chunka jest jedna, skalowana w XZ uniformem
  `meshScale` (VS), a skirty (wierzchołki z `aPos.y = -1`) TES opuszcza o `skirtDepth`; `SetFrameUniforms` ustawia oba na neutralne (1 i 0) dla `TerrainView`.
  Culling (chunki na CPU w `ChunkWorld`, patche w TCS) zakłada |wysokość| ≤ 1,5 (`heightBound` w `ChunkWorld.cpp` = `HEIGHT_BOUND` w TCS); mapa o większych
  wartościach wymaga zmiany obu, inaczej teren przy krawędzi ekranu zniknie. Nie skaluj `model` niejednorodnie — `normalMatrix` i mapa pochodnych (liczona z krokiem `worldUnitsPerTexel · 2^L`) zakładają jednostki świata. Stara roślinność — w historii (`fb8b840`), opis w ROADMAP, Faza 4.

## Kierunek rozwoju (skrót z ROADMAP)

Cel: edycja parametrów w czasie rzeczywistym + maksymalna wydajność + biomy i roślinność.
Kolejność: naprawy krytyczne (Faza 0) → porządki/architektura (Faza 1) → generacja na GPU (compute shaders)
i system chunków z LOD (Faza 2) → biomy ciągłe z blendingiem i teksturowaniem (Faza 3, domknięta 2026-11-13 kontynentami, tabelą 18 biomów i rzeźbą terenu) — wszystko to zamknięte →
roślinność instancjonowana (Faza 4, następna) → erozja GPU, woda, eksport (Faza 5).
