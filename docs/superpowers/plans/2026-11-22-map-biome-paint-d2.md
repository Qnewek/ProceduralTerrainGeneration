# Pędzel biomów w Imported map (D2) — plan implementacji

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Malowanie biomów pędzlem z miękkimi krawędziami na warstwie nad automatem (klimat z terenu), z Cofnij/Ponów, zapisem w projekcie i eksportem.

**Architecture:** Warstwa malowania na CPU (`biomepaint::Layer`: paleta nazw biomów + 2 bajty na teksel: slot i siła) zmieniana pędzlem w `Update`; do GPU idzie prostokąt zmieniony przez dab (`GL_RG8UI`), a `map_biome_compute.shader` miesza automat z malowanym biomem według siły i liczy tylko prostokąt wokół pędzla. Pociągnięcie = krok `mapedit::History` z kopią warstwy; warstwa w `.ptgmap` v4.

**Tech Stack:** C++20, OpenGL 4.5 compute, ImGui, doctest, zlib, nlohmann/json.

**Spec:** projekt D2 zatwierdzony w czacie 2026-11-22 (decyzja właściciela: krawędzie miękkie — siła 0–1 na teksel, twardość pędzla), na podstawie projektu etapu D (ROADMAP, Faza 6, punkt D: warstwa nadpisań nad automatem, gumka = automat, biom z listy tabeli).

## Global Constraints

- Paleta po nazwach: zmiana kolejności tabeli nie psuje malowania; nazwa nieobecna w tabeli = automat. Najwyżej 255 nazw (slot 0 = niepomalowane).
- Malowanie B na A: najpierw siła A spada do 0, potem rośnie B (A → automat → B). Gumka: siła w dół do 0, wtedy slot 0.
- Kolor = mix(automat, kolor B, s); materiały = mix(znormalizowana mieszanka automatu, znormalizowane warstwy B, s), potem plamy jak dotąd; id = B, gdy s ≥ 128/255 i B jest w tabeli, inaczej automat.
- Reguły mieszania dwa razy (CPU `biomepaint::*` z testami i shader), `--gpu-check` porównuje.
- Pociągnięcie = jeden krok historii; „Clear painting” = krok historii. Nowa mapa czyści malowanie, edycja terenu go nie rusza.
- `.ptgmap` `currentVersion = 4`; wersje 1–3 nadal czytane (bez malowania).
- Pędzel biomów i Sculpt wykluczają się (jeden lewy przycisk). Shift + kółko = promień aktywnego pędzla, Ctrl = gumka.
- Konwencje repo (CLAUDE.md): `(width, height)`, komentarze po angielsku, nowe `.cpp` w `CMakeLists.txt`, edycje plików narzędziami Edit/Write.

## Review Focus

1. Biom usunięty lub przemianowany w tabeli po malowaniu: te teksele pokazują automat (bez złego id, bez wyjścia poza tabelę w shaderze); przywrócenie nazwy przywraca malowanie.
2. Cofnij/Ponów malowania: przywraca warstwę i przelicza całe biomy, ale nie liczy od nowa klimatu (C/M) ani nie podbija jednostek mapy.
3. Malowanie, gdy klimat się jeszcze liczy albo tekstura klimatu ma inny rozmiar (świeżo otwarta mapa): nic się nie psuje, malowanie widać po nadejściu klimatu.
4. Paleta pełna (255 nazw): wybór kolejnego biomu nie maluje i mówi dlaczego, zamiast nadpisywać slot.
5. Eksport w trakcie pociągnięcia: zablokowany jak przy nieaktualnych biomach (id z innego stanu warstwy).

Testy: 1 → Task 1 (`"Slots map to the table by name"`) + Task 4 (`mapbiomes: paint follows names`), 2 → Task 2 (`"Paint step restores only the paint"`), 4 → Task 1 (`"Full palette gives no slot"`), 3 i 5 → Task 5 (warunki w kodzie + sprawdzenie ręczne).

---

### Task 1: Warstwa malowania na CPU (`BiomePaint.*`)

**Files:**
- Create: `ProceduralTerrainGenerator/src/terrainGeneration/BiomePaint.h`, `BiomePaint.cpp`
- Create: `ProceduralTerrainGenerator/tests/BiomePaintTests.cpp`
- Modify: `CMakeLists.txt` (`BiomePaint.cpp` w `ptg_core`, test w `ptg_tests`)

**Interfaces:**
- Produces (namespace `biomepaint`):
  - `inline constexpr int maxPalette = 255;`
  - `struct Layer { int width = 0, height = 0; std::vector<std::string> palette; std::vector<uint16_t> texels; bool Empty() const; size_t Bytes() const; };` — `palette[i]` = nazwa slotu `i + 1`; texel = slot (młodszy bajt, 0 = niepomalowany) | siła 0–255 (starszy bajt; ten układ to `GL_RG8UI` po wgraniu: R = slot, G = siła). `Empty()` = brak tekseli albo żaden nie ma siły > 0.
  - `Layer Make(int width, int height);`
  - `uint8_t SlotOf(Layer& layer, const std::string& name);` — istniejący slot albo nowy na końcu palety; 0, gdy paleta pełna.
  - `struct Dab { glm::vec2 center; float radius = 32.0f; float hardness = 0.5f; float amount = 0.0f; uint8_t slot = 0; bool erase = false; };` — `amount` = zmiana siły (0–1) przy pełnym zaniku.
  - `glm::ivec4 Apply(Layer& layer, const Dab& dab);` — zanik `sculpt::Falloff(odległość, radius, hardness)`, d = falloff · amount · 255 zaokrąglone; zwraca zmieniony prostokąt (x, y, w, h), w = h = 0, gdy nic.
  - `std::array<int, 256> SlotBiomes(const Layer& layer, const std::vector<biome::Biome>& table);` — [0] = −1, [i] = indeks pierwszego biomu o nazwie `palette[i-1]` albo −1.
  - `biome::MaterialWeights PaintedMaterials(const biome::MaterialWeights& automatic, const biome::Biome& painted, float strength);` — oba znormalizowane do sumy 1, potem mix (wzór z Global Constraints).

- [ ] **Step 1: Testy** (`BiomePaintTests.cpp`, mapa 40×24):
  - `"Paint raises the strength up to full"`: `Make(40,24)`, slot = `SlotOf(l,"Forest")` == 1; 5 × `Apply` z `{ {10,10}, 4, 1.0f, 0.3f, 1 }` → teksel (10,10): slot 1, siła 255; (20,10): 0; prostokąt pierwszego `Apply` zawiera (6..14, 6..14) i mieści się w mapie.
  - `"Painting another biome fades the first one out first"`: (10,10) pomalowany slotem 1 do 255; jeden dab slot 2 amount 0.4 → slot 1, siła 255 − 102 = 153; trzy kolejne → slot 2 z siłą > 0 (przejście przez 0).
  - `"Eraser goes back to automatic"`: po malowaniu dab `erase = true` amount 1 → siła 0, slot 0, `Empty()`.
  - `"Soft edge is weaker than the centre"`: hardness 0, radius 8, amount 0.5 → siła(10,10) > siła(15,10) > 0, siła(19,10) == 0.
  - `"Full palette gives no slot"`: 255 różnych nazw → `SlotOf(l, "x256") == 0`, `SlotOf(l, "name0") == 1`.
  - `"Slots map to the table by name"`: paleta {"B","Gone","A"}, tabela {A,B} → `{-1, 1, -1, 0, -1...}`.
  - `"Painted materials mix the normalised mixes"`: automat {m0: 2, m1: 2}, biom z warstwami {m2: 1}, s = 0.25 → m0 = m1 = 0.375, m2 = 0.25; s = 0 → automat znormalizowany; s = 1 → tylko m2.
- [ ] **Step 2:** build.bat → nowe testy FAIL / nie kompilują się.
- [ ] **Step 3:** Implementacja `BiomePaint.cpp` wg Interfaces (malowanie: slot == dab.slot albo siła 0 → slot = dab.slot, siła = min(255, s + d); inny slot → siła = max(0, s − d), przy 0 slot = 0; gumka: max(0, s − d), przy 0 slot = 0).
- [ ] **Step 4:** ctest → PASS.

### Task 2: Historia z warstwą malowania (`MapEdit`)

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/MapEdit.h/.cpp`, `tests/MapEditTests.cpp`, `src/subSystems/MapImportEdit.cpp` (`StepHistory`)

**Interfaces:**
- Consumes: `biomepaint::Layer` (Task 1).
- Produces:
  - `mapedit::Step` + pole `std::optional<biomepaint::Layer> paint;` (none = operacja nie zmieniła malowania); `BytesOf` liczy `paint->Bytes()`.
  - `struct Restored { bool map = false; bool seaLevel = false; bool paint = false; };`
  - `std::optional<Restored> History::Undo(HeightField& map, std::optional<float>& seaLevel, biomepaint::Layer& paint);` i tak samo `Redo` — zamienia to, co krok trzyma (morze zawsze, jak dziś); `seaLevel` = wartość się zmieniła; none = nic do cofnięcia.

- [ ] **Step 1: Testy:** istniejące testy historii przepisane na nowe sygnatury (zachowanie bez zmian); nowe `"Paint step restores only the paint"` (krok `{ "Paint", nullopt, sea, warstwa A }`, bieżąca warstwa B → Undo: warstwa == A, mapa bez zmian, `Restored{false,false,true}`; Redo → B) i `"Paint counts in the history memory"` (`Bytes()` == `A.Bytes()` po Push).
- [ ] **Step 2:** FAIL. **Step 3:** implementacja; `MapImportSys::StepHistory`: `ShowMetres` tylko przy `map || seaLevel`, przy `paint` → `UsePaint(std::move(paint))` z Task 5 (do czasu Task 5: pole `biomePaint` w `MapImportSys` + `biomesStale = true`). **Step 4:** ctest PASS.

### Task 3: Projekt `.ptgmap` v4 z malowaniem

**Files:**
- Modify: `ProceduralTerrainGenerator/src/terrainGeneration/MapProject.h/.cpp`, `tests/MapProjectTests.cpp`

**Interfaces:**
- Produces: `mapproject::Project::paint` (`biomepaint::Layer`, pusty = brak); `currentVersion = 4`; w nagłówku `"biomePaint": { "palette": [...] }` i blok `"biomePaint"` (zlib surowych `uint16` little-endian), zapisywane tylko gdy `!paint.Empty()`.

- [ ] **Step 1: Testy:** `"Project keeps its biome paint"` (round trip palety i tekseli bit w bit, `currentVersion == 4`); `"Version 3 project has no biome paint"` (wersja podmieniona na 3 jak w teście v2 → `paint.Empty()`); `"Paint of another size is not saved"` (Serialize → false, błąd); `"Paint with a slot past its palette fails"` (ręcznie uszkodzony slot > palette.size() po Serialize przez zmianę palety na krótszą w nagłówku → Deserialize false, `out` nietknięty).
- [ ] **Step 2:** FAIL. **Step 3:** implementacja (blok czytany do `std::vector<uint16_t>` rozmiaru width·height; paleta ≤ 255; slot > rozmiar palety = „The biome paint data is damaged”). **Step 4:** ctest PASS.

### Task 4: GPU — malowanie w shaderze i przebieg na prostokącie

**Files:**
- Modify: `res/shaders/ComputeShaders/map_biome_compute.shader`, `src/opengl/GpuHeightGenerator.h/.cpp`, `src/opengl/Shader.h/.cpp`, `src/opengl/TextureClass.h/.cpp`, `src/opengl/GpuHeightCheck.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `MapBiomeInput` + `const TextureClass* paint = nullptr;` (`GL_RG8UI`, rozmiar mapy; nullptr = brak → generator binduje własną teksturę 1×1 zer) i `std::array<int, 256> paintBiomes{}` (z `SlotBiomes`), `glm::ivec4 region = glm::ivec4(0, 0, -1, -1)` (x, y, w, h; w < 0 = cała mapa).
  - `void Shader::SetUniform1iv(const std::string& name, int count, const int* values);`
  - `void TextureClass::UploadRegion(int x, int y, int width, int height, int rowLength, const void* data, GLenum format, GLenum type);` — `data` = teksel (x, y), wiersze po `rowLength` tekseli (`GL_UNPACK_ROW_LENGTH`, przywracane na 0).
  - Shader: `layout(rg8ui, binding = 5) uniform readonly uimage2D paintMap; uniform int paintBiomes[256]; uniform ivec2 regionEnd;` — teksele poza `[regionOrigin, regionEnd)` wracają; dispatch tylko kafli prostokąta.

- [ ] **Step 1: `--gpu-check`** w `CheckMapBiomes`: `"mapbiomes: paint matches the CPU"` (warstwa z dwoma plamami — biom 2 siła 255 i biom 4 siła 64, miękkie brzegi z `biomepaint::Apply` — id, kolory ≤ 1e-5 i splat ≤ 1/255 + 1e-4 wg reguł z Global Constraints; patchiness 0); `"mapbiomes: paint follows names"` (paleta z nazwą spoza tabeli + odwrócona kolejność tabeli → id = automat albo biom o tej nazwie); `"mapbiomes: region equals the whole map"` (przebieg na prostokącie (300, 40, 500, 150) po przebiegu całości z inną warstwą = wynik całości z nową warstwą, bit w bit w prostokącie, poza nim stary wynik).
- [ ] **Step 2:** build → FAIL (brak API). **Step 3:** implementacja. **Step 4:** ctest + `--gpu-check` → wszystko ok, sekcje `world:`, `chunk:`, `sculpt:` bez zmian.

### Task 5: Pędzel w `MapImportSys`, zakładka Biomes → Paint, dokumentacja

**Files:**
- Modify: `src/subSystems/MapImportSys.h/.cpp`, `MapImportBiomes.cpp`, `MapImportSculpt.cpp`, `MapImportEdit.cpp`, `CLAUDE.md`, `docs/ROADMAP.md`

**Interfaces:**
- Consumes: Task 1–4.
- Produces (prywatne `MapImportSys`): `biomepaint::Layer biomePaint`, `std::unique_ptr<TextureClass> paintTexture`, `bool biomePaintOn`, `std::string paintBiome` (nazwa wybrana z tabeli), `float paintRadius = 32, paintHardness = 0.5f, paintRate = 2.0f` (siła na sekundę), `struct PaintStroke { biomepaint::Layer before; bool painted = false; }; std::optional<PaintStroke> paintStroke;`, `void UpdateBiomePaint(const Camera&, bool active, float deltaTime)`, `void FinishPaintStroke()`, `void UsePaint(biomepaint::Layer layer)` (warstwa + pełne wgranie + `biomesStale`), `void PaintImGui()`, `float ActiveBrushRadius() const`.

- [ ] **Step 1: Wejście i pędzel.** `UpdateSculpt` przy `biomePaintOn` woła `UpdateBiomePaint` (te same `paintPressed/paintHeld/paintInvert`, `hoverTexel`); `PickUnderCursor`/`SculptInputImGui` działają dla obu pędzli (promień = `ActiveBrushRadius()`, kółko w kolorze biomu). Start pociągnięcia: `biomesOn`, mapa, `!Busy()`, slot z `SlotOf` (0 → status „Paint: the palette is full (255 biomes)”, bez pociągnięcia); dab co klatkę z `amount = paintRate · dt` (≤ 0,1 s), `erase = paintInvert`; zmieniony prostokąt → `UploadRegion`; przebieg GPU na tym prostokącie (gdy klimat gotowy i rozmiar się zgadza). Puszczenie → krok historii `{ "Paint biomes", nullopt, seaLevelMetres, before }`, `dirty = true`. `Busy()` obejmuje `paintStroke`; eksport biomów wymaga `!paintStroke`.
- [ ] **Step 2: Stan mapy.** Nowa mapa (`InstallMap`) → pusta warstwa; `InstallProject` → `project.paint` (rozmiar ≠ mapa → pusta + status); reload tego samego pliku zachowuje warstwę przy tym samym rozmiarze; `StartSaving` zapisuje warstwę; wyłączenie biomów kończy pędzel (`biomePaintOn = false`). Przebieg GPU zawsze dostaje warstwę i `SlotBiomes(biomePaint, table)`.
- [ ] **Step 3: UI** (sekcja „Paint” w zakładce Biomes, po tabeli): checkbox „Paint biomes with the mouse” (wyłącza Sculpt; Sculpt wyłącza ten), combo biomów tabeli (kolor + nazwa), „Radius (px)” 1–512 log, „Hardness” 0–1, „Speed” 0,1–10 /s log, „Clear painting” (krok historii, nieaktywne przy pustej warstwie), podpowiedź „Left button: paint, Ctrl: erase, Shift + wheel: radius”, liczba nazw w palecie i ile z nich nie ma w tabeli.
- [ ] **Step 4:** build, ctest, `--gpu-check` → PASS. Ręcznie (właściciel): malowanie miękkie/ostre, A → B, gumka, Ctrl+Z/Y, zmiana nazwy i kolejności biomu, Save → Open, eksport, płynność na 8192².
- [ ] **Step 5: Dokumentacja.** CLAUDE.md: `BiomePaint.*` w strukturze; w akapicie „Biomy (D1)” dopisek D2 (warstwa, reguły mieszania dwa razy, przebieg na prostokącie, historia, v4); ROADMAP Faza 6 D: 🟡 D2 z datą i listą do sprawdzenia.
- [ ] **Step 6:** Bez commita (commituje właściciel).
