# D3: biomy z pliku pokrycia terenu (Imported map) — projekt

Data: 2026-11-22. Etap D Fazy 6 (ROADMAP): D1 klimat z terenu ✅, D2 pędzel biomów ✅, **D3 pokrycie terenu**, D4 roślinność.

## Po co

Mapa rzeczywista (DEM) ma dziś biomy zgadywane z rzeźby i szumów temperatury/wilgotności (D1). Właściciel chce, żeby teren razem z biomami
nadawał się do eksportu — dla prawdziwego miejsca biomy powinny leżeć tam, gdzie naprawdę jest las, łąka, pole, woda czy skała. Źródłem jest
plik pokrycia terenu, przede wszystkim ESA WorldCover (GeoTIFF, klasy uint8, ok. 10 m, stopnie geograficzne, kafle 3°×3°).

## Decyzje właściciela

- Oba źródła automatyczne (klimat z terenu i plik pokrycia) + pędzel biomów na wierzchu (ustalone przy projekcie etapu D). Malowanie z D2
  działa bez zmian ponad każdym źródłem.
- **Wariant B (2026-11-22):** klasa pokrycia zawęża listę biomów, a w jej obrębie wybiera klimat (najbliższy biom w przestrzeni C, M, W, T, H, E
  jak w D1). Las w dolinie = liściasty, wysoko = tajga; „Bare” w górach = skały, na nizinie = pustynia.
- **Układy (2026-11-22):** stopnie geograficzne oraz poprzeczny Merkator — UTM (WGS84 326xx/327xx, ETRS89 258xx) i polskie PUWG 1992
  (EPSG:2180, NMT z Geoportalu) — własne przeliczenie, bez nowej biblioteki. Inne układy: rozciągnięcie z ostrzeżeniem.
- Przyjęte propozycje (2026-11-22): kilka kafli naraz; wbudowana legenda WorldCover, a dla innych plików lista kodów znalezionych w pliku do
  przypisania ręcznie; na piksel mapy klasa najczęstsza w jego obszarze (granice klas ostre jak mapa id), kolory i materiały mieszane tylko
  między biomami dozwolonymi w klasie.
- Podejście (2026-11-22): CPU w tle liczy mapę klas w rozmiarze mapy, GPU wybiera biom z dozwolonych (maska na klasę).

## Cele

1. Georeferencja mapy: import zapamiętuje układ i położenie mapy, przenosi je przez wycinek i zmniejszenie, projekt je zapisuje.
2. Wczytanie jednego lub kilku plików pokrycia (GeoTIFF klas uint8), tylko fragmentu pod mapą.
3. Mapa klas = klasa najczęstsza w obszarze każdego piksela mapy, z przeliczeniem układów (stopnie ↔ poprzeczny Merkator).
4. Bez georeferencji (mapa z PNG/R16, mapa z TerrainGen, plik bez współrzędnych, układ nieobsługiwany): pierwszy plik rozciągnięty na całą mapę.
5. Tabela klas: kod → nazwa, kolor, udział na mapie, lista dozwolonych biomów (po nazwach). Domyślna dla WorldCover.
6. Przebieg GPU biomów wybiera i miesza tylko biomy dozwolone w klasie teksela; klasa bez listy, kod 0 (brak danych) i teren poza plikami = wszystkie biomy (jak D1).
7. Eksport działa bez zmian (id i splat z przebiegu GPU).
8. Georeferencję można ustawić także po imporcie (stare projekty, mapy już edytowane), a klasy widać na terenie w trybie „LAND COVER”.

## Poza zakresem (non-goals)

- Inne odwzorowania (Lambert, LAEA EPSG:3035 — CORINE, Web Mercator), transformacje między datumami (WGS84 / ETRS89 / GRS80 traktowane jako
  jedno — różnica < 1 m przy pikselu WorldCover 10 m), przeliczanie samego DEM do innego układu.
- Miękkie przejścia między klasami (mieszanie udziałów klas), edycja pliku pokrycia w aplikacji, biom „miasto” / „pole” w domyślnej tabeli
  (użytkownik może dodać biom i przypisać klasę).
- Roślinność (D4).

## Anty-cele

- Biom sprzeczny z plikiem tam, gdzie plik jest jednoznaczny (las tam, gdzie plik mówi woda) — maska jest twarda.
- Mapa „pusta” albo zawieszona aplikacja przy dużych kaflach: liczenie w tle, pamięć ograniczona, błąd = komunikat i powrót do klimatu.
- Ciche złe dopasowanie: gdy plik i mapa się nie pokrywają albo układ jest nieznany, UI to mówi (pokrycie w %, „rozciągnięty”).

## Podejścia

- **Wybrane — CPU mapa klas + maski na GPU.** Mapa klas zmienia się tylko po zmianie plików, mapy (nowa mapa) albo układu, więc liczy ją CPU
  w tle (jak klimat), wynik `R8UI` w rozmiarze mapy. Shader dostaje maski dozwolonych biomów na klasę.
- Odrzucone — surowe piksele pokrycia na GPU i przeliczanie układu w shaderze: `float` traci precyzję przy współrzędnych rzędu 10⁵–10⁶ m,
  a klasa najczęstsza w shaderze kosztowałaby przy każdym przebiegu biomów.
- Odrzucone — pokrycie jako warstwa malowania D2: jeden stały biom na klasę (wariant A, odrzucony przez właściciela).

## Projekt

### Georeferencja

- `struct GeoReference` (ptg_core): układ (`epsg`: 4326 dla stopni lub kod poprzecznego Merkatora z listy niżej), położenie lewego górnego narożnika
  piksela (0, 0) w jednostkach układu (stopnie lub metry) i wektor piksela (x > 0, y < 0 — wiersze na południe). Brak = mapa bez georeferencji.
- Źródła: GeoTIFF — ModelTiepoint (33922) + ModelPixelScale (33550), GeoKeys: `GTModelTypeGeoKey` (1024), `GTRasterTypeGeoKey` (1025: PixelIsPoint
  przesuwa narożnik o pół piksela), `GeographicTypeGeoKey` (2048), `ProjectedCSTypeGeoKey` (3072); ModelTransformation (34264) tylko bez obrotu.
  SRTM `.hgt` — nazwa (N50E019 = narożnik SW), stopnie, piksel = środek próbki (1201/3601 próbek na 1°). ESRI `.asc` — xllcorner/yllcorner/cellsize
  (center → pół komórki), układ z `.prj` obok pliku (proste rozpoznanie po słowach kluczowych: GEOGCS bez PROJCS = stopnie, „UTM zone NN[N/S]”,
  „CS92”/„1992” = 2180, „CS2000” + strefa), a gdy go nie ma — **ręczny wybór układu w zakładce Load** (lista z sekcji niżej; domyślnie stopnie,
  gdy komórka < 0,02 — reguła już istniejąca w imporcie).
- Wycinek: narożnik += crop · piksel; zmniejszenie: piksel *= factor. `HeightmapImport::ImportInfo` dostaje `std::optional<GeoReference>`.
- **Decyzja właściciela (2026-11-22): georeferencję można ustawić po imporcie.** W sekcji „Land cover” zakładki Biomes: układ mapy (lista
  z sekcji niżej, gdy mapa ma współrzędne bez układu albo trzeba go poprawić) i „Take georeference from file…” — dla starych projektów (v1–v4)
  i map bez położenia: plik DEM (GeoTIFF / .hgt / .asc), jego georeferencja + wycinek (x, y) i zmniejszenie, jakimi zrobiono mapę (pola
  wypełnione 0, 0, 1); rozmiar po wycinku i zmniejszeniu musi równać się rozmiarowi mapy, inaczej błąd i nic się nie zmienia. Zmiana = mapa
  klas od nowa, projekt brudny (krok historii nie jest potrzebny).
- Projekt `.ptgmap` **wersja 5**: nagłówek `"georeference": {"epsg", "origin": [x, y], "pixel": [x, y]}` (brak = bez georeferencji); wersje 1–4
  czytane (bez georeferencji). Mapa wysłana z TerrainGen/Noise — bez georeferencji.

### Poprzeczny Merkator (ptg_core, testy)

- Elipsoida WGS84/GRS80 (jedna), szereg Krügera (dokładność mm w strefie). Funkcje: stopnie → metry i metry → stopnie dla parametrów strefy.
- Obsługiwane EPSG: 326NN / 327NN (WGS84 UTM N/S, strefy 1–60: λ0 = 6·NN − 183, k0 = 0,9996, FE = 500 000, FN = 0 / 10 000 000), 258NN
  (ETRS89 UTM 28–38, jak 326NN), 2180 (PUWG 1992: λ0 = 19°, k0 = 0,9993, FE = 500 000, FN = −5 300 000), 2176–2179 (PUWG 2000 strefy 5–8:
  λ0 = 15/18/21/24°, k0 = 0,999923, FE = strefa · 10⁶ + 500 000, FN = 0).
- Testy na punktach referencyjnych z niezależnego źródła (np. GeographicLib / przeliczenia epsg.io), wartości wpisane w test; przejście tam i z powrotem.

### Pliki pokrycia

- GeoTIFF jednokanałowy uint8 (kafelkowany lub paskowy, kompresje libtiff z vcpkg), z georeferencją jak wyżej. Czytany tylko prostokąt pod mapą
  (narożniki mapy przeliczone do układu pliku, z marginesem). *Decyzja projektanta:* limit pamięci trzymanego rastra = 2× rozdzielczość mapy na oś
  (8192² mapa → ≤ 16384², 256 MB) — to tylko limit: mniejszy prostokąt zostaje w pełnej rozdzielczości pliku; większy jest zmniejszany całkowitym
  krokiem s z klasą najczęstszą w bloku s×s (każdy piksel czytany, kod 0 pomijany), nie przez przeskakiwanie pikseli.
- *Decyzja projektanta:* mapa z georeferencją — pliki bez georeferencji albo w nieobsługiwanym układzie są pomijane (status przy pliku „skipped: …”),
  reszta działa. Mapa bez georeferencji — pierwszy plik rozciągnięty na całą mapę, pozostałe pominięte (UI to mówi).
- Mapa klas (`std::vector<uint8_t>` w rozmiarze mapy → `GL_R8UI`): dla piksela mapy próbki w jego obszarze (siatka k×k, k = clamp(ceil(piksel mapy /
  piksel pokrycia), 1, 4)) → położenie w układzie mapy → stopnie → plik, który je zawiera (pierwszy z listy) → klasa; wynik = klasa najczęstsza
  (remis: mniejszy kod). Kod 0 (brak danych WorldCover) liczy się jak brak próbki; piksel bez żadnej próbki = 0. Przeliczenie układu na rzadkiej
  siatce (np. co 16 pikseli) i interpolacja dwuliniowa między węzłami — błąd pomijalny wobec piksela, koszt przeliczeń znika.
- W tle (`GenerationJob`, try/catch jak klimat), wynik starszej `mapVersion` / starszej listy plików odrzucany. Cel: 8192² w ≤ 10 s.
  Przeliczana po: nowej mapie, zmianie listy plików, zmianie układu mapy (ręczny wybór). Edycje terenu i morza jej nie ruszają.
- Udziały klas na mapie liczone przy okazji (do tabeli w UI).

### Tabela klas i maski

- `mapbiomes::Settings` dostaje źródło (`CLIMATE` / `LAND_COVER`), listę plików (ścieżki; w projekcie także względne do projektu, odczyt próbuje
  obu) i tabelę klas: kod → nazwa, kolor, lista nazw dozwolonych biomów. Zapis w JSON biomów projektu (pola z domyślnymi, więc stary JSON się czyta).
- Domyślna legenda WorldCover: 10 Tree cover, 20 Shrubland, 30 Grassland, 40 Cropland, 50 Built-up, 60 Bare / sparse vegetation, 70 Snow and ice,
  80 Permanent water bodies, 90 Herbaceous wetland, 95 Mangroves, 100 Moss and lichen. Domyślne biomy (nazwy z domyślnej tabeli):
  Tree cover → Forest, Taiga, Jungle; Shrubland → Steppe, Savanna, Tundra; Grassland → Plains, Steppe, Savanna, Tundra, Hills; Cropland → Plains,
  Steppe, Hills; Built-up → Plains, Hills; Bare → Desert, Mountain, Rocky peaks, Stony shore, Beach, Tundra; Snow and ice → Snowfields, Snowy peaks,
  Snowy shore; Water → Ocean, Deep ocean; Wetland → Plains, Forest, Tundra; Mangroves → Jungle, Beach; Moss and lichen → Tundra, Snowfields.
- Kod spoza legendy (inny plik) pojawia się w tabeli jako „Class N” bez biomów (= wszystkie), do przypisania.
- Maski: na kod 0–255 maska 64-bitowa indeksów tabeli (nazwy → indeksy przy każdym przebiegu, jak `SlotBiomes` w D2; nazwa spoza tabeli pominięta).
  Maska pusta = wszystkie biomy. Do shadera w SSBO (512 × uint), nie w uniformach (limit komponentów uniform w compute).

### Shader i CPU wzorzec

- `map_biome_compute.shader`: przy źródle LAND_COVER czyta klasę teksela (`R8UI`, nowy binding), bierze maskę; najbliższy biom, wagi koloru
  i materiałów tylko po biomach z maski (`BlendWeight` względem najbliższego dozwolonego). Potem malowanie D2 jak dziś.
- Wzorzec CPU (testy + `--gpu-check`, sekcja `mapbiomes:`): `biome::Nearest` / `MixMaterials` z maską (nowe przeciążenia albo parametr maski),
  przypadki: maska zawęża id, kolor/splat zgodne z CPU, pusta maska = D1 bit w bit, malowanie nad pokryciem.

### UI (zakładka Biomes)

- Na górze „Source”: „Climate from terrain” / „Land cover”. Klimat liczy się w obu (T, H, E, C, M wybierają w obrębie klasy).
- Sekcja „Land cover”: lista plików (Add… przez `FileDialog`, Remove), przy każdym: układ, pokrycie mapy w %, „stretched” gdy rozciągnięty;
  stan liczenia („Reading land cover…”, czas); tabela klas: kolor, kod, nazwa, udział na mapie, przycisk z listą biomów (checkboxy z tabeli biomów);
  „WorldCover defaults”. Brak pliku / błąd odczytu = komunikat, przebieg jak przy „Climate from terrain”.
- Zakładka Load (import): układ mapy z pliku albo wybór ręczny, gdy plik ma współrzędne bez układu (`.asc` bez `.prj`).
- **Decyzja właściciela (2026-11-22): tryb wyświetlania „LAND COVER”** (Display, tylko Imported map, gdy jest mapa klas): kolory klas z tabeli
  na terenie jak BIOMES — do sprawdzenia, czy plik leży na mapie (rzeki, jeziora, granice lasu). Mapa klas (`R8UI`) na wolnej jednostce tekstur
  terenu + paleta 256 kolorów (np. tekstura 256×1 albo UBO); ustawia `TerrainView::Draw` jak pozostałe warstwy. Bez mapy klas tryb niedostępny
  (powrót do BIOMES / GREYSCALE jak przy wyłączeniu biomów).

### Dane w projekcie i stan

- Projekt v5: georeferencja (nagłówek), ustawienia źródła i tabela klas w JSON biomów; mapa klas nie jest zapisywana (liczona przy otwarciu).
  Brak pliku przy otwarciu = komunikat, źródło zostaje LAND_COVER, przebieg jak bez pokrycia, aż plik się znajdzie (Add / Refresh).
- Nowa mapa czyści mapę klas; otwarcie projektu liczy ją od nowa.
- *Decyzja projektanta:* lista plików pokrycia, źródło i tabela klas przeżywają import nowej mapy (sąsiedni kafel DEM na tych samych kaflach
  WorldCover) — reszta ustawień biomów resetuje się jak dziś (`UseBiomeSettings("")`); projekt ma własne i je przywraca.

## Zostawione budowniczemu

- Kształt modułów (np. `GeoReference.*`, `TransverseMercator.*`, `LandCover.*` w `terrainGeneration/`), nazwy pól JSON, układ UI w szczegółach.
- Czy `.prj` czytać pełniej niż słowami kluczowymi; dokładny margines i krok siatki przeliczeń.
- Dokumentacja: CLAUDE.md (struktura, akapit D3 przy biomach Imported map, wersja projektu), ROADMAP (🟡 D3 z listą do sprawdzenia).
