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
- Targety: `ptg_vendor` (Simplex, stb_image impl., bez ostrzeżeń), `ptg_core` (`terrainGeneration/`: configi, presety,
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
  zgodność id i kolorów biomów (też biomów Imported map, `mapbiomes:`), chunki i poziomy LOD bit w bit z pełną mapą, mapę pochodnych na mapach o znanych pochodnych, udział wody domyślnego świata
  (55–65%, ≥ 80% lądu w kontynentach), rzeki (obie metody: zakres, jeden przebieg = dwa, chunki), erozję (krople i rury), pędzle (GPU = `sculpt::Apply`); potem czasy GPU generacji i erozji.
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
      MapImportSys.*             tryb "Imported map": projekt z jedną kopią roboczą mapy (z pliku mapy, z projektu .ptgmap albo wysłana z innego trybu),
                                 zakładki Project (Open/Save/Save as, Ctrl+S, `*`), Edit i Load, Saving = paczka eksportu; odczyt i zapis w tle;
                                 MapImportEdit.cpp = zakładka Edit tej klasy (erozja, wygładzanie, skala wysokości, oryginał, Cofnij/Ponów, morze, rzeki i jeziora);
                                 MapImportSculpt.cpp = pędzle (punkt pod kursorem, pociągnięcie na GPU, kółko pędzla);
                                 MapImportBiomes.cpp = zakładka Biomes (klimat z terenu w tle, pokrycie terenu i georeferencja, przebieg GPU biomów, ustawienia projektu)
    terrainGeneration/
      WorldConfig.h              same dane: WorldConfig (TerrainConfig + BiomeConfig), NoiseMapConfig, ErosionConfig
      Noise.h                    noise::NoiseConfigParameters (fBm, ridge, island, redistribution); sam szum jest tylko w GLSL
      MapBiomes.*                biomy mapy niegenerowanej (Imported map): `mapbiomes::Settings` (JSON w projekcie), C i M z mapy w metrach (`ComputeClimate`:
                                 odległość od brzegu, rzeźba w promieniu), wysokość dla klimatu (`ClimateHeight`) — wzorzec CPU dla `map_biome_compute.shader`
      GeoReference.*             georeferencja map rzeczywistych (D3): stopnie (EPSG 4326) i Transverse Mercator (UTM, ETRS89, PUWG 1992/2000), piksel ↔ układ ↔ stopnie,
                                 odczyt znaczników GeoTIFF (`FromGeoTiffTags`), wycinek i zmniejszenie (`Cropped`); TiffFile.* = wspólne otwieranie TIFF (libtiff)
      LandCover.*                mapa klas pokrycia terenu (D3, np. ESA WorldCover) pod mapą: kilka plików GeoTIFF uint8, większość z k × k próbek, rozciągnięcie bez georeferencji
      BiomePaint.*               malowane biomy Imported map (D2): warstwa (paleta nazw + slot i siła na teksel), dab pędzla, slot → biom tabeli po nazwie,
                                 materiały teksela pomalowanego — wzorzec CPU mieszania w `map_biome_compute.shader`
      Biome.*                    biom = zakresy 6 parametrów (5 szumów + elewacja) + kolor; najbliższy biom (Nearest) i wagi przejść (BlendWeight) — opis reguł, które liczy biome_compute.shader
      Grid.h                     mapy Grid<T>
      SplineSampling.*           próbkowanie splajnu do tablicy (dla GPU i wykresu w edytorze)
      Frustum.h                  płaszczyzny frustum z macierzy i test AABB (culling chunków)
      Preset.*                   zapis/odczyt WorldConfig i NoiseMapConfig jako JSON + walidacja; opcjonalne `mapOffset` (położenie mapy częściowej)
      Places.*                   nazwane miejsca trybu (kamera przed Model scale, yaw/pitch, `mapOffset`) w jednym pliku JSON: places/world.json, places/noise.json
      HeightmapExport.*          zapis mapy jako PNG 16-bit, RAW R16 lub EXR float32 w metrach (własny zapis, bez kompresji); FileIO.* = zapis przez plik tymczasowy
      ExportManifest.*           <nazwa>.json paczki eksportu: rozmiar i wysokości w metrach, kodowanie każdego pliku, poziom morza, wskazówki importu, parametry generacji,
                                 lista materiałów (plik splat + kanał, mapa wag) i biomów (id, nazwa, kolor, udział); `ReadSource`/`ListPacks` = odczyt paczki dla „Load from pack”
      MaterialExport.*           mapy materiałów i biomów paczki (8-bit PNG): splat RGBA po 4 materiały, mapa wag na materiał, id i kolory biomów; Normalize = suma 255 na teksel
      TerrainMesh.*              siatki eksportu (metry, Y w górę): pełna siatka mapy, LOD-y (meshopt_simplify bez fałd), błąd wysokości, kafle, woda (quad, zupa trójkątów → siatka)
      MeshFile.*                 zapis GLB (glTF 2.0, ręcznie: JSON + bufor; węzeł = prymityw na materiał) i OBJ; MeshExport.* = pliki siatek paczki (LOD-y z kaflami + woda), bez GL — w tle
      VegetationExport.*         roślinność paczki: PlaceChunk na chunkach skończonej mapy (jak świat nieskończony) → _vegetation.json, modele z LOD-ami aplikacji → vegetation/*.glb
      HeightmapImport.*          odczyt mapy z PNG, R16/RAW, GeoTIFF (libtiff), SRTM .hgt, ESRI .asc; format po rozszerzeniu, nowy = czytnik i rozszerzenie w tym pliku;
                                 `Inspect` = sam nagłówek, `Import(ImportOptions)` = odczyt → wycinek → NoData (tylko w wycinku) → zmniejszenie → wynik ≤ 8192²;
                                 próbki |v| ≥ 1e30 to też NoData (±FLT_MAX bez znacznika)
      DemTools.*                 mapy rzeczywiste: wypełnianie NoData z sąsiadów, wycinek, zmniejszenie (średnia bloków), piksel w metrach ze stopni
      MapProject.*               plik projektu .ptgmap: sygnatura + długość + nagłówek JSON (rozmiar, piksel, morze, źródło, preset, przewyższenie) + bloki zlib
                                 (float32 w metrach rozdzielone na 4 płaszczyzny bajtów): kopia robocza i oryginał (tylko gdy się różni); zmiana pól = podbicie `currentVersion`
                                 (2 = ustawienia rzek jako JSON `preset::RiversJson`, 3 = ustawienia biomów `mapbiomes::ToJson`, 4 = malowane biomy: paleta
                                 w nagłówku `biomePaint`, blok `biomePaint` = zlib uint16 LE; 5 = georeferencja `georeference` w nagłówku; starsze wersje nadal czytane)
      MapEdit.*                  operacje na mapie w metrach (wygładzanie pudełkowe, skala + przesunięcie, `ErosionScale`) i historia Cofnij/Ponów z limitem pamięci
      Sculpt.*                   pędzle (podnieś, obniż, wygładź, spłaszcz, szum) na CPU = wzorzec `sculpt_compute.shader`, zanik, rzut kursora na teksel mapy (głębia → teksel)
      CameraMath.*               matematyka kamery edytora: rozglądanie, orbita wokół punktu (punkt zostaje w tym samym miejscu widoku), przesuwanie, jednostki na piksel
      GenerationJob.h            jedno zadanie generacji w tle (std::async + TryTake)
      LandStats.*                udział wody i masy lądu (flood fill) z rzadkiej próbki świata (GpuHeightGenerator::SampleTerrain); „Land statistics” w zakładce Terrain i sekcja land: w --gpu-check
      Hydrology.*                sieć rzek regionu (metoda REGION): priority-flood, spływ D8, akumulacja, jeziora, biegi rzek → siatki dla RiverMap;
                                 `ComputeOnMap` = sieć całej mapy w metrach (Imported map)
    opengl/                      cienkie wrappery: Shader, VertexArray/Buffer, IndexBuffer, TextureClass, Renderer, GpuQuery
      TerrainView.*              siatka + shader + tekstury (TERRAIN/OVERLAY/BIOMES), wspólny dla obu trybów
      GpuHeightGenerator.*       jedyna generacja: szum, teren i biomy w compute shaderach (GenerateWorld = teren + biomy w jednym przebiegu); GpuHeightCheck.* = testy generacji (--gpu-check)
      NoisePreview.*             podgląd edytowanego szumu jako mapa 2D w panelu ImGui (≤ 512 tekseli na dłuższym boku, preview_compute.shader = skala szarości)
      RiverMap.*                 sieć rzek z Hydrology na GPU (2 tekstury, jednostki 20–21), czytana przy generacji przez common/rivers.glsl, + siatka wody rzek i jezior
      GpuSurfacePass.*           mapa pochodnych (gradient + laplasjan) z mapy wysokości; uruchamia ją TerrainView i ChunkWorld
      GpuErosion.*               erozja w compute: kroplowa (erosion_droplet_compute.shader, algorytm dawnej wersji CPU) albo rury wirtualne + termiczna
                                 (erosion_pipe_compute.shader); metoda = ErosionConfig::method
      GpuSculpt.*                pędzel na GPU (sculpt_compute.shader): jeden dab w kwadracie wokół pędzla, wprost do warstwy wysokości
      Clouds.*                   chmury wolumetryczne: szumy 3D + mapa pogody (raz), ray march w compute w połowie rozdzielczości, składane przez niebo / po scenie
      ShadowMap.*                cienie słońca: cascaded shadow maps (teren + roślinność), dane kaskad w FrameBlock; WaterSurface.* = woda
      ChunkWorld.*               „Infinite generation”: quadtree chunków 128² z LOD wokół kamery, kolejka wg odległości, cache, skirts, rysowanie shaderem terenu
      TerrainMaterials.*         materiały terenu (tryb TEXTURED): lista z terrain_materials.json, texture arrays (albedo+wysokość, normal+roughness+AO; jednostki 4, 5)
                                 + UBO parametrów FS (binding 0), sekcja „Terrain materials”
    utility/
      Camera.*                   kamera FPS (WASD, Space/lewy Shift, M = mysz, Esc = zwolnij mysz); EditorCamera.* = sterowanie myszą jak w edytorach (prawy, Alt + lewy, środkowy, kółko)
      LightSource.*              słońce: światło kierunkowe (azymut, pora dnia = wysokość, cykl dnia), ambient, mgła (zamglenie wysokościowe + zanikanie na granicy)
      Sky.*                      atmosfera: LUT transmitancji i widoku nieba (rozpraszanie Rayleigh/Mie/ozon, compute, co klatkę) + tło 3D (niebo, tarcza słońca, gwiazdy)
      FrameMetrics.*             pomiary w panelu Output (czas klatki/CPU/GPU, trójkąty); RollingStats.h = średnia i maks. z ostatnich próbek;
                                 FlightRecord.h = „Record flight” (prędkość, klatki > 20 ms, klatki z dziurą), pomiar kryterium Fazy 2
      PresetPanel.*              sekcja „Presets” w ImGui (lista plików, nazwa); zapis i odczyt wykonuje subsystem
      PlacesPanel.*              sekcja „Places” zakładki Presets: lista miejsc i jej plik; subsystem podaje bieżący widok i przenosi kamerę + mapę
      PackPicker.*               sekcja „Export packs” zakładki Presets: lista paczek z exports/, „Load from pack” (subsystem stosuje `generation` + `worldOrigin`)
      ErosionPanel.*             parametry erozji (wybór metody + jej pola, bez przycisków), wspólne dla zakładek Erosion obu trybów; nazwa metody
      MapLabels.*                podpisy map rysowanych obok siebie (mapa + kopie po erozji): rzut w Draw, rysowanie w ramce ImGui
      ExportPanel.*              sekcja „Saving” w ImGui: paczka w exports/<nazwa>/; subsystem podaje `ExportPanel::Pack` (mapy, zakres, metry na jednostkę, morze, preset)
      BiomeEditor.*              edytor tabeli biomów (lista, zakresy, diagram Whittakera T × H); edytuje tabelę w miejscu, subsystem przelicza biomy;
                                 `ShowTerrainModifiers(false)` w Imported map
      BiomeSettingsPanel.*       sekcje ustawień biomów wspólne dla TerrainGen i Imported map (blend, klimat, materiały)
      PanelTabs.*                zakładki panelu: rzędy przycisków na górze (zawijane, gdy się nie mieszczą) + przewijana treść pod nimi; wybór trzymany po nazwie
      FileDialog.*               systemowe okna otwierania i zapisu pliku (tylko Windows, comdlg32); przy porcie na Linuksa do zastąpienia
      SendToImported.*           przyciski „Send map” / „Send eroded copy” w Saving TerrainGen i Noise: mapa z `ExportPanel::Pack` w metrach → `mapproject::Project`
      utilities.*                helpery ImGui (NoiseImGui, DisplayModeImGui...), siatka patchy, kolory biomów
    vendor/                      SimplexNoise (z niego tylko makePermutation: tablice permutacji dla GPU), stb_image
  tests/                         testy jednostkowe ptg_core (doctest)
  legacy/cpuErosion/             LEGACY, niekompilowane: erozja kroplowa na CPU z pracy dyplomowej + jej testy, tylko poglądowo
  res/shaders/
    TerrainShaders/              VS → TCS → TES → FS, jedyny zestaw dla terenu (oba tryby)
    ComputeShaders/              height_compute.shader (szum, teren), biome_compute.shader, map_biome_compute.shader (biomy Imported map), surface_compute.shader, erosion_droplet/pipe_compute.shader; wspólne: common/noise.glsl, noise_params.glsl, simplex.glsl, surface.glsl,
                                 terrain.glsl (wysokość terenu z C/M/W), rivers.glsl (żłobienie koryt), world_input.glsl (wspólny bufor wejściowy), biomes.glsl (reguły biomów, klimat, modyfikatory terenu, mieszanie materiałów)
    common/materials.glsl        próbkowanie materiałów w trybie TEXTURED (triplanar, anti-tiling), blok TerrainMaterialBlock = TerrainMaterials::Block
    sky_vertex/fragment.shader   niebo; kolory nieba i światła w common/sky.glsl (z LUT), mgła w common/fog.glsl; model atmosfery common/atmosphere_model.glsl, LUT-y w AtmosphereShaders/;
                                 chmury: CloudShaders/ + common/clouds.glsl (gęstość, mapa pogody, cień chmur w sunShadow)
  res/textures/terrain_materials.json   lista materiałów (kolejność = id); same tekstury w res/textures/terrain/ (poza gitem)
tools/fetch_textures.py          pobiera tekstury materiałów (CC0, Poly Haven / ambientCG), tylko biblioteka standardowa Pythona
tools/fetch_models.py            pobiera modele roślinności i kamieni (CC0 glTF, Poly Haven) z mapami alpha liści
CMakeLists.txt, CMakePresets.json, vcpkg.json   build; GLFW, GLEW, glm, ImGui, ImPlot, stb, libtiff (z zlib, tylko ptg_core i testy), zlib (projekty .ptgmap, ptg_core) z vcpkg
.github/workflows/build.yml      CI (Windows x64 Debug/Release, build + testy jednostkowe)
```

## Przepływ danych (jak to działa)

1. Aplikacja generuje mapy wyłącznie na GPU (`GpuHeightGenerator`). UI ustawia flagi w `WorldRequest` / `NoiseRequest`, a `Update()` subsystemu
   (co klatkę, przed `Draw`, w wątku głównym) uruchamia compute shader, który pisze wprost do tekstury warstwy `TerrainView` (`LayerTexture`).
   Biomy liczą w shaderze wszystkie 5 szumów, więc nie ma osobnych tekstur szumów ani zależności między krokami.
   Generacji na CPU nie ma (usunięta 2026-11-08; ostatnia wersja z nią: `56eae39`).
2. Mapy na CPU powstają tylko na żądanie przez `TerrainView::ReadLayer` (eksport). Erozja (od 2026-11-16) też na GPU: `GpuErosion` w `Update()`
   czyta warstwę TERRAIN (albo OVERLAY, gdy erozja się kumuluje) i pisze OVERLAY. Mapa z pliku w trybie Imported map idzie przez `TerrainView::SetLayer`.
   Tekstury mają niezmienny storage, nowa powstaje przy zmianie rozmiaru.
3. Siatka (`TerrainView`): stała siatka `mapResolution × mapResolution` (50×50) patchy-quadów o rozmiarze
   `width * worldUnitsPerTexel` (= 4, `GetWorldWidth()`), rysowana `GL_PATCHES` (4 wierzchołki).
4. TCS: w 3D poziom teselacji z długości krawędzi na ekranie (`tessPixels`, suwak „Terrain detail”, 1–64), w widoku 2D po staremu 4–64 od odległości (ma zostać ostry). Normalna i kolor biomów liczone w FS na piksel (z `surfaceMap` / `biomeMap`). TES: próbkuje `heightMap`,
   przesuwa wierzchołek o `height * heightScale`, bierze normalną z `surfaceMap`, koloruje wg `displayMode`
   (0 grey, 1 topo, 2 mono, 3 biomy z `biomeMap`, 4 nachylenie, 5 krzywizna, 6 TEXTURED — materiały liczy FS z `splatMap`, 7 LAND COVER — klasy pokrycia z `landCoverMap`, tylko Imported map). `surfaceMap` (gradient + laplasjan surowej wysokości, `RGBA32F`)
   liczy `TerrainView::Draw` przez `GpuSurfacePass`, tylko gdy warstwa wysokości zmieniła się od ostatniego razu (`LayerTexture` oznacza ją jako nieaktualną).
5. Tryb „infinite” (TerrainGen i Noise heightmap) to `ChunkWorld`: w `Update(camera)` subsystem woła `Plan` (chunki w zasięgu „Render distance”)
   i `GenerateNext` z własną funkcją generującą (`GpuHeightGenerator` z originem chunka i `islandSize` = rozmiar mapy), a `Draw` rysuje chunki zamiast `TerrainView`.
   Każde żądanie (`Request*`) w tym trybie unieważnia wszystkie chunki; skończona mapa jest odświeżana po powrocie do „Partial generation”. Eksport i erozja działają tylko na skończonej mapie.
   Pozycja mapy p (teksele) leży w świecie w `mapOrigin + p * worldUnitsPerTexel`; `mapOrigin` ustawia świat tak, że pokrywa skończoną mapę (`AlignWithMap`).
   Mapa częściowa to wycinek świata: `mapOffset` (subsystem) = pozycja mapy jej teksela (0, 0), generacja z `MapArea::origin = mapOffset` i `islandSize` = rozmiar
   mapy (wyspa zostaje w początku świata), `TerrainView::SetMapOffset` przesuwa siatkę, więc środek mapy leży w `mapOffset * worldUnitsPerTexel`. Wyjście
   z „Infinite generation” ustawia `mapOffset` pod kamerą (`OffsetUnder`), Display → „Partial map” pozwala ją przesunąć / śledzić kamerę. Region rzek (REGION)
   i „Land statistics” liczone wokół środka mapy z offsetem. W TerrainGen chunki działają też w trybie częściowym, ograniczone do prostokąta mapy
   (`ChunkWorld::SetBounds`, nierysowane): z nich roślinność (`VegetationLayer` odrzuca rośliny poza prostokątem), trawa (obszar przycięty) i cienie
   (rzuca teren chunków — chunk na krawędzi rzuca też cień terenu tuż za mapą); checkbox „Plants, grass and shadows”. `--gpu-check` (`chunk: moved map`)
   sprawdza, że mapa z offsetem równa się bit w bit tym samym tekselom mapy z początku świata.

Wartości szumów są w [-1, 1]. Konwencja indeksowania: `map[y * width + x]`.

## Konwencje kodu

- Klasy PascalCase, metody PascalCase, pola camelCase, parametry ctor/Initialize z prefiksem `_` (`_width`).
- Wzorzec: konstruktor domyślny + `bool Initialize(...)` + `Resize(...)`; błędy jako `return false` +
  `std::cout << "[ERROR] ..."`; logi `"[LOG] ..."`.
- Mapy jako `Grid<T>` (`terrainGeneration/Grid.h`: `HeightField`, `BiomeMap`) — bez surowych `new[]/delete[]`.
  Do OpenGL przez `.Data()`. OpenGL obiekty w `std::unique_ptr`.
- Warstwy (A3): parametry świata to same dane w `WorldConfig.h`, trzymane przez subsystem (`world` / `config`).
  Generatory (`GpuHeightGenerator`, `GpuErosion`) **nie trzymają configu** — dostają `const XxxConfig&` w każdym wywołaniu.
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
- Erozja (`GpuErosion`, tryb Noise heightmap, tylko skończona mapa): każda metoda ma własną mapę wynikową — krople w warstwie OVERLAY `view`, rury w warstwie TERRAIN
  drugiego widoku `pipesView` (TerrainView ma tylko jedną wolną warstwę wysokości; jednostki tekstur 0–2 są zajęte). Kolejne „Erode map” kumulują się na mapie metody
  (`ErodedMap::fromBase`), nowy szum lub „Reset” zaczyna obie od TERRAIN. „Compare both methods” eroduje obiema naraz i rysuje 3 mapy obok siebie (oba widoki dostają
  te same `SetFlatViewBounds`). Podpisy map (`MapLabels::Place` w `Draw` rzutuje środek górnej krawędzi każdej mapy macierzą kamery albo kamery 2D z `TerrainView::FrameBlock`,
  `MapLabels::ImGuiDraw` rysuje je na `GetBackgroundDrawList` — nad sceną, pod panelami) pojawiają się, gdy obok szumu jest choć jedna mapa po erozji.
- Erozja w TerrainGen (od 2026-11-20): tylko mapa częściowa (w świecie nieskończonym odrzucona — symulacja, nie deterministyczna generacja). Kopia po erozji
  w warstwie OVERLAY `view` (`SetOverlaySplat(true)`: rysowana z mapą materiałów terenu; biomy i splat są te z mapy, nie przeliczane), obok mapy w X
  (`ErodedShift`), z drugim `WaterSurface::DrawMap` (morze z wysokości kopii, rzeki i jeziora z poziomów sieci) i podpisami. Parametry w `WorldConfig::erosion`
  (preset świata; `pipes.heightScale` = 256 pod skalę 1024). Każde `request.terrain` w `GenerateFinite` i wyjście do świata nieskończonego wołają `ResetErosion`.
  Rośliny kopii: `vegexport::PlaceMap` na odczytanej kopii (wątek roboczy, `ErodedTerrain::version` odrzuca wynik starszej kopii) → `ToAppInstances` →
  `VegetationLayer::SetExtraInstances` (osobny zakres bufora, kwadraty chunku poziomu 0 do cullingu; zostaje przy `Update`/`ForgetChunks`); powyżej 2048² tylko drzewa.
  Trawa i cień terenu na kopii — nie ma (biorą się z chunków). „Follow camera” czeka, gdy kopia jest pokazana (lot nad kopię przesunąłby mapę i ją skasował);
  wyłączenie biomów czyści rośliny kopii, włączenie (`RequestBiomes`) rozmieszcza je znowu. Krople: mapa w buforze stałoprzecinkowym (2^-24, |h| ≤ 127) zmienianym `atomicAdd` (GL 4.5 nie ma
  atomików float), krok `i` wszystkich kropel przed krokiem `i + 1` jak w wersji CPU (dispatch na krok, paczki po `dropletBatch`); kolejność zapisów nie jest stała,
  więc wynik nie jest bit w bit powtarzalny (statystycznie zgodny z CPU — sprawdzone na legacy). Rury: deterministyczne, osad płynie rurami razem z wodą
  (zachowanie masy poza tym, co woda wynosi za krawędź), wymywanie nie schodzi poniżej najniższego sąsiada (bez dołów), a odkładanie i końcowy zrzut osadu
  nie podnoszą teksela ponad najwyższego sąsiada + wysokość kąta zsypu (bez linii kolców wzdłuż strużek); po iteracjach faza wysychania bez deszczu
  (`dryingEvaporation` w `GpuErosion.cpp` — szybsze wysychanie = więcej kolców). Nowa metoda = `erosion::Method` + shader +
  przypadki w `CheckErosion` (`GpuHeightCheck.cpp`). OpenGL tylko w wątku głównym, więc `GpuHeightGenerator` i `GpuErosion` działają w `Update()`.
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
  Atmosfera (`Sky`): `Sky::Update` (co klatkę, przed rysowaniem, też w 2D) liczy w compute LUT widoku nieba (192×108 + wiersz kolorów światła: transmitancja do słońca
  = `sunlightTint`, oświetlenie całym niebem = `ambientTint`, unormowane do słońca na 45° stałą `AMBIENT_REFERENCE`) i trzyma go na jednostce 14 (`Sky::skyViewUnit`,
  `layout(binding = 14)` w `sky.glsl`) — nie używaj jej do niczego innego. Każde narzędzie z własnym oknem rysujące oświetlone shadery musi mieć `Sky` i wołać `Update`
  (bez LUT światło jest czarne). LUT jest liniowy, `skyToDisplay` = 1 − exp(−L · `SKY_EXPOSURE`), bez gammy (reszta oświetlenia też jej nie ma). Noc: `NIGHT_SKY`, `NIGHT_AMBIENT`, gwiazdy.
  Mgła (`applyFog` w `common/fog.glsl`, wszystkie shadery sceny): zamglenie o gęstości malejącej wykładniczo z wysokością (`FrameBlock::hazeDensity/hazeFalloff`, z „Haze visibility/height”,
  całka analityczna wzdłuż promienia) w kolor nieba przy horyzoncie (`fogColor`), potem zanikanie w niebo od „Fog start” do „Render distance”; w widoku 2D mgły i nieba nie ma.
  Kolory ambient i słońca z UI są w shaderze mnożone przez `ambientTint` / `sunlightTint` (`sky.glsl`).
  Chmury (`Clouds`, ustawienia w `LightSource`, sekcja „Clouds” w Sun & sky; w `FrameBlock` jako `cloudLayer` / `cloudWind`): warstwa między „Cloud base” a base + thickness (metry),
  gęstość = mapa pogody 512² (kafel 24 km, przesuwana wiatrem) × szum kształtu 128³ (Perlin-Worley) erodowany szumem detalu 32³ — wszystko w `cloud_noise_compute.shader`,
  liczone raz. Ray march (`cloud_compute.shader`) w połowie rozdzielczości viewportu, światło: krótki marsz ku słońcu + faza dwupłatowa + niebo; daleko zanikają w niebo.
  Wynik (rgb = światło, a = transmitancja): `RenderSky` przed niebem, które składa chmury na sobie — teren rysowany później je zasłania, co jest poprawne, dopóki kamera
  jest pod podstawą chmur (dlatego podstawa ≥ 400 m > najwyższy teren); kamera na wysokości chmur lub wyżej → `DrawOver` po scenie (kopia głębi, promień kończy się na scenie,
  blend `GL_ONE, GL_SRC_ALPHA`). Mapa pogody stale na jednostce 16 (`Clouds::weatherUnit`, `layout(binding = 16)` w `clouds.glsl`): `sunShadow` = cień kaskad × `cloudShadow`
  (pokrycie tam, gdzie promień ku słońcu przecina środek warstwy), więc cienie chmur padają na wszystko; w 2D wyłączone. Woda odbija niebo z chmurami z kopii sceny,
  gdy kierunek odbicia jest na ekranie. Koszt ok. 1 ms (1080p, pokrycie 0,45). Benchmark ma linię „clouds GPU” (tylko `RenderSky`).
- Po wyłączeniu „Infinite generation” subsystem przesuwa mapę pod kamerę i woła `TerrainView::BringCameraToMap` (kamera przenoszona tylko, gdy nie jest nad mapą
  z offsetem), dlatego `Update` dostaje `Camera&`. Kod liczący pozycję w świecie z teksela mapy musi uwzględnić `TerrainView::GetWorldCenter` / `GetMapOffset`
  (biom pod kamerą, woda `DrawMap`, podpisy map w Noise heightmap, eksport: `worldOrigin` w manifeście, rośliny i rzeki względem teksela (0, 0) mapy).
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
  Woda (`WaterSurface`, w `TerrainGenerationSys`, sekcja „Water” w Display): rysowana **na końcu**, quad na poziomie `TerrainConfig::WaterHeight() * heightScale`
  na każdym chunku gruntu (`Draw`) albo jeden na skończonej mapie (`DrawMap`, tekstura z `TerrainView::GetLayerTexture`), bez zapisu głębi, nieprzezroczysta.
  Przed nią `CopyScene` kopiuje kolor i głębię viewportu (`glCopyTextureSubImage2D`, jednostki 12–13): dno = kopia przesunięta falami (refrakcja), osłabiana wodą
  wzdłuż promienia widzenia (grubość z kopii głębi); odbicie = SSR po kopii głębi dla **gładkiej** tafli (fale tylko przesuwają odczyt — promień na każdą falę
  migał między brzegiem a niebem), z grubością trafienia `SSR_THICKNESS`; poza ekranem, ku kamerze i bez trafienia niebo z `skyColor` + refleks słońca, Fresnel.
  Głębokość pionowa z mapy wysokości (kolor toni, piana). W 2D widziana z góry (`flatView`, kamera z `TerrainView::FrameBlock`), bez SSR, nigdy całkiem przezroczysta.
  Koszt ok. 0,5 ms (wybrzeże w kadrze, 1080p).
  Cienie słońca (`ShadowMap`, w `TerrainGenerationSys`, sekcja „Shadows” w Display): cascaded shadow maps, do 4 kaskad w tablicy głębokości (`GL_DEPTH_COMPONENT32F`,
  porównanie sprzętowe), zasięg „Shadow distance”. Kaskada = ortho ze słońca wokół sfery swojego wycinka frustum, środek przyciągany do tekseli (bez migotania),
  zakres głębokości wydłużony ku słońcu o najwyższy teren + `GL_DEPTH_CLAMP`; na czas przebiegu wyłączony `GL_SCISSOR_TEST` (prostokąt viewportu z `TerrainGenApp` zostawiał resztę mapy niewyczyszczoną = cień na pół ekranu). `ShadowMap::Render` idzie w `Draw` **przed** roślinnością: na kaskadę teren
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
  Jednostki tekstur terenu: 0–1 wysokość, 2 biomy, 3 `surfaceMap`, 4–5 materiały, 6 splat, 7 klasy pokrycia terenu (`usampler2D`, tryb LAND COVER).
- Presety (`Preset.cpp`): nowe pole w `WorldConfig.h` trzeba dopisać do zapisu i odczytu oraz do `tests/PresetTests.cpp` (funkcje `Distinct*` / `CheckSame*`).
  Wersja 2 (2026-11-08, tabela biomów zamiast poziomów); wersja 1 nie jest już czytana (`minVersion`).
  Nowe wymaganie generacji wobec wartości configu = nowy warunek w `Check*`, bo plik można edytować ręcznie. Nie zmieniaj nazw pól
  ani enumów w JSON bez podbicia `currentVersion`. Pliki trafiają do `presets/` względem katalogu roboczego (poza gitem).
  Położenie mapy (`mapOffset`, od 2026-11-20) nie jest częścią configu: osobny parametr `ToJson/FromJson/Save/Load`, zapisywane przy checkboxie
  „Map position” (w świecie nieskończonym = `OffsetUnder(camera)`, bo tam trafi mapa po wyjściu z niego). Wczytanie presetu z położeniem / paczki
  (`grid.worldOrigin`) = `MoveMap` + `TerrainView::LookDownAtMap` (kamera wysoko nad środkiem, blisko pionu — „Follow camera” nie przesuwa mapy z powrotem).
  W TerrainGen `MoveMap` także w świecie nieskończonym (region rzek idzie za miejscem); w Noise heightmap tylko na mapie skończonej (chunki nie zależą od offsetu).
  Miejsca (`places/`, poza gitem) nie trzymają parametrów generacji.
- Eksport map: zakres wartości plików 16-bit jest stały (`heightmap::terrainRange`, `noiseRange`), nie min–max mapy. Zmiana zakresu generacji wymaga zmiany
  tych stałych, inaczej wartości zostaną przycięte. Eksport czyta mapę z tekstury w chwili zapisu, w wątku głównym.
  Paczka (`exports/<nazwa>/`): pliki `<nazwa>_height*.{png,r16,exr}` + `<nazwa>.json` (`exportpack::ToJson`). Metry: 1 teksel = `Pack::metresPerTexel` (mapy generowane 1 m, Imported map = piksel mapy; roślinność tylko 1 m),
  wysokość 1 = `heightScale / worldUnitsPerTexel` m z bieżącego „Height scale” (TerrainGen domyślnie 256 m). EXR trzyma metry, 16-bit — wartości z zakresu.
  Kolejne części eksportu (siatki, roślinność) dopisują swoje pliki do `Manifest::files`; zmiana nazw pól JSON = podbicie `manifestVersion`.
  Mapy materiałów i biomów (tylko TerrainGen z biomami): splat czytany z GPU (`TextureClass::Read` tablicy = warstwa po warstwie, materiał m = warstwa m/4,
  kanał m%4, jak `imageStore` w `biome_compute.shader`), `splatmap::Normalize` (największe reszty) daje sumę 255 na teksel, której oczekują Unity i Unreal.
  `_splat_N.png` = materiały 4N..4N+3 (id z `terrain_materials.json`), `_weight_ID_nazwa.png` tylko dla materiałów obecnych na mapie, `_biome_ids.png` = id
  z `BiomeConfig::table` (≤ 64, 8 bit), `_biome_colors.png` = warstwa BIOMES.
  Siatki (`meshexport::Write`, w tle przez `GenerationJob`; mapy i manifest w wątku głównym, manifest dopiero po siatkach — `ExportPanel::Update` co klatkę
  w `Update` subsystemu, bo zakładka Saving może być zamknięta): `_terrain_lod<n>.glb|obj`, kafle jako węzły `terrain_<x>_<z>_LOD<n>` (podział po środku
  trójkąta, po upraszczaniu — bez szczelin), LOD n+1 z LOD n (¼ trójkątów). `meshopt_simplify` odrzuca tylko obrót normalnej > 90°, więc na stromym zboczu
  robił fałdy (trójkąt odwrócony w rzucie z góry): upraszczana jest kopia ze ściśniętą wysokością (najbardziej strome nachylenie 0,25), przy fałdzie ponownie
  4× mocniej (`terrainmesh::Folds`; trójkąty pionowe są dozwolone). Błąd w JSON = `MaxHeightError` (maks. różnica pionowa względem pełnej siatki), nie metryka
  meshoptimizer (ta jest prostopadła do powierzchni, na stromiznach wielokrotnie mniejsza). Pomiar: test „Mesh export timing” (`--no-skip`).
  Woda `_water.*`: węzeł `sea` (quad na poziomie morza) i `rivers_lakes` (`hydrology::BuildWaterMesh` z `riverNetwork`, tylko REGION, przycięte do mapy).
  Tekstura terenu w glTF = `_biome_colors.png`, jeśli eksportowany (zewnętrzny plik obok).
  Roślinność (`vegexport::Write`, w tym samym zadaniu w tle co siatki; mapa pochodnych `TerrainView::ReadSurface` i id biomów czytane w wątku głównym):
  roślinność rośnie tylko w świecie nieskończonym, więc eksport sam woła `vegetation::PlaceChunk` na chunkach 128 tekseli skończonej mapy (margines
  1 teksela wycięty z pełnej mapy z przycięciem do krawędzi), z `mapOrigin = 0` i 1 jednostką na teksel — hash komórki zależy tylko od pozycji mapy,
  więc rośliny są te same co w aplikacji (test w `VegetationExportTests.cpp`). Zmiana ustawień rozmieszczenia w `VegetationLayer::World` /
  `PlacementSettings` wymaga tej samej zmiany w `vegexport::Input` budowanym w `TerrainGenerationSys::ImGuiSaving`. Modele: LOD-y z cache aplikacji
  (`vegetation::CacheKey` w `VegetationMesh`, wspólny z `VegetationModels`), węzły `<nazwa>_<wariant>_LOD<n>`; id wariantu w JSON = kolejność wariantów
  `LoadGltf`, więc liczba wariantów pliku musi zgadzać się z aplikacją (`Result::modelErrors`, jeśli nie).
- Import map: PNG/R16 nie przechowują zakresu wartości, więc wybiera go użytkownik (`valueRanges` w `MapImportSys.cpp`, albo „Custom (metres)”). `lodepng` nie zamienia koloru na szary
  ani niższej głębi na 16 bitów (błąd 56), dlatego PNG dekodujemy do RGBA o głębi pliku. Mapa żyje w `MapImportSys::map`, stąd pójdą przyszłe operacje na niej
  (Faza 6: jedna widoczna kopia robocza). Jednostki: mapa = metry / `metresPerUnit` (DEM i „Custom”: max(|min|, |max|, 1 m); zakres TerrainGen = 256, Noise = 64),
  więc tryby wyświetlania i culling (|h| ≤ 1,5) widzą ok. [-1, 1]; prawdziwe proporcje `trueHeightScale = metresPerUnit · worldUnitsPerTexel / piksel [m]`
  (piksel po zmniejszeniu), ustawiane po wczytaniu, „Vertical exaggeration” = `heightScale / trueHeightScale`. libtiff 4.7 nie zna znaczników GeoTIFF
  (33550, 33922, 34735) ani GDAL_NODATA (42113) — czytamy je jako pola anonimowe (licznik uint16 albo uint32, `GetArray`); testowy zapis rejestruje je
  `TIFFMergeFieldInfo`. Mapy w stopniach (GeoTIFF `GTModelTypeGeoKey` = 2, `.hgt`, `.asc` z małą komórką) bez reprojekcji: piksel w metrach przy szerokości środka.
  Projekt (B1, `MapProject.*`): plik trzyma metry, pamięć — jednostki; `metresPerUnit` liczone od nowa przy Open i przyjęciu mapy (max |h| kopii i oryginału, ≥ 1 m).
  Każda akcja, która zastępuje mapę (Open, Load, przyjęcie mapy, zamknięcie okna), idzie przez `MapImportSys::Request`: przy niezapisanych zmianach okno
  „Unsaved changes” (Save → akcja po udanym zapisie). Wyjątek: zmiana zakresu wartości przeładowuje ten sam plik bez pytania. Każda instalacja mapy podbija
  `mapVersion`; wynik wczytywania lub zapisu rozpoczęty przy starszej wersji jest odrzucany (zapis nie nadaje projektowi nazwy). `MapImportSys::Update` idzie
  co klatkę w każdym trybie (zapis przed zamknięciem z innego trybu), a `ImGuiModals` też. Mapa wysłana z TerrainGen/Noise = to, co eksport (`Pack`), w metrach.
  Edycja (B2, zakładka Edit, `MapImportEdit.cpp`): każda operacja = krok `mapedit::History` (mapy w metrach, stan sprzed operacji; krok morza bez mapy),
  `ApplyEdit` → `ShowMetres` (jednostki od nowa z max |h| kopii i oryginału, przewyższenie zachowane, `mapVersion++`); pierwsza zmiana mapy bez oryginału
  zapamiętuje go. Nowa mapa (`InstallMap`) czyści historię; `ShowUnits` jej nie rusza. Wygładzanie i skala w tle (`editJob`), erozja w `Update` (GL):
  jednostki erozji = metry / (piksel · 256), więc parametry TerrainGen (`DefaultWorldErosion`) działają tak samo na każdej mapie; krople odrzucają mapę
  z |h| > 127 jednostek (bufor stałoprzecinkowy). Rzeki: `OnMapChanged` ustawia flagę, `UpdateRivers` liczy `hydrology::ComputeOnMap` w tle (wysokości
  metry / 256, długości w pikselach, próbka = całe komórki), wynik starszej `mapVersion` odrzucany; woda przez `WaterSurface::DrawMap` (poziomy siatki
  przeliczane na jednostki mapy w `UploadRiverWater`), bez żłobienia koryt. Historia nie trafia do pliku, ustawienia rzek tak.
  Pędzle (C, sekcja „Sculpt” zakładki Edit, `MapImportSculpt.cpp`): wzory pędzli są dwa razy — `sculpt::Apply` (`Sculpt.cpp`, testy) i `sculpt_compute.shader`
  (`GpuSculpt`, kwadrat `sculpt::Area` wokół pędzla, czyta kopię kwadratu poszerzoną o zasięg SMOOTH) — zmiana w jednym wymaga zmiany w drugim, sprawdza
  `--gpu-check` (sekcja `sculpt:`, ≤ 1e-5). Punkt pod kursorem: `PickUnderCursor` po `view.Draw` czyta 1 piksel głębi (`glReadPixels`) i odwraca macierze
  `TerrainView::FrameBlock` (te same w 3D i 2D), `sculpt::PickTexel` zakłada mapę bez offsetu. Wejście myszy (przycisk, Shift + kółko) zapamiętuje
  `SculptInputImGui` w ramce ImGui (`io.MouseWheel` znika w `EndFrame`), `UpdateSculpt` (w `Update`, następna klatka) prowadzi pociągnięcie: w trakcie mapa
  żyje tylko na GPU (`view.LayerTexture`) w niezmienionych jednostkach (kolory, „Height scale” i izolinie stoją), przycięta do `Stroke::limit`: 1,5, a dla
  podnieś/obniż/szum `sculpt::StrokeLimit` = miejsce na 30 s malowania nad najwyższym punktem (płaska mapa ma jednostkę 1 m); tę samą granicę dostaje culling
  patchy w TCS (`TerrainRenderSettings::heightBound`, ustawiane w `MapImportSys::Draw`), `map` na CPU = stan sprzed pociągnięcia (FLATTEN bierze z niej cel),
  `Busy()` obejmuje pociągnięcie; puszczenie przycisku (też utrata fokusu, wyjście z trybu, M) = `ReadLayer` → `ApplyEdit` jednym krokiem historii.
  Działanie na klatkę × `deltaTime` (≤ 0,1 s). Każda klatka malowania liczy całą `surfaceMap` od nowa (na 8192² to koszt pełnego przebiegu).
  Biomy (D1, zakładka Biomes, `MapImportBiomes.cpp`): tabela, klimat, szumy T/H i materiały jak w TerrainGen (`mapbiomes::Settings::config`), ale C i M
  z mapy: C = podpisana odległość od brzegu (transformata odległości, ląd > morze) / `coastMetres` w [-1, 1] (bez morza 1), M = 2 · (max − min
  w promieniu `reliefRadiusMetres`) / `reliefMetres` − 1, W = 0; T i H = szumy w `climateOrigin + teksel · piksel` (metry), plamy materiałów w tekselach
  (`patchOrigin` = origin / piksel + teksel — na DEM 30 m plamy w metrach byłyby mniejsze od piksela). Klimat i elewacja biomów widzą
  wysokość h' = (metry − `baseMetres`) / `heightUnitMetres`, a płaskie morze mapy rzeczywistej (≤ 1 m pod poziomem) h' = `WaterHeight` · (1 − |C|), żeby
  biomy wody głębiały od brzegu. C/M liczy CPU w tle (`climateJob`, half2 → `GL_RG16F`) po każdej zmianie mapy/morza/ustawień klimatu (`climateStale`,
  ustawiane też w `OnMapChanged`; „Height of terrain 1” i „Height 0 at” tylko przebieg GPU), przebieg GPU (`GenerateMapBiomes` → warstwa BIOMES, id `GL_R32I`, splat) po zmianie tabeli/szumów/materiałów
  (`biomesStale`); w trakcie pociągnięcia pędzla nic się nie przelicza. Wzory są dwa razy: `MapBiomes.cpp` (+ `Biome.cpp`) i `map_biome_compute.shader`
  (+ `common/biomes.glsl`) — sprawdza `--gpu-check` (sekcja `mapbiomes:`). Modyfikatorów terenu i rzek w biomach mapy nie ma. Id czytane z GPU dopiero
  przy eksporcie (eksport map biomów zablokowany, gdy coś jest nieaktualne). Mapa wysłana z TerrainGen: `mapbiomes::FromGeneration` (tabela presetu,
  `heightUnitMetres` = metry na jednostkę, origin = `worldOrigin`), więc biomy wychodzą podobne do TerrainGen (C/M z mapy, nie z szumów).
  Pędzel biomów (D2, sekcja „Paint” zakładki Biomes, `MapImportBiomes.cpp`; wyklucza się ze Sculpt, to samo wejście myszy co pędzle terenu):
  `biomePaint` (`biomepaint::Layer`) = paleta nazw biomów (≤ 255, slot 0 = nic) + na teksel slot i siła 0–255; 0×0 do pierwszego pociągnięcia,
  potem rozmiar mapy, `paintTexture` (`GL_RG8UI`) istnieje dokładnie wtedy. Shader miesza wg siły s: kolor automatu → biomu, materiały = znormalizowana
  mieszanka automatu i znormalizowane warstwy biomu (przed plamami), id = biom od s ≥ 128/255; slot o nazwie spoza tabeli (`SlotBiomes` = -1) = automat,
  więc zmiana kolejności tabeli nie psuje malowania. Reguły dwa razy: `BiomePaint.cpp` (`Apply`, `PaintedMaterials`) i `map_biome_compute.shader`
  (`paintMaterials`) — sprawdza `--gpu-check` (`mapbiomes: paint…`, `region equals the whole map`). Dab: zmiana na CPU → `UploadRegion` prostokąta →
  `RunBiomePass(prostokąt)` (`MapBiomeInput::region`; świeży cel BIOMES/splat/id = cała mapa). Pociągnięcie = krok historii z warstwą sprzed niego
  (`mapedit::Step::paint`, Cofnij takiego kroku nie liczy klimatu ani jednostek), „Clear painting” też. Nowa mapa czyści warstwę, edycja terenu ją zostawia,
  reload tego samego pliku zostawia przy tym samym rozmiarze; projekt v4 trzyma ją, gdy coś jest pomalowane.
  Pokrycie terenu (D3, sekcja „Land cover” zakładki Biomes): źródło `mapbiomes::Source::LAND_COVER` = klasy z plików (`landCoverFiles`, w projekcie też
  `landCoverFilesRelative` względem folderu projektu) ograniczają biomy: klasa ma listę nazw biomów → maska 64 bitów (`ClassMasks`, 0 = wszystkie),
  klimat wybiera najbliższy biom z dozwolonych, a kolor i materiały mieszają tylko dozwolone (`biome::Nearest/MixColor/MixMaterials` z maską =
  `effectiveMask`/`nearestAllowed` w `map_biome_compute.shader`, obraz klas binding 6, SSBO masek binding 2; malowanie idzie po nich). Mapa klas
  (`landcover::Compute`) liczona w tle (`landCoverJob`) po nowej mapie (`landCoverMapId`), zmianie plików lub georeferencji (`landCoverFilesVersion`),
  nie po edycji terenu; wynik → `classTexture` (`GL_R8UI`, filtrowanie NEAREST — tekstura całkowita z LINEAR jest niekompletna). Georeferencja mapy
  (`MapImportSys::georeference`, `geo::GeoReference`, z pliku mapy albo „Take georeference from file…”) w systemie spoza `geo::Supported` = rozciągnięcie
  pierwszego pliku. Nowa mapa z pliku zachowuje źródło, pliki i klasy. Tryb LAND COVER rysuje kolory klas (`TerrainView::SetLandCover`).
- Kamera edytora (`EditorCamera`, wszystkie tryby, tylko widok 3D i bez M): prawy przycisk = rozglądanie (z WASD lot), Alt + lewy = orbita, środkowy = przesuwanie,
  kółko = przód/tył (Shift + kółko nigdy — zostaje dla promienia pędzla). Punkt obrotu i odległość przesuwania: `AfterDraw` czyta głębię pod kursorem po `Draw`
  (macierze `camera.GetViewProjection()`), bez trafienia punkt przed kamerą. Matematyka w `CameraMath.*` (`ptg_core`, testy). Lewy przycisk bez Alt należy do pędzli.
- Rzeki (tylko TerrainGen, zakładka „Rivers”, `WorldConfig::rivers`, domyślnie `NONE`): `carveRivers` (`common/rivers.glsl`) to ostatni krok wysokości w obu ścieżkach
  (`height_compute` i `biome_compute` z `writeHeight`), po modyfikatorach; biomy wybiera wysokość sprzed rzek. Generacja dostaje `const RiverInput*` (config + `RiverMap`);
  `SampleTerrain` zawsze bez rzek (z niej liczy się sieć — inaczej sprzężenie). Każdy dispatch ustawia `riverMethod` (program pamięta uniformy).
  REGION: `TerrainGenerationSys::UpdateRivers` próbkuje teren (odczyt z GPU) i liczy `hydrology::Compute` w tle (`GenerationJob`), po przyjściu sieci ustawia
  `pending.terrain` bezpośrednio — nie `RequestTerrain`, które oznacza sieć jako nieaktualną (pętla). `RequestTerrain` i `RequestBiomes` (z modyfikatorami) unieważniają sieć.
  Region leży wokół środka mapy częściowej; w świecie nieskończonym (checkbox „Follow camera”, `riversFollowCamera`) przeskakuje za kamerą na stałej siatce
  (krok ¼ regionu w całych komórkach, `hydrology::PlaceRegion` / `LeftRegion`): `UpdateRivers(camera)` ustawia wtedy `riversStale` bezpośrednio (teren się nie
  zmienił), środek i krok ostatniej sieci = `riverRegion` (krok zapamiętany z siecią, więc przeciąganie „Region size” nie przesuwa regionu przed puszczeniem),
  bez śledzenia region w świecie nieskończonym zostaje w miejscu (edycja liczy sieć tam), pozycja mapy pod kamerą = `MapPositionUnder`.
  Koryto w teksturze to wektor do najbliższego punktu biegu (dokładny przy próbkowaniu dwuliniowym) plus odległość skalarna, bo wektor między dwiema rzekami
  maleje do 0 — shader bierze `max(|wektor|, odległość − komórka)`; ten sam odczyt na CPU = `hydrology::WaterLevelAt` (roślinność nie stoi w wodzie rzek i jezior). NOISE: tablica permutacji 6 (`WorldInput` ma 7 tablic),
  nie żłobi dna morza (wygaszanie na głębokości koryta pod lustrem). `channelAt` raz na teksel, potem `carveRivers` / `riverHumidity` (wilgotność dla biomów, dodawana po `applyClimate`
  w obu ścieżkach — zmienia biomy, więc i modyfikatory; dlatego test „rzeki tylko obniżają” idzie z wilgotnością 0) / `riverBedShare` (materiał dna w splat, po `finishMaterials`).
  `GenerateBiomes` też dostaje `RiverInput` (wilgotność, dno), żeby zgadzał się z `GenerateWorld`. Woda rzek i jezior (REGION): `hydrology::BuildWaterMesh` (w zadaniu w tle) → siatka
  w `RiverMap`, rysowana przez `WaterSurface` po morzu tą samą kopią sceny (`water_mesh_vertex.shader` + wspólny FS, `meshWater`: głębokość z kopii głębi, fale niesione przepływem);
  brzeg przycina test głębi z terenem, na skończonej mapie `clipRect`. Poziom wody zostaje tylko w jeziorach i korytach (dno wyżłobione); suche narożniki
  i brzegi rzek schodzą pod grunt próbki (`Network::ground`), inaczej tafla wisiała nad niższym terenem (odpływ jeziora, zbocze obok rzeki). Nowy parametr rzek = `RiverConfig` + presety
  (`RiversToJson`/`ReadRivers`/`CheckRivers`) + `PresetTests` + UI; zmiana shadera → `--gpu-check` (sekcja `rivers:`).
- **Generacja jest tylko w GLSL** (`ComputeShaders/`, `common/noise.glsl`, `common/simplex.glsl`, `common/surface.glsl`). Nowe pole configu szumu trzeba
  dopisać do `NoiseParams` (`noise_params.glsl`), `GpuNoiseParams`/`SetNoise` (`GpuHeightGenerator.cpp`), presetów i UI, a potem uruchomić `--gpu-check`.
- LOD chunków: poziom L bierze co 2^L-tą próbkę (`MapArea::step`, potęga dwójki, więc próbki poziomów się pokrywają). Siatka chunka jest jedna, skalowana w XZ uniformem
  `meshScale` (VS), a skirty (wierzchołki z `aPos.y = -1`) TES opuszcza o `skirtDepth`; `SetFrameUniforms` ustawia oba na neutralne (1 i 0) dla `TerrainView`.
  Culling (chunki na CPU w `ChunkWorld`, patche w TCS) zakłada |wysokość| ≤ 1,5 (`heightBound` w `ChunkWorld.cpp` = uniform `heightBound` w TCS z
  `TerrainRenderSettings::heightBound`, domyślnie 1,5); mapa o większych wartościach wymaga zmiany obu, inaczej teren przy krawędzi ekranu zniknie. Nie skaluj `model` niejednorodnie — `normalMatrix` i mapa pochodnych (liczona z krokiem `worldUnitsPerTexel · 2^L`) zakładają jednostki świata. Stara roślinność — w historii (`fb8b840`), opis w ROADMAP, Faza 4.

## Kierunek rozwoju (skrót z ROADMAP)

Cel: edycja parametrów w czasie rzeczywistym + maksymalna wydajność + biomy i roślinność.
Kolejność: naprawy krytyczne (Faza 0) → porządki/architektura (Faza 1) → generacja na GPU (compute shaders)
i system chunków z LOD (Faza 2) → biomy ciągłe z blendingiem i teksturowaniem (Faza 3, domknięta 2026-11-13 kontynentami, tabelą 18 biomów i rzeźbą terenu) — wszystko to zamknięte →
roślinność instancjonowana (Faza 4, następna) → erozja GPU, woda, eksport (Faza 5).
