# Procedural Terrain Generator

A tool created in order to play with **procedural terrain generation** method by adjusting theirs parameters and getting different interesting results. App uses noise-based algorithms, erosion simulation, and biome assignment as well as biome based vegetation placement. Thanks to interactive UI implemented using **ImGui** and **ImPlot** user can seamlessly adjust algorithms configs.

## Features

- **Loading heightmap from file**
    Mode enabling possibility of loading greyscale image to be displayed as a terrain.
  
- **Noise-based Terrain Generation**
    Uses [**Simplex Noise**](https://thebookofshaders.com/11) to generate base terrain and displays it. This mode gives user possibility of adjusting every parameter being used in generation method mentioned above.
  List of parameters with theirs meaning:
  
  

- **Erosion Simulation**
    Apply natural erosion algorithm on previously generated or loaded map, in order to create realistic terrain shapes and features in real world created by hydraulic erosion (water flowing downhill). Algorithm is based on [this essay](http://www.firespark.de/resources/downloads/implementation%20of%20a%20methode%20for%20hydraulic%20erosion.pdf).

- **Terrain Gneration with Noise-based parameters**
    Generating terrain using multiple generated noises as parameters:
  -**Mountainousness**: how much elevation should be in certain point.
  -**Continentalness**: how far into the land the point is.
  -**Peaks&Valleys**: adjusting terrain by adding some sharper shapes as ridges and valleys.
  Currently there are 2 implemented algorithms, which are:
  -Simple combining [0.0-1.0] noise values and multiplying them by heightscale.
  -Using values generated with noises as points to sample from previously set spline functions.
  
- **Biome Generation**
    Assign biomes based on terrain parameters, **temperature**, and **humidity** and 3 parameters mentioned above in **Terrain Gneration with Noise-based parameters** section, enabling making changes based on current biome.
  The tool gives user the possibility of seting his own config of biome generation via UI, levels of each parameter noise with its bounds and defining biomes parameters includiong mentioned levels for which they occur. As for now there is a possibility of painting terrain with biome specified color.

## Screenshots

*ScreenShots *

## Installation

1. Clone the repository:  
   ```bash
   git clone https://github.com/Qnewek/ProceduralTerrainGeneration.git
