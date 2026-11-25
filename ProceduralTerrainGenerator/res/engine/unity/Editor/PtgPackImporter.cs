// Importer of Procedural Terrain Generator export packs for Unity 2022.3 LTS / Unity 6 (editor only: put it in a folder named Editor).
// Tools > PTG > Import pack...: choose <name>.json of a pack. Builds, under a GameObject <name> in the open scene:
//   Terrain_<x>_<z> - Unity Terrain tiles (maps wider than 4097 texels become several connected tiles), heights from the R16 file,
//                     terrain layers from the material textures and splat maps of the pack
//   trees           - every plant of <name>_vegetation.json as a Terrain Tree (prefab per model variant with a LODGroup of the app's LODs)
//   Water           - the sea (a plane at the sea level) and the rivers and lakes of the water glTF
// Assets go to Assets/PTG/<name>/; importing the same pack again replaces them and the GameObject.
// glTF files (vegetation models, water) need the glTFast package (com.unity.cloud.gltfast); without it they are skipped with a warning.
//
// Axes: the pack is right-handed, Y up, x = map column, z = map row (row 0 = top of the 2D map). Unity is left-handed, so a pack position
// (x, y, z) is placed at (x, y, sizeZ - z): seen from above the map looks as in the app, not mirrored. glTFast negates X when it imports
// glTF, so a model turned by yaw in the pack is turned by PI - yaw here, and the water glTF is turned 180 degrees and moved by sizeZ.
#if UNITY_EDITOR
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ptg
{
	public static class PtgPackImporter
	{
		const int MaxHeightmapResolution = 4097;
		// 2048, not Unity's 4096: a 4096² alphamap of 16 layers is 1 GB of floats and ~1e9 samples per tile
		const int MaxAlphamapResolution = 2048;
		const int AlphamapBlockRows = 256;
		const string DialogTitle = "PTG pack import";

		// ---- Manifest (only the fields the importer reads; JsonUtility ignores the rest) ----
		[Serializable] class Manifest { public string generator; public string name; public Grid grid; public FileEntry[] files; public Materials materials; public Meshes meshes; public VegetationInfo vegetation; public SeaLevel seaLevel; }
		[Serializable] class Grid { public int width; public int height; public float metresPerTexel; }
		[Serializable] class SeaLevel { public float metres; }
		[Serializable] class FileEntry { public string path; public string content; public string format; public Encoding encoding; }
		[Serializable] class Encoding { public float metresMin; public float metresMax; }
		[Serializable] class Materials { public MaterialEntry[] list; }
		[Serializable] class MaterialEntry { public int id; public string name; public Splat splat; public float[] fallbackColor; public Textures textures; }
		[Serializable] class Splat { public string file; public string channel; }
		[Serializable] class Textures { public string albedo; public string normal; public string roughness; public float tileMetres; }
		[Serializable] class Meshes { public Water water; }
		[Serializable] class Water { public string file; }
		[Serializable] class VegetationInfo { public string file; }
		[Serializable] class VegetationFile { public VegetationModel[] models; }
		[Serializable] class VegetationModel { public string name; public string layer; public string file; public int variants; public int count; }

		struct LodRange { public float lod1, lod2, max; }

		// What the tiles share
		class Layout
		{
			public int width, height, tilesX, tilesZ, resolution, alphamapResolution;
			public float metresPerTexel, metresMin, metresSpan, spanX, spanZ; // spans in texels per tile
			public float SizeZ => (height - 1) * metresPerTexel;
			public float TileMetresX => spanX * metresPerTexel;
			public float TileMetresZ => spanZ * metresPerTexel;
		}

		[MenuItem("Tools/PTG/Import pack...")]
		static void ImportMenu()
		{
			string path = EditorUtility.OpenFilePanel("Choose <name>.json of a PTG export pack", "", "json");
			if (string.IsNullOrEmpty(path))
				return;
			try
			{
				Import(path);
			}
			catch (Exception e)
			{
				Debug.LogException(e);
				EditorUtility.DisplayDialog(DialogTitle, e.Message, "OK");
			}
			finally
			{
				EditorUtility.ClearProgressBar();
			}
		}

		public static void Import(string manifestPath)
		{
			string text = File.ReadAllText(manifestPath);
			Manifest manifest = JsonUtility.FromJson<Manifest>(text);
			if (manifest == null || manifest.generator != "Procedural Terrain Generator" || manifest.grid == null)
				throw new Exception(manifestPath + " is not a manifest of a Procedural Terrain Generator pack");
			string packFolder = Path.GetDirectoryName(manifestPath);
			// The manifest's own "seaLevel" is an object; the generation preset inside it has a number of that name
			bool hasSea = Regex.IsMatch(text, "\"seaLevel\"\\s*:\\s*\\{");
			string name = manifest.name;

			FileEntry heightFile = Array.Find(manifest.files, f => f.content == "height" && f.format == "r16");
			if (heightFile == null)
				throw new Exception("The pack has no R16 height file: export it with \"Engine import scripts\" on.");

			// Fresh assets folder and GameObject
			string assetFolder = "Assets/PTG/" + name;
			if (AssetDatabase.IsValidFolder(assetFolder))
				AssetDatabase.DeleteAsset(assetFolder);
			Directory.CreateDirectory(DiskPath(assetFolder));
			AssetDatabase.Refresh();
			GameObject old = GameObject.Find("/" + name);
			if (old != null)
				UnityEngine.Object.DestroyImmediate(old);

			EditorUtility.DisplayProgressBar(DialogTitle, "Reading heights", 0.05f);
			Layout layout = MakeLayout(manifest, heightFile);
			float[] heights = ReadR16(Path.Combine(packFolder, heightFile.path), layout);

			EditorUtility.DisplayProgressBar(DialogTitle, "Terrain layers", 0.2f);
			TerrainLayer[] layers = MakeLayers(manifest, packFolder, assetFolder);
			List<Share> shares = ReadShares(manifest, packFolder, layout, layers.Length);

			var root = new GameObject(name);
			Undo.RegisterCreatedObjectUndo(root, "Import PTG pack");
			var terrains = new Terrain[layout.tilesX, layout.tilesZ];
			for (int tz = 0; tz < layout.tilesZ; tz++)
			{
				for (int tx = 0; tx < layout.tilesX; tx++)
				{
					EditorUtility.DisplayProgressBar(DialogTitle, "Terrain tiles", 0.3f + 0.4f * (tz * layout.tilesX + tx) / (layout.tilesX * layout.tilesZ));
					terrains[tx, tz] = MakeTile(layout, heights, shares, layers, tx, tz, assetFolder, root.transform);
				}
			}
			for (int tz = 0; tz < layout.tilesZ; tz++)
			{
				for (int tx = 0; tx < layout.tilesX; tx++)
				{
					Terrain left = tx > 0 ? terrains[tx - 1, tz] : null;
					Terrain right = tx + 1 < layout.tilesX ? terrains[tx + 1, tz] : null;
					Terrain bottom = tz > 0 ? terrains[tx, tz - 1] : null;
					Terrain top = tz + 1 < layout.tilesZ ? terrains[tx, tz + 1] : null;
					terrains[tx, tz].SetNeighbors(left, top, right, bottom);
				}
			}

			bool gltfMissing = false;
			if (manifest.vegetation != null && !string.IsNullOrEmpty(manifest.vegetation.file))
			{
				EditorUtility.DisplayProgressBar(DialogTitle, "Vegetation", 0.75f);
				ImportVegetation(manifest, packFolder, assetFolder, layout, terrains, ref gltfMissing);
			}
			EditorUtility.DisplayProgressBar(DialogTitle, "Water", 0.95f);
			ImportWater(manifest, packFolder, assetFolder, layout, hasSea, root.transform, ref gltfMissing);
			if (gltfMissing)
				Debug.LogWarning("PTG: install glTFast (Window > Package Manager > + > Add package by name > com.unity.cloud.gltfast) to import the vegetation models and the water glTF, then import the pack again.");

			AssetDatabase.SaveAssets();
			EditorSceneManager.MarkSceneDirty(root.scene);
			Selection.activeGameObject = root;
			Debug.Log(string.Format("PTG: imported {0}: {1} terrain tile(s) of {2} heightmap texels, {3} layer(s)", name, layout.tilesX * layout.tilesZ, layout.resolution, layers.Length));
		}

		// ---- Heights ----

		static Layout MakeLayout(Manifest manifest, FileEntry heightFile)
		{
			var l = new Layout();
			l.width = manifest.grid.width;
			l.height = manifest.grid.height;
			l.metresPerTexel = manifest.grid.metresPerTexel;
			l.metresMin = heightFile.encoding.metresMin;
			l.metresSpan = Mathf.Max(heightFile.encoding.metresMax - heightFile.encoding.metresMin, 0.01f);
			l.tilesX = Mathf.Max(1, Mathf.CeilToInt((l.width - 1) / (float)(MaxHeightmapResolution - 1)));
			l.tilesZ = Mathf.Max(1, Mathf.CeilToInt((l.height - 1) / (float)(MaxHeightmapResolution - 1)));
			l.spanX = (l.width - 1) / (float)l.tilesX;
			l.spanZ = (l.height - 1) / (float)l.tilesZ;
			int span = Mathf.CeilToInt(Mathf.Max(l.spanX, l.spanZ));
			l.resolution = Mathf.Min(MaxHeightmapResolution, Mathf.NextPowerOfTwo(Mathf.Max(span, 32)) + 1);
			l.alphamapResolution = Mathf.Clamp(Mathf.NextPowerOfTwo(span), 16, MaxAlphamapResolution);
			return l;
		}

		// Heights in metres, row after row (row 0 first)
		static float[] ReadR16(string path, Layout l)
		{
			byte[] bytes = File.ReadAllBytes(path);
			int count = l.width * l.height;
			if (bytes.Length < count * 2)
				throw new Exception(path + " is shorter than " + l.width + " x " + l.height + " 16-bit values");
			var metres = new float[count];
			for (int i = 0; i < count; i++)
				metres[i] = l.metresMin + (bytes[2 * i] | (bytes[2 * i + 1] << 8)) / 65535f * l.metresSpan;
			return metres;
		}

		// Bilinear value of a map at a fractional pack texel (column, row), clamped to the map
		static float Sample(float[] map, int width, int height, float column, float row)
		{
			column = Mathf.Clamp(column, 0f, width - 1);
			row = Mathf.Clamp(row, 0f, height - 1);
			int x0 = Mathf.Min((int)column, Mathf.Max(width - 2, 0)), y0 = Mathf.Min((int)row, Mathf.Max(height - 2, 0));
			int x1 = Mathf.Min(x0 + 1, width - 1), y1 = Mathf.Min(y0 + 1, height - 1);
			float fx = column - x0, fy = row - y0;
			float top = Mathf.Lerp(map[y0 * width + x0], map[y0 * width + x1], fx);
			float bottom = Mathf.Lerp(map[y1 * width + x0], map[y1 * width + x1], fx);
			return Mathf.Lerp(top, bottom, fy);
		}

		// Pack texel (column, row) of a point of tile (tx, tz) at fractions (u along X, v along Unity Z) of the tile
		static Vector2 PackTexel(Layout l, int tx, int tz, float u, float v)
		{
			float column = (tx + u) * l.spanX;
			float unityRow = (tz + v) * l.spanZ;		// texels from the Unity Z = 0 edge, which is the last pack row
			return new Vector2(column, (l.height - 1) - unityRow);
		}

		static Terrain MakeTile(Layout l, float[] heights, List<Share> shares, TerrainLayer[] layers, int tx, int tz, string assetFolder, Transform parent)
		{
			var data = new TerrainData();
			// Resolution first: setting it resets the size
			data.heightmapResolution = l.resolution;
			data.size = new Vector3(l.TileMetresX, l.metresSpan, l.TileMetresZ);
			int res = data.heightmapResolution;
			var values = new float[res, res];
			for (int zi = 0; zi < res; zi++)
			{
				for (int xi = 0; xi < res; xi++)
				{
					Vector2 p = PackTexel(l, tx, tz, xi / (float)(res - 1), zi / (float)(res - 1));
					values[zi, xi] = Mathf.Clamp01((Sample(heights, l.width, l.height, p.x, p.y) - l.metresMin) / l.metresSpan);
				}
			}
			data.SetHeights(0, 0, values);

			data.terrainLayers = layers;
			data.alphamapResolution = l.alphamapResolution;
			int ares = data.alphamapResolution;
			// In blocks of rows, so a tile never holds the whole float alphamap
			for (int z0 = 0; z0 < ares; z0 += AlphamapBlockRows)
			{
				int rows = Mathf.Min(AlphamapBlockRows, ares - z0);
				var alphas = new float[rows, ares, layers.Length];
				for (int zb = 0; zb < rows; zb++)
				{
					int zi = z0 + zb;
					for (int xi = 0; xi < ares; xi++)
					{
						Vector2 p = PackTexel(l, tx, tz, (xi + 0.5f) / ares, (zi + 0.5f) / ares);
						float total = 0f;
						for (int m = 0; m < layers.Length; m++)
						{
							float w = shares == null ? 1f : shares[m].Sample(l, p.x, p.y);
							alphas[zb, xi, m] = w;
							total += w;
						}
						if (total <= 1e-4f)
							alphas[zb, xi, 0] = total = 1f;
						for (int m = 0; m < layers.Length; m++)
							alphas[zb, xi, m] /= total;
					}
				}
				data.SetAlphamaps(0, z0, alphas);
				EditorUtility.DisplayProgressBar(DialogTitle, "Terrain layers of tile " + tx + ", " + tz, (z0 + rows) / (float)ares);
			}
			AssetDatabase.CreateAsset(data, assetFolder + "/Terrain_" + tx + "_" + tz + ".asset");

			GameObject go = Terrain.CreateTerrainGameObject(data);
			go.name = "Terrain_" + tx + "_" + tz;
			go.transform.SetParent(parent, false);
			go.transform.localPosition = new Vector3(tx * l.TileMetresX, l.metresMin, tz * l.TileMetresZ);
			return go.GetComponent<Terrain>();
		}

		// ---- Layers ----

		static TerrainLayer[] MakeLayers(Manifest manifest, string packFolder, string assetFolder)
		{
			var layers = new List<TerrainLayer>();
			if (manifest.materials == null || manifest.materials.list == null || manifest.materials.list.Length == 0)
			{
				layers.Add(SaveLayer(new TerrainLayer { diffuseTexture = ColorTexture(new Color(0.5f, 0.5f, 0.5f), assetFolder + "/grey.png"), tileSize = new Vector2(4f, 4f) },
					assetFolder + "/grey.terrainlayer"));
				return layers.ToArray();
			}
			string folder = assetFolder + "/Materials";
			Directory.CreateDirectory(DiskPath(folder));
			foreach (MaterialEntry m in manifest.materials.list)
			{
				string safe = SafeName(m.id.ToString("00") + "_" + m.name);
				var layer = new TerrainLayer();
				bool textured = m.textures != null && !string.IsNullOrEmpty(m.textures.albedo);
				float tile = textured && m.textures.tileMetres > 0f ? m.textures.tileMetres : 4f * manifest.grid.metresPerTexel;
				layer.tileSize = new Vector2(tile, tile);
				if (textured)
				{
					layer.diffuseTexture = CopyTexture(Path.Combine(packFolder, m.textures.albedo), folder + "/" + safe + "_albedo" + Path.GetExtension(m.textures.albedo), false);
					if (!string.IsNullOrEmpty(m.textures.normal))
						layer.normalMapTexture = CopyTexture(Path.Combine(packFolder, m.textures.normal), folder + "/" + safe + "_normal" + Path.GetExtension(m.textures.normal), true);
				}
				if (layer.diffuseTexture == null)
				{
					float[] c = m.fallbackColor != null && m.fallbackColor.Length >= 3 ? m.fallbackColor : new[] { 0.5f, 0.5f, 0.5f };
					layer.diffuseTexture = ColorTexture(new Color(c[0], c[1], c[2]), folder + "/" + safe + "_color.png");
				}
				layer.smoothness = 0.1f;
				layers.Add(SaveLayer(layer, folder + "/" + safe + ".terrainlayer"));
			}
			return layers.ToArray();
		}

		static TerrainLayer SaveLayer(TerrainLayer layer, string path)
		{
			AssetDatabase.CreateAsset(layer, path);
			return layer;
		}

		static Texture2D CopyTexture(string source, string assetPath, bool normalMap)
		{
			if (!File.Exists(source))
				return null;
			File.Copy(source, DiskPath(assetPath), true);
			AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceSynchronousImport);
			if (normalMap && AssetImporter.GetAtPath(assetPath) is TextureImporter importer)
			{
				importer.textureType = TextureImporterType.NormalMap;
				importer.SaveAndReimport();
			}
			return AssetDatabase.LoadAssetAtPath<Texture2D>(assetPath);
		}

		static Texture2D ColorTexture(Color color, string assetPath)
		{
			var texture = new Texture2D(4, 4, TextureFormat.RGBA32, false);
			var pixels = new Color[16];
			for (int i = 0; i < pixels.Length; i++)
				pixels[i] = color;
			texture.SetPixels(pixels);
			File.WriteAllBytes(DiskPath(assetPath), texture.EncodeToPNG());
			UnityEngine.Object.DestroyImmediate(texture);
			AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceSynchronousImport);
			return AssetDatabase.LoadAssetAtPath<Texture2D>(assetPath);
		}

		// One material in a splat file of the pack: the pixels as LoadImage gives them (rows bottom-up) and its channel
		class Share
		{
			public Color32[] pixels;	// null = no splat file, the material covers nothing
			public int channel;

			public float At(Layout l, int column, int row)
			{
				if (pixels == null)
					return 0f;
				Color32 p = pixels[(l.height - 1 - row) * l.width + column];	// pack row r is image row height - 1 - r
				return (channel == 0 ? p.r : channel == 1 ? p.g : channel == 2 ? p.b : p.a) / 255f;
			}

			// Bilinear at a fractional pack texel, clamped to the map
			public float Sample(Layout l, float column, float row)
			{
				column = Mathf.Clamp(column, 0f, l.width - 1);
				row = Mathf.Clamp(row, 0f, l.height - 1);
				int x0 = Mathf.Min((int)column, Mathf.Max(l.width - 2, 0)), y0 = Mathf.Min((int)row, Mathf.Max(l.height - 2, 0));
				int x1 = Mathf.Min(x0 + 1, l.width - 1), y1 = Mathf.Min(y0 + 1, l.height - 1);
				float fx = column - x0, fy = row - y0;
				return Mathf.Lerp(Mathf.Lerp(At(l, x0, y0), At(l, x1, y0), fx), Mathf.Lerp(At(l, x0, y1), At(l, x1, y1), fx), fy);
			}
		}

		// The splat channel of every material (material order); null = no material maps. Each RGBA file is loaded once and shared
		static List<Share> ReadShares(Manifest manifest, string packFolder, Layout l, int layerCount)
		{
			if (manifest.materials == null || manifest.materials.list == null || manifest.materials.list.Length == 0)
				return null;
			var shares = new List<Share>();
			var images = new Dictionary<string, Color32[]>();
			foreach (MaterialEntry m in manifest.materials.list)
			{
				var share = new Share();
				shares.Add(share);
				if (m.splat == null || string.IsNullOrEmpty(m.splat.file))
					continue;
				if (!images.TryGetValue(m.splat.file, out Color32[] pixels))
				{
					var texture = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
					if (!texture.LoadImage(File.ReadAllBytes(Path.Combine(packFolder, m.splat.file))) || texture.width != l.width || texture.height != l.height)
						throw new Exception(m.splat.file + ": not a " + l.width + " x " + l.height + " PNG");
					pixels = texture.GetPixels32();
					UnityEngine.Object.DestroyImmediate(texture);
					images[m.splat.file] = pixels;
				}
				share.pixels = pixels;
				share.channel = Mathf.Max(0, "RGBA".IndexOf(string.IsNullOrEmpty(m.splat.channel) ? 'R' : m.splat.channel[0]));
			}
			return shares.Count == layerCount ? shares : null;
		}

		// ---- Vegetation ----

		static void ImportVegetation(Manifest manifest, string packFolder, string assetFolder, Layout l, Terrain[,] terrains, ref bool gltfMissing)
		{
			string jsonPath = Path.Combine(packFolder, manifest.vegetation.file);
			string text = File.ReadAllText(jsonPath);
			VegetationFile file = JsonUtility.FromJson<VegetationFile>(text);
			if (file == null || file.models == null)
			{
				Debug.LogWarning("PTG: cannot read " + manifest.vegetation.file + ", no vegetation");
				return;
			}
			bool anyFile = Array.Exists(file.models, m => !string.IsNullOrEmpty(m.file) && m.count > 0);
			if (!anyFile)
			{
				Debug.LogWarning("PTG: the vegetation models were not exported (\"Vegetation models\" off), no vegetation");
				return;
			}
			// The models with their textures (vegetation/ of the pack) into the project
			string vegetationFolder = Path.Combine(packFolder, "vegetation");
			string target = assetFolder + "/Vegetation";
			CopyFolder(vegetationFolder, DiskPath(target));
			AssetDatabase.Refresh();

			string prefabFolder = assetFolder + "/Prefabs";
			Directory.CreateDirectory(DiskPath(prefabFolder));
			AssetDatabase.Refresh();
			var prototypes = new List<TreePrototype>();
			var prototypeOf = new Dictionary<long, int>();		// model * 65536 + variant
			for (int mi = 0; mi < file.models.Length; mi++)
			{
				VegetationModel model = file.models[mi];
				if (string.IsNullOrEmpty(model.file) || model.count == 0)
					continue;
				string glb = target + "/" + Path.GetFileName(model.file);
				var asset = AssetDatabase.LoadAssetAtPath<GameObject>(glb);
				if (asset == null)
				{
					gltfMissing = true;
					return;
				}
				LodRange range = ReadLodRange(text, model.layer);
				for (int v = 0; v < model.variants; v++)
				{
					GameObject prefab = MakeTreePrefab(asset, model.name, v, range, prefabFolder);
					if (prefab == null)
						continue;
					prototypeOf[(long)mi * 65536 + v] = prototypes.Count;
					prototypes.Add(new TreePrototype { prefab = prefab });
				}
			}
			if (prototypes.Count == 0)
				return;

			var instances = new List<TreeInstance>[l.tilesX, l.tilesZ];
			for (int tz = 0; tz < l.tilesZ; tz++)
				for (int tx = 0; tx < l.tilesX; tx++)
					instances[tx, tz] = new List<TreeInstance>();
			int skipped = 0;
			ForEachInstance(text, row =>
			{
				int model = (int)row[5], variant = (int)row[6];
				if (!prototypeOf.TryGetValue((long)model * 65536 + variant, out int prototype))
				{
					skipped++;
					return;
				}
				float x = (float)row[0], y = (float)row[1], unityZ = l.SizeZ - (float)row[2];
				int tx = Mathf.Clamp(Mathf.FloorToInt(x / l.TileMetresX), 0, l.tilesX - 1);
				int tz = Mathf.Clamp(Mathf.FloorToInt(unityZ / l.TileMetresZ), 0, l.tilesZ - 1);
				var instance = new TreeInstance
				{
					position = new Vector3((x - tx * l.TileMetresX) / l.TileMetresX, Mathf.Clamp01((y - l.metresMin) / l.metresSpan), (unityZ - tz * l.TileMetresZ) / l.TileMetresZ),
					rotation = Mathf.PI - (float)row[3],
					widthScale = (float)row[4],
					heightScale = (float)row[4],
					color = Color.white,
					lightmapColor = Color.white,
					prototypeIndex = prototype,
				};
				instances[tx, tz].Add(instance);
			});
			int placed = 0;
			for (int tz = 0; tz < l.tilesZ; tz++)
			{
				for (int tx = 0; tx < l.tilesX; tx++)
				{
					TerrainData data = terrains[tx, tz].terrainData;
					data.treePrototypes = prototypes.ToArray();
					data.SetTreeInstances(instances[tx, tz].ToArray(), true);
					placed += instances[tx, tz].Count;
					EditorUtility.SetDirty(data);
				}
			}
			Debug.Log(string.Format("PTG: {0} plants of {1} prefabs{2}", placed, prototypes.Count, skipped > 0 ? ", " + skipped + " of models not exported left out" : ""));
		}

		// A prefab of nodes <name>_<variant>_LOD<n> of an imported glTF with a LODGroup switching at the app's distances
		static GameObject MakeTreePrefab(GameObject asset, string modelName, int variant, LodRange range, string prefabFolder)
		{
			var root = new GameObject(modelName + "_" + variant);
			var lods = new List<Renderer[]>();
			for (int n = 0; ; n++)
			{
				Transform node = FindDeep(asset.transform, modelName + "_" + variant + "_LOD" + n);
				if (node == null)
					break;
				GameObject copy = UnityEngine.Object.Instantiate(node.gameObject, root.transform, false);
				copy.name = node.name;
				copy.transform.localPosition = Vector3.zero;
				copy.transform.localRotation = Quaternion.identity;
				lods.Add(copy.GetComponentsInChildren<Renderer>());
			}
			if (lods.Count == 0)
			{
				UnityEngine.Object.DestroyImmediate(root);
				return null;
			}
			var group = root.AddComponent<LODGroup>();
			// Size of the object from its nearest level (the group has no renderers before SetLODs, so its own size is not known yet)
			float size = 0.1f;
			if (lods[0].Length > 0)
			{
				Bounds bounds = lods[0][0].bounds;
				foreach (Renderer r in lods[0])
					bounds.Encapsulate(r.bounds);
				size = Mathf.Max(size, Mathf.Max(bounds.size.x, Mathf.Max(bounds.size.y, bounds.size.z)));
			}
			// Screen height of the object at distance d with a 60 degree field of view
			float ScreenHeight(float d) => size / (Mathf.Max(d, 0.01f) * 2f * Mathf.Tan(30f * Mathf.Deg2Rad));
			float[] ends = { range.lod1, range.lod2, range.max };
			var lodArray = new LOD[lods.Count];
			float previous = 1f;
			for (int n = 0; n < lods.Count; n++)
			{
				float end = n == lods.Count - 1 ? range.max : ends[Mathf.Min(n, 2)];
				float height = Mathf.Min(ScreenHeight(end), previous * 0.99f);
				lodArray[n] = new LOD(height, lods[n]);
				previous = height;
			}
			group.SetLODs(lodArray);
			group.RecalculateBounds();
			string path = prefabFolder + "/" + SafeName(root.name) + ".prefab";
			GameObject prefab = PrefabUtility.SaveAsPrefabAsset(root, path);
			UnityEngine.Object.DestroyImmediate(root);
			return prefab;
		}

		static Transform FindDeep(Transform parent, string name)
		{
			if (parent.name == name)
				return parent;
			foreach (Transform child in parent)
			{
				Transform found = FindDeep(child, name);
				if (found != null)
					return found;
			}
			return null;
		}

		// "lodRanges": { "<layer>": { "lod1": .., "lod2": .., "max": .. } } (a dictionary, which JsonUtility cannot read)
		static LodRange ReadLodRange(string text, string layer)
		{
			var range = new LodRange { lod1 = 40f, lod2 = 100f, max = 1000f };
			int start = text.IndexOf("\"lodRanges\"", StringComparison.Ordinal);
			if (start < 0 || string.IsNullOrEmpty(layer))
				return range;
			int at = text.IndexOf("\"" + layer + "\"", start, StringComparison.Ordinal);
			if (at < 0)
				return range;
			int end = text.IndexOf('}', at);
			range.lod1 = NumberAfter(text, at, end, "\"lod1\"", range.lod1);
			range.lod2 = NumberAfter(text, at, end, "\"lod2\"", range.lod2);
			range.max = NumberAfter(text, at, end, "\"max\"", range.max);
			return range;
		}

		static float NumberAfter(string text, int from, int to, string key, float fallback)
		{
			int at = text.IndexOf(key, from, StringComparison.Ordinal);
			if (at < 0 || at > to)
				return fallback;
			int i = text.IndexOf(':', at) + 1;
			int j = i;
			while (j < text.Length && (char.IsWhiteSpace(text[j]) || "+-.eE0123456789".IndexOf(text[j]) >= 0))
				j++;
			return float.TryParse(text.Substring(i, j - i).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float v) ? v : fallback;
		}

		// Rows [x, y, z, yaw, scale, model, variant] of "instances", read with a number scanner (JsonUtility cannot read arrays of arrays)
		static void ForEachInstance(string text, Action<double[]> action)
		{
			int start = text.IndexOf("\"instances\"", StringComparison.Ordinal);
			if (start < 0)
				return;
			int i = text.IndexOf('[', start);
			if (i < 0)
				return;
			int depth = 0;
			var row = new double[7];
			int column = 0;
			for (; i < text.Length; i++)
			{
				char ch = text[i];
				if (ch == '[')
				{
					depth++;
					column = 0;
				}
				else if (ch == ']')
				{
					if (depth == 2 && column == 7)
						action(row);
					if (--depth == 0)
						return;
				}
				else if (ch == '-' || ch == '.' || char.IsDigit(ch))
				{
					int j = i;
					while (j < text.Length && "+-.eE0123456789".IndexOf(text[j]) >= 0)
						j++;
					if (column < 7)
						row[column] = double.Parse(text.Substring(i, j - i), NumberStyles.Float, CultureInfo.InvariantCulture);
					column++;
					i = j - 1;
				}
			}
		}

		// ---- Water ----

		static void ImportWater(Manifest manifest, string packFolder, string assetFolder, Layout l, bool hasSea, Transform parent, ref bool gltfMissing)
		{
			string waterFile = manifest.meshes != null && manifest.meshes.water != null ? manifest.meshes.water.file : null;
			if (string.IsNullOrEmpty(waterFile) && !hasSea)
				return;
			Material material = WaterMaterial(assetFolder + "/Water.mat");
			var water = new GameObject("Water");
			water.transform.SetParent(parent, false);
			bool fromGltf = false;
			if (!string.IsNullOrEmpty(waterFile) && waterFile.EndsWith(".glb", StringComparison.OrdinalIgnoreCase) && File.Exists(Path.Combine(packFolder, waterFile)))
			{
				string assetPath = assetFolder + "/" + Path.GetFileName(waterFile);
				File.Copy(Path.Combine(packFolder, waterFile), DiskPath(assetPath), true);
				AssetDatabase.ImportAsset(assetPath, ImportAssetOptions.ForceSynchronousImport);
				var asset = AssetDatabase.LoadAssetAtPath<GameObject>(assetPath);
				if (asset == null)
				{
					gltfMissing = true;
				}
				else
				{
					// glTFast gives (-x, y, z); turned 180 degrees and moved by sizeZ that is (x, y, sizeZ - z)
					GameObject surfaces = UnityEngine.Object.Instantiate(asset, water.transform, false);
					surfaces.name = "water_glTF";
					surfaces.transform.localRotation = Quaternion.Euler(0f, 180f, 0f);
					surfaces.transform.localPosition = new Vector3(0f, 0f, l.SizeZ);
					foreach (Renderer r in surfaces.GetComponentsInChildren<Renderer>())
						r.sharedMaterial = material;
					fromGltf = true;
				}
			}
			if (!fromGltf && hasSea)
			{
				float sizeX = (l.width - 1) * l.metresPerTexel;
				GameObject sea = GameObject.CreatePrimitive(PrimitiveType.Plane);	// 10 x 10 units
				sea.name = "Sea";
				UnityEngine.Object.DestroyImmediate(sea.GetComponent<Collider>());
				sea.transform.SetParent(water.transform, false);
				sea.transform.localPosition = new Vector3(sizeX * 0.5f, manifest.seaLevel.metres, l.SizeZ * 0.5f);
				sea.transform.localScale = new Vector3(sizeX / 10f, 1f, l.SizeZ / 10f);
				sea.GetComponent<Renderer>().sharedMaterial = material;
			}
		}

		static Material WaterMaterial(string assetPath)
		{
			RenderPipelineAsset pipeline = GraphicsSettings.currentRenderPipeline;
			Shader shader = pipeline != null && pipeline.defaultMaterial != null ? pipeline.defaultMaterial.shader : Shader.Find("Standard");
			var material = new Material(shader) { name = "PTG water" };
			var color = new Color(0.05f, 0.22f, 0.30f, 1f);
			if (material.HasProperty("_Color"))
				material.SetColor("_Color", color);
			if (material.HasProperty("_BaseColor"))
				material.SetColor("_BaseColor", color);
			if (material.HasProperty("_Smoothness"))
				material.SetFloat("_Smoothness", 0.95f);
			if (material.HasProperty("_Glossiness"))
				material.SetFloat("_Glossiness", 0.95f);
			AssetDatabase.CreateAsset(material, assetPath);
			return material;
		}

		// ---- Files ----

		static string DiskPath(string assetPath)
		{
			return Path.Combine(Path.GetDirectoryName(Application.dataPath), assetPath);
		}

		static void CopyFolder(string from, string to)
		{
			if (!Directory.Exists(from))
				return;
			Directory.CreateDirectory(to);
			foreach (string file in Directory.GetFiles(from))
				File.Copy(file, Path.Combine(to, Path.GetFileName(file)), true);
			foreach (string sub in Directory.GetDirectories(from))
				CopyFolder(sub, Path.Combine(to, Path.GetFileName(sub)));
		}

		static string SafeName(string name)
		{
			var chars = name.ToCharArray();
			for (int i = 0; i < chars.Length; i++)
			{
				if (Array.IndexOf(Path.GetInvalidFileNameChars(), chars[i]) >= 0 || chars[i] == ' ')
					chars[i] = '_';
			}
			return new string(chars);
		}
	}
}
#endif
