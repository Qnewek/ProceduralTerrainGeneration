# Region rzek podążający za kamerą (świat nieskończony) — projekt

Data: 2026-11-20. ROADMAP, Faza 5, „Mapa częściowa jako wycinek świata”, punkt 3. Branch `feat/rivers-follow-camera`.

## Cel i powód

Rzeki metody REGION mają być wszędzie, gdzie lata kamera w świecie nieskończonym TerrainGen, a nie tylko w kwadracie `regionSize`
(domyślnie 8192 teksele) wokół ostatniej mapy częściowej. Dziś poza nim teren nie jest żłobiony, nie ma wody rzek ani jezior.

- **Cel:** region przesuwa się za kamerą, sieć liczy się w tle, chunki i roślinność dostają nową sieć istniejącym mechanizmem.
- **Poza zakresem:** płynne przejście między sieciami, kafle sieci, zmiany shaderów i `RiverMap`, zmiany na mapie częściowej.
- **Porażka mimo działania:** przeskoki zbyt częste (przeliczanie wszystkich chunków co chwilę) albo inne rzeki po powrocie w to samo miejsce.

## Podejścia (wybrane: A)

- **A. Skokowe przesuwanie regionu na stałej siatce** — wybrane przez właściciela. Mało kodu, bez zmian GLSL, powtarzalne (siatka).
  Wada: przy przeskoku rzeki mogą się zmienić w części wspólnej (hydrologia zależy od całego regionu), głównie przy krawędziach starego regionu.
- **B. Kafle sieci z zakładką, ładowane przed wejściem w zasięg widoku** — brak przeskoków w kadrze, ale szwy rzek na granicach kafli i duże zmiany
  (GLSL czyta 2×2 sieci, `RiverMap`, woda, roślinność, jednostki tekstur).
- **C. Przenikanie dwóch sieci** — odrzucone: mieszanie żłobienia dwóch biegów daje płytkie podwójne koryta.

## Projekt

1. **Środek regionu jako stan.** `TerrainGenerationSys` trzyma środek sieci, która jest liczona / wgrana (dziś liczony w miejscu z `MapCenter()`).
   - Mapa częściowa: środek = `MapCenter()`, jak dziś.
   - Świat nieskończony z włączonym śledzeniem: środek = pozycja kamery w tekselach mapy, przyciągnięta do siatki o kroku `regionSize / 4`
     zaokrąglonym do wielokrotności `cellSize` (komórki kolejnych regionów leżą na tych samych pozycjach świata → te same próbki terenu → spójniejsze sieci).
2. **Kiedy przesunąć.** Co klatkę w świecie nieskończonym: jeśli kamera jest dalej niż `regionSize / 4` (w osi X lub Z) od środka bieżącej sieci,
   sieć jest nieaktualna z nowym środkiem. Po przeskoku kamera jest najwyżej pół kroku od środka, więc jest histereza (brak migotania między dwoma środkami).
   Gdy sieć jeszcze się liczy, nowa nie startuje (jak dziś); po jej przyjściu warunek sprawdza się znowu.
3. **Czysta funkcja do testów** w `ptg_core` (`Hydrology.*`): środek regionu dla kamery — zwraca nowy środek albo „bez zmian” dla
   (bieżący środek, pozycja kamery, `regionSize`, `cellSize`). Testy: brak zmiany w ¼, zmiana dalej, przyciąganie do siatki i wielokrotności `cellSize`,
   powtarzalność (ta sama pozycja → ten sam środek), ujemne współrzędne.
4. **Podmiana.** Bez zmian: `UpdateRivers` wgrywa sieć i ustawia `pending.terrain` → `chunks.Invalidate()` → chunki od najbliższych, stare rysowane do podmiany;
   roślinność dostaje nowy `riverNetwork`.
5. **UI (zakładka Rivers, sekcja Region):** checkbox „Follow camera (infinite world)”, domyślnie włączony, nie zapisywany w presecie (stan widoku, nie generacji);
   tekst ze środkiem bieżącego regionu. Tooltip „Region size” bez „around the middle of the finite map” w świecie nieskończonym.
6. **Przejścia trybów:** wyjście ze świata nieskończonego — mapa pod kamerą i sieć wokół mapy (jak dziś); wejście — sieć zostaje, dopóki kamera nie odejdzie o ¼.
   „Places” / preset w świecie nieskończonym przenoszą kamerę, więc region pójdzie za nią sam.

## Zostawione wykonawcy

Nazwy pól i funkcji, dokładne miejsce sprawdzenia w `Update`, opis w ROADMAP / CLAUDE.md. Pomiar czasu przeskoku (hydrologia `riverMs` + przeliczenie chunków)
na domyślnym świecie, zapisany w ROADMAP.
