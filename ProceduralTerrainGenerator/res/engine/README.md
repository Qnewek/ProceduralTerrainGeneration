# Importing this pack into a game engine

This folder was copied into the export pack by Procedural Terrain Generator ("Engine import scripts" in Saving).
The pack folder (the one holding `<name>.json`) is what the importers read; keep it together.

Everything is in metres and at true scale: one map texel is `grid.metresPerTexel` metres, heights come from the R16 file
and the encoding in the manifest.

## Unity (2022.3 LTS or Unity 6)

1. Copy `engine/unity/Editor/PtgPackImporter.cs` into your project under any folder named `Editor` (e.g. `Assets/Editor/`).
2. Optional, for trees, shrubs, rocks, rivers and lakes: install glTFast —
   `Window → Package Manager → + → Add package by name… → com.unity.cloud.gltfast`.
   Without it the terrain, its layers and the sea are still imported, and the console says what was skipped.
3. `Tools → PTG → Import pack…`, choose `<name>.json` of the pack.

Result: assets in `Assets/PTG/<name>/` (terrain data, terrain layers, textures, tree prefabs) and a GameObject `<name>`
in the open scene with one Terrain per tile (maps wider than 4097 texels become several connected tiles), the sea and
rivers/lakes. Importing the same pack again replaces both.

## Godot 4 (4.3 or newer)

Export the pack with **Terrain mesh = glTF** — Godot has no built-in terrain, the importer uses the mesh tiles and their
levels of detail.

1. Copy `engine/godot/addons` into your project folder (you get `res://addons/ptg_import/`).
2. `Project → Project Settings → Plugins`, enable "PTG pack import".
3. `Project → Tools → Import PTG pack…`, choose `<name>.json` of the pack.

Result: the pack copied to `res://ptg/<name>/` and the scene `res://ptg/<name>/<name>.scn` opened: terrain tiles with
distance LODs and a material shader mixing the surface materials, height-map collision, the sea and rivers/lakes, and the
vegetation as MultiMeshes with the app's LOD distances. Importing the same pack again replaces the scene.

## Large packs

Both importers run inside the editor, which stays busy until they finish.
- Unity: a map of 8192² texels needs about 1.5 GB of memory during the import and several minutes; terrain layers are
  sampled at most 2048² per terrain tile.
- Godot: the vegetation file is parsed in GDScript — a few hundred thousand plants take tens of seconds, the maximum of
  2 million plants several minutes and a scene file of hundreds of MB. Plants are grouped in squares about half as wide
  as the distance where their level of detail ends; on maps larger than ~4 km the squares grow (at most 64 × 64 per
  model and level), so levels of detail switch less precisely there. The collision shape of an 8192² map is ~270 MB.

## Axes

The pack is right-handed, Y up: x = map column, z = map row, row 0 = top of the 2D map in the app.
Godot uses the same axes. Unity is left-handed, so the importer puts row 0 at the far (+Z) edge: the map looks the same
from above as in the app, not mirrored.
