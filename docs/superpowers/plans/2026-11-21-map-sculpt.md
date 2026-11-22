# Faza 6 C: pędzle GPU w Imported map — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modelowanie kopii roboczej Imported map pędzlami (podnieś, obniż, wygładź, spłaszcz, szum) malowanymi kursorem w widoku 3D i na mapie 2D; jedno pociągnięcie = jeden krok historii.

**Architecture:** Matematyka pędzli i rzutowanie kursora w nowym `terrainGeneration/Sculpt.*` (`ptg_core`, testy, wersja CPU pędzli = wzorzec). Pędzel na GPU (`opengl/GpuSculpt.*` + `sculpt_compute.shader`) pisze wprost do warstwy TERRAIN `TerrainView` w kwadracie wokół kursora; `--gpu-check` porównuje GPU z CPU. `MapImportSys` (zakładka Edit, sekcja Sculpt): punkt pod kursorem z głębi sceny (odczyt 1 piksela po narysowaniu terenu, `glm::unProject` tymi samymi macierzami co rysowanie — działa tak samo w 3D i 2D), pociągnięcie w `Update`, po puszczeniu przycisku odczyt mapy z GPU → `ApplyEdit`.

**Tech Stack:** C++20, OpenGL 4.5 compute (image load/store R32F), glm, Dear ImGui, doctest.

**Spec:** zakres zatwierdzony w rozmowie 2026-11-21 (ROADMAP, Faza 6, punkt C + odpowiedź właściciela „zgadza się”: 3D i 2D). Brak pliku specyfikacji (właściciel nie chce `docs/superpowers/specs`).

## Global Constraints

- Pędzle: RAISE, LOWER, SMOOTH, FLATTEN, NOISE. Parametry: promień w pikselach mapy (1–512), siła w metrach na sekundę (RAISE/LOWER/NOISE; SMOOTH/FLATTEN: część drogi do celu na sekundę, 0–10), twardość 0–1 (zanik `1 − smoothstep(hardness·R, R, d)`), skala szumu w pikselach.
- Działanie pędzla na klatkę skalowane czasem klatki (`deltaTime`, ograniczony do 0,1 s), więc tempo nie zależy od FPS.
- Malowanie: lewy przycisk myszy nad sceną (nie nad panelem ImGui: `!io.WantCaptureMouse`), nie w trybie myszy kamery (M). Ctrl przy RAISE/LOWER odwraca kierunek. Shift + kółko zmienia promień.
- FLATTEN: cel = wysokość w punkcie, w którym pociągnięcie się zaczęło.
- Jedno pociągnięcie = jeden krok historii przez istniejące `ApplyEdit` (etykieta „Sculpt: <pędzel>”); w trakcie pociągnięcia mapa żyje tylko na GPU, `map` na CPU zostaje stanem sprzed pociągnięcia.
- W trakcie pociągnięcia wysokość przycinana do |h| ≤ 1,5 jednostki mapy (granica cullingu `HEIGHT_BOUND`); po puszczeniu `ShowMetres` przelicza jednostki, więc następne pociągnięcie może iść wyżej.
- Kółko pędzla rysowane na terenie (rzut punktów okręgu jak `MapLabels`, lista tła ImGui).
- Działa w 3D i na mapie 2D (`renderSettings.map2d`).
- Zapis do warstwy wysokości tylko przez `TerrainView::LayerTexture` (unieważnia `surfaceMap`). OpenGL tylko w wątku głównym.
- Konwencje z CLAUDE.md: `(width, height)`, `Grid<T>`, komentarze po angielsku, `/W4 /WX`, nowe `.cpp` w `CMakeLists.txt`, nowa operacja generacji/GPU = przypadek w `--gpu-check`.

## Review Focus

1. Puszczenie przycisku poza oknem / nad panelem, utrata fokusu okna, przełączenie trybu w trakcie pociągnięcia: pociągnięcie kończy się krokiem historii, nie zostaje „wiszące” (Busy na zawsze).
2. Kursor nad niebem (głębia 1,0) lub poza mapą: brak punktu, nic się nie maluje, brak kółka; pędzel częściowo poza mapą działa tylko na tekselach mapy (brzeg, mapa niekwadratowa).
3. Undo/Ctrl+Z, Open/Load, operacja z zakładki Edit w trakcie pociągnięcia: zablokowane (`Busy()`), nie mieszają stanów CPU/GPU.
4. Mapa 8192²: koszt klatki pociągnięcia zależy od promienia, nie od mapy; odczyt całej mapy tylko raz, po puszczeniu.
5. Duże podniesienie ponad maksimum mapy: przycięcie do 1,5 w trakcie, po puszczeniu poprawne metry (bez utraty wysokości ponad stare maksimum poza przycięciem).

---

### Task 1: `Sculpt` — pędzle na CPU i rzutowanie kursora

**Files:**
- Create: `ProceduralTerrainGenerator/src/terrainGeneration/Sculpt.h`, `Sculpt.cpp`
- Create: `ProceduralTerrainGenerator/tests/SculptTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces (namespace `sculpt`):
  - `enum class Brush { RAISE, LOWER, SMOOTH, FLATTEN, NOISE };` `const char* BrushName(Brush);` („Raise”, „Lower”, „Smooth”, „Flatten”, „Noise”)
  - `struct BrushConfig { Brush brush = Brush::RAISE; float radius = 32.0f; float strength = 10.0f; float hardness = 0.5f; float noiseScale = 16.0f; };`
  - `struct Dab { glm::vec2 center; float amount; float target; uint32_t seed; float limit; };` — jedno użycie pędzla: środek w tekselach, `amount` = RAISE/LOWER/NOISE: jednostki mapy (metry · dt / metresPerUnit, znak = kierunek), SMOOTH/FLATTEN: część drogi `min(strength · dt, 1)`; `target` = cel FLATTEN w jednostkach; `limit` = 1,5.
  - `float Falloff(float distance, float radius, float hardness);` — 1 w środku, 0 od `radius`; `hardness ≥ 1` = ostra krawędź (1 do R).
  - `glm::ivec4 Area(const Dab&, float radius, glm::ivec2 mapSize);` — x, y, szerokość, wysokość prostokąta tekseli w zasięgu, przycięte do mapy (szerokość 0 = poza mapą).
  - `float NoiseAt(glm::vec2 texel, float scale, uint32_t seed);` — szum wartości z hasha (interpolacja smoothstep, 2 oktawy) w [-1, 1]; ta sama formuła w `sculpt_compute.shader`.
  - `void Apply(HeightField& units, const BrushConfig&, const Dab&);` — wzorzec GPU: czyta z kopii przed zmianą (SMOOTH: średnia 3×3 tekseli w odstępie `max(1, round(R / 16))`, brzeg = najbliższy teksel mapy), wynik przycięty do `[-limit, limit]`.
  - `std::optional<glm::vec2> PickTexel(glm::vec2 windowPixel, float depth, glm::ivec4 viewport, const glm::mat4& viewProjection, float modelScale, glm::ivec2 mapSize);` — `glm::unProject` → świat → teksel `(world.xz / modelScale − worldTexelOrigin) / worldUnitsPerTexel` (`worldTexelOrigin = (−size / 2 + 0,5) · 4`, mapa bez offsetu, X → x, Z → y); `nullopt` dla `depth ≥ 1` lub teksela poza `[-0,5, size − 0,5]`.
  - `glm::vec3 TexelToWorld(glm::vec2 texel, float heightUnits, float heightScale, float modelScale, glm::ivec2 mapSize);` — odwrotność (do kółka pędzla).

- [ ] **Step 1: Testy** (mapy niekwadratowe, np. 40×24):
  - `"Falloff is 1 at the centre and 0 at the radius"`, `"Hard brush keeps full strength to the edge"` (hardness 1 → `Falloff(0,99R) == 1`).
  - `"Raise adds amount at the centre and nothing outside the radius"`; `"Lower takes it away"`.
  - `"Flatten moves towards the target"` (amount 1 w środku → dokładnie `target`; 0,5 → połowa drogi).
  - `"Smooth flattens a spike and keeps a flat map"`.
  - `"Noise is deterministic and bounded"` (ten sam seed → te same wartości, |n| ≤ 1, inny seed → inne).
  - `"Brush near the border touches only map texels"` (środek (−3, 5), promień 8 → `Area` przycięte, `Apply` bez wyjścia poza tablicę, zmienione tylko teksele x < 5).
  - `"Heights are clamped to the limit"` (RAISE ponad 1,5 → 1,5).
  - `"Pick and TexelToWorld round trip in 3D and 2D"` — perspektywa (`glm::perspective` + `lookAt`) i ortho z góry jak `SetFlatCamera`: rzut `TexelToWorld` → okno (`glm::project`) → `PickTexel` daje ten sam teksel (±0,01); `depth = 1` → `nullopt`; punkt poza mapą → `nullopt`.
- [ ] **Step 2:** Build + ctest → FAIL (brak `Sculpt.h`).
- [ ] **Step 3:** Implementacja.
- [ ] **Step 4:** Build + ctest → PASS.
- [ ] **Step 5:** Ledger.

### Task 2: `GpuSculpt` + `sculpt_compute.shader` + `--gpu-check`

**Files:**
- Create: `ProceduralTerrainGenerator/src/opengl/GpuSculpt.h/.cpp`, `ProceduralTerrainGenerator/res/shaders/ComputeShaders/sculpt_compute.shader`
- Modify: `opengl/GpuHeightCheck.cpp` (+ wywołanie w zestawie sprawdzeń), `CMakeLists.txt`

**Interfaces:**
- Consumes: `sculpt::BrushConfig/Dab/Area/Apply/NoiseAt` (Task 1).
- Produces: `class GpuSculpt { public: bool Initialize(); void ReloadShaders(); void Apply(const sculpt::BrushConfig&, const sculpt::Dab&, TextureClass& heights); };`
  - `Apply`: prostokąt `Area` (pusty → nic), kopia prostokąta do tekstury roboczej R32F (`glCopyImageSubData`, tekstura rośnie do potrzebnego rozmiaru), dispatch 16×16 na prostokąt: shader czyta wysokości z kopii (SMOOTH: sąsiedzi spoza prostokąta — z kopii poszerzonej o odstęp próbek, przycięte do mapy), pisze `imageStore` do `heights`; uniformy: rodzaj, środek, promień, twardość, amount, target, seed, skala szumu, limit, rozmiar mapy, początek prostokąta. Bariera `GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` po dispatchu.
- `--gpu-check`: `CheckSculpt` — mapa 200×120 (niekwadratowa, wzór z sinusów), dla każdego pędzla: GPU `Apply` na teksturze vs CPU `sculpt::Apply` na tej samej mapie, środek przy krawędzi i w środku, max |różnica| ≤ 1e-5; komunikat w stylu pozostałych sekcji (`sculpt: ...`).

- [ ] **Step 1:** `CheckSculpt` w `GpuHeightCheck.cpp` (najpierw z pustym `GpuSculpt::Apply`) → `--gpu-check` FAIL w sekcji `sculpt:`.
- [ ] **Step 2:** Shader + `GpuSculpt`.
- [ ] **Step 3:** `out\build\x64-release\ProceduralTerrainGenerator.exe --gpu-check` → `All checks passed`; ctest → PASS.
- [ ] **Step 4:** Ledger.

### Task 3: Sculpt w Imported map — kursor, pociągnięcie, historia, kółko pędzla

**Files:**
- Modify: `subSystems/MapImportSys.h/.cpp`, `subSystems/MapImportEdit.cpp`, `utility/Camera.h` (`bool MouseControl() const`), `subSystems/TerrainGenApp.cpp` (przekazanie `deltaTime` do `MapImportSys::Update`, jeśli nie jest dostępny)

**Interfaces:**
- Consumes: `sculpt::*` (Task 1), `GpuSculpt` (Task 2), `ApplyEdit`, `WorkingMetres`, `Busy` (B2).
- Produces (w `MapImportSys`):
  - `sculpt::BrushConfig brush; bool sculptOn = false; GpuSculpt sculptor; std::optional<glm::vec2> hoverTexel; std::optional<Stroke> stroke;` gdzie `struct Stroke { HeightField before; float target; uint32_t seed; };`
  - `Busy()` obejmuje `stroke.has_value()`.
  - `Draw`: gdy `sculptOn` i mysz nad viewportem — po `view.Draw` odczyt głębi 1 piksela pod kursorem (`glReadPixels(..., GL_DEPTH_COMPONENT, GL_FLOAT)`), `hoverTexel = sculpt::PickTexel(...)` z macierzami z `TerrainView::FrameBlock(camera, light, renderSettings, flatMin, flatMax)` (to samo co rysowanie, także 2D); punkty okręgu (48) przez `TexelToWorld` z wysokością z `map` → ekran (jak `MapLabels::Place`) do `brushOutline`.
  - `Update(camera, active, deltaTime)`: przy `sculptOn && active && !camera.MouseControl()` — początek pociągnięcia (lewy przycisk wciśnięty nad sceną, `hoverTexel`, `!Busy()`): `stroke = { WorkingMetres(), target = map[hover], seed = ++strokeSeed }`; w trakcie: `sculptor.Apply(brush, dab, view.LayerTexture(TERRAIN, w, h))` co klatkę z `hoverTexel` (brak punktu = klatka bez dabu); koniec (przycisk puszczony albo `!active`): `view.ReadLayer(TERRAIN)` × `metresPerUnit` → `ApplyEdit(metres, sea, { "Sculpt: " + BrushName, std::move(stroke->before), sea })`, `stroke.reset()`.
  - UI: sekcja „Sculpt” na górze zakładki Edit: checkbox „Sculpt with the mouse”, wybór pędzla (5 przycisków radio), suwaki promienia (log 1–512 px), siły (m/s; dla SMOOTH/FLATTEN „Speed” 0–10), twardości, skali szumu (tylko NOISE); podpowiedź „LMB paint, Ctrl inverts, Shift+wheel radius”. Shift + kółko nad sceną: promień × 1,1^kółko.
  - Kółko: w `ImGuiRightPanel` (co klatkę w trybie) `GetBackgroundDrawList()->AddPolyline(brushOutline, ..., closed)` z przycięciem do viewportu, kolor zależny od pędzla.
  - `InstallMap` kończy/odrzuca trwające pociągnięcie (nie powinno się zdarzyć przez `Busy`, ale bez wiszącego stanu); `ReloadShaders` też `sculptor.ReloadShaders()`; `Initialize` woła `sculptor.Initialize()`.
- [ ] **Step 1:** Implementacja.
- [ ] **Step 2:** Build oba presety + ctest + `--gpu-check` → PASS.
- [ ] **Step 3:** Ledger (weryfikacja malowania w aplikacji przez właściciela).

### Task 4: Kamera w stylu edytora (wymaganie właściciela 2026-11-21)

Przy edycji sterowanie M (przełączana mysz kamery) i widok terenu są niewygodne; potrzebne obracanie widoku myszką jak w Unity.

**Files:**
- Modify: `utility/Camera.h/.cpp` (+ czysta matematyka orbity/przesunięcia w `ptg_core`, jeśli da się ją wydzielić bez GLFW — wtedy testy), `subSystems/TerrainGenApp.cpp`

**Interfaces / zachowanie** (we wszystkich trybach, tylko gdy kursor nad sceną — `!io.WantCaptureMouse` — i M wyłączone):
- Przytrzymany prawy przycisk = rozglądanie (yaw/pitch z ruchu myszy, kursor ukryty na czas przytrzymania), z WASD/Space/Ctrl = lot jak dziś.
- Alt + lewy = orbita wokół punktu: punkt pod kursorem w chwili wciśnięcia (głębia sceny, jak `sculpt::PickTexel` — w Imported map), a gdy go brak — punkt w stałej odległości przed kamerą.
- Środkowy przycisk = przesuwanie kamery w płaszczyźnie ekranu (tempo proporcjonalne do odległości od punktu pod kursorem / prędkości kamery).
- Kółko = przybliżanie wzdłuż kierunku patrzenia (krok proporcjonalny do prędkości kamery); Shift + kółko zostaje dla promienia pędzla, gdy pędzel włączony.
- Lewy przycisk bez Alt zostaje dla pędzla; w widoku 2D: prawy/środkowy = przesuwanie mapy, kółko = zoom kadru (`SetFlatViewBounds`) — jeśli prosto, inaczej osobny punkt w ROADMAP.
- Dotychczasowe M (mysz kamery) i Esc działają jak dziś; checkbox „Editor camera (mouse)” w zakładce Camera, domyślnie włączony.
- [ ] **Step 1:** Testy matematyki orbity (obrót wokół punktu zachowuje odległość, pitch ograniczony) i przesunięcia, jeśli wydzielone.
- [ ] **Step 2:** Implementacja. **Step 3:** Build oba presety + ctest → PASS. **Step 4:** Ledger (sprawdzenie w aplikacji przez właściciela).

### Task 5: Dokumentacja

**Files:** `CLAUDE.md` (struktura: `Sculpt.*`, `GpuSculpt.*`, `sculpt_compute.shader`; pułapki: odczyt głębi w `Draw`, przycięcie 1,5, pociągnięcie = krok historii, `--gpu-check` sekcja `sculpt:`), `docs/ROADMAP.md` (C 🟡 z datą i listą do sprawdzenia w aplikacji).
- [ ] **Step 1:** Edycja. **Step 2:** Ledger; potem przegląd całej gałęzi (sonnet, wg `~/.claude/CLAUDE.md`).
