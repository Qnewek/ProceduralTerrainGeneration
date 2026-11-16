# Legacy: erozja kroplowa na CPU

Kod nieużywany i niekompilowany (nie ma go w `CMakeLists.txt`) — trzymany tylko poglądowo, jako pierwsza wersja algorytmu
z pracy dyplomowej (Hans Theobald Beyer, *Implementation of a method for hydraulic erosion*).

- `Erosion.h`, `Erosion.cpp` — symulacja: krople na liście jednokierunkowej, wszystkie robią krok `i`, zanim którakolwiek zrobi krok `i + 1`.
- `ErosionTests.cpp` — testy doctest tej wersji.

Aplikacja liczy erozję na GPU:

- ta sama metoda kroplowa: `src/opengl/GpuErosion.*`, `res/shaders/ComputeShaders/erosion_droplet_compute.shader`
  (ten sam porządek kroków, mapa w buforze stałoprzecinkowym zmienianym przez `atomicAdd`);
- model rur wirtualnych (virtual pipes) + erozja termiczna: `res/shaders/ComputeShaders/erosion_pipe_compute.shader`.

Kod zależy od `Grid.h` i `ErosionConfig` z `WorldConfig.h` w `src/terrainGeneration/`; po zmianach tamtych plików może się już nie kompilować.
