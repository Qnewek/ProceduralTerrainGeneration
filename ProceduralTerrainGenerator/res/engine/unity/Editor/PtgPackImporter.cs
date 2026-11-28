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
using System.Text;
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
		const int StreamBufferSize = 65536;
		const string DialogTitle = "PTG pack import";

		// ---- Manifest (only the fields the importer reads; JsonUtility ignores the rest) ----
		[Serializable] class Manifest { public string generator; public string name; public Grid grid; public FileEntry[] files; public Materials materials; public Meshes meshes; public VegetationInfo vegetation; public SeaLevel seaLevel; }
		[Serializable] class Grid { public int width; public int height; public float metresPerTexel; }
		[Serializable] class SeaLevel { public float metres; }
		[Serializable] class FileEntry { public string path; public string content; public string format; public EncodingInfo encoding; }
		[Serializable] class EncodingInfo { public float metresMin; public float metresMax; }
		[Serializable] class Materials { public MaterialEntry[] list; }
		[Serializable] class MaterialEntry { public int id; public string name; public Splat splat; public float[] fallbackColor; public Textures textures; }
		[Serializable] class Splat { public string file; public string channel; }
		[Serializable] class Textures { public string albedo; public string normal; public string roughness; public float tileMetres; }
		[Serializable] class Meshes { public Water water; }
		[Serializable] class Water { public string file; }
		[Serializable] class VegetationInfo { public string file; }
		[Serializable] class VegetationFile { public VegetationModel[] models; }
		[Serializable] class VegetationModel { public string name; public string layer; public string file; public int variants; public int count; }

		internal struct LodRange { public float lod1, lod2, max; }

		enum TextureKind { Color, Normal, Mask }
		struct PendingTexture
		{
			public string assetPath;
			public TextureKind kind;
		}
		struct LayerSlot
		{
			public string safeName;
			public float tileMetres;
			public string diffuseAssetPath;
			public string normalAssetPath;
			public string maskAssetPath;
		}

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
			if (manifest == null || manifest.generator != "Procedural Terrain Generator" || manifest.grid == null || manifest.files == null)
				throw new Exception(manifestPath + " is not a manifest of a Procedural Terrain Generator pack");
			if (manifest.grid.width < 2 || manifest.grid.height < 2 || !(manifest.grid.metresPerTexel > 0f))
				throw new Exception("The pack has an invalid terrain grid size");

			string packFolder = Path.GetDirectoryName(Path.GetFullPath(manifestPath));
			// The manifest's own "seaLevel" is an object; the generation preset inside it has a number of that name
			bool hasSea = Regex.IsMatch(text, "\"seaLevel\"\\s*:\\s*\\{");
			string name = ValidatePackName(manifest.name);

			FileEntry heightFile = Array.Find(manifest.files, f => f != null && f.content == "height" && f.format == "r16");
			if (heightFile == null || heightFile.encoding == null)
				throw new Exception("The pack has no R16 height file: export it with \"Engine import scripts\" on.");
			string heightPath = SafePackPath(packFolder, heightFile.path);

			// Fresh assets folder and GameObject
			string assetFolder = "Assets/PTG/" + name;
			if (AssetDatabase.IsValidFolder(assetFolder))
				AssetDatabase.DeleteAsset(assetFolder);
			Directory.CreateDirectory(DiskPath(assetFolder));
			AssetDatabase.Refresh();
			// GameObject.Find skips inactive objects: a root switched off would stay and the import would add a second one
			foreach (GameObject old in EditorSceneManager.GetActiveScene().GetRootGameObjects())
			{
				if (old.name == name)
					UnityEngine.Object.DestroyImmediate(old);
			}

			EditorUtility.DisplayProgressBar(DialogTitle, "Reading heights", 0.05f);
			Layout layout = MakeLayout(manifest, heightFile);
			float[] heights = ReadR16(heightPath, layout);

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

			// Release raw height & splat buffers before loading vegetation and water assets
			if (shares != null)
			{
				foreach (Share s in shares)
					s.pixels = null;
				shares.Clear();
				shares = null;
			}
			heights = null;

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

		// ---- Security helpers ----

		static StringComparison PathComparison =>
			Path.DirectorySeparatorChar == '\\' ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;

		static string EnsureTrailingSeparator(string path)
		{
			if (string.IsNullOrEmpty(path))
				return path;
			char last = path[path.Length - 1];
			return (last == Path.DirectorySeparatorChar || last == Path.AltDirectorySeparatorChar)
				? path
				: path + Path.DirectorySeparatorChar;
		}

		internal static string ValidatePackName(string name)
		{
			if (string.IsNullOrWhiteSpace(name) || name.Trim() != name)
				throw new Exception("Invalid pack name: empty or whitespace");
			if (name == "." || name == ".." || name.Contains("..")
				|| name.IndexOfAny(new[] { '/', '\\', ':' }) >= 0
				|| name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
			{
				throw new Exception("Invalid pack name: " + name);
			}
			return name;
		}

		internal static string SafePackPath(string packFolder, string relativePath)
		{
			if (string.IsNullOrWhiteSpace(packFolder))
				throw new Exception("Pack folder is empty");
			if (string.IsNullOrWhiteSpace(relativePath))
				throw new Exception("Pack file path is empty");
			if (Path.IsPathRooted(relativePath) || relativePath[0] == '/' || relativePath[0] == '\\' || relativePath.IndexOf(':') >= 0)
				throw new Exception("Unsafe absolute path in pack manifest: " + relativePath);
			string[] segments = relativePath.Split(new[] { '/', '\\' });
			foreach (string segment in segments)
			{
				if (segment == "..")
					throw new Exception("Unsafe parent-directory reference in pack manifest: " + relativePath);
			}
			string rootFull = EnsureTrailingSeparator(Path.GetFullPath(packFolder));
			string combined = Path.GetFullPath(Path.Combine(rootFull, relativePath));
			if (!combined.StartsWith(rootFull, PathComparison))
				throw new Exception("Path escapes pack folder: " + relativePath);
			return combined;
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
			long countLong = (long)l.width * l.height;
			if (countLong <= 0 || countLong > int.MaxValue / 2)
				throw new Exception("Terrain dimensions are too large: " + l.width + " x " + l.height);
			byte[] bytes = File.ReadAllBytes(path);
			int count = (int)countLong;
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
			if (manifest.materials == null || manifest.materials.list == null || manifest.materials.list.Length == 0)
			{
				string greyTexPath = assetFolder + "/grey.png";
				WriteColorTexturePng(new Color(0.5f, 0.5f, 0.5f, 1f).gamma, greyTexPath);
				AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
				var greyLayer = new TerrainLayer
				{
					diffuseTexture = AssetDatabase.LoadAssetAtPath<Texture2D>(greyTexPath),
					tileSize = new Vector2(4f, 4f)
				};
				AssetDatabase.CreateAsset(greyLayer, assetFolder + "/grey.terrainlayer");
				return new[] { greyLayer };
			}

			string folder = assetFolder + "/Materials";
			Directory.CreateDirectory(DiskPath(folder));
			var slots = new List<LayerSlot>(manifest.materials.list.Length);
			var pending = new List<PendingTexture>();

			foreach (MaterialEntry m in manifest.materials.list)
			{
				string safe = SafeName(m.id.ToString("00") + "_" + (m.name ?? "material"));
				bool textured = m.textures != null && !string.IsNullOrEmpty(m.textures.albedo);
				float tile = textured && m.textures.tileMetres > 0f ? m.textures.tileMetres : 4f * manifest.grid.metresPerTexel;
				var slot = new LayerSlot { safeName = safe, tileMetres = tile };

				if (textured)
				{
					string albedoSrc = SafePackPath(packFolder, m.textures.albedo);
					if (File.Exists(albedoSrc))
					{
						string albedoAsset = folder + "/" + safe + "_albedo" + Path.GetExtension(albedoSrc);
						File.Copy(albedoSrc, DiskPath(albedoAsset), true);
						slot.diffuseAssetPath = albedoAsset;
						pending.Add(new PendingTexture { assetPath = albedoAsset, kind = TextureKind.Color });
					}
					if (!string.IsNullOrEmpty(m.textures.normal))
					{
						string normalSrc = SafePackPath(packFolder, m.textures.normal);
						if (File.Exists(normalSrc))
						{
							string normalAsset = folder + "/" + safe + "_normal" + Path.GetExtension(normalSrc);
							File.Copy(normalSrc, DiskPath(normalAsset), true);
							slot.normalAssetPath = normalAsset;
							pending.Add(new PendingTexture { assetPath = normalAsset, kind = TextureKind.Normal });
						}
					}
					if (!string.IsNullOrEmpty(m.textures.roughness))
					{
						string roughSrc = SafePackPath(packFolder, m.textures.roughness);
						string maskAsset = folder + "/" + safe + "_mask.png";
						if (WriteMaskMapPng(roughSrc, DiskPath(maskAsset)))
						{
							slot.maskAssetPath = maskAsset;
							pending.Add(new PendingTexture { assetPath = maskAsset, kind = TextureKind.Mask });
						}
					}
				}
				if (slot.diffuseAssetPath == null)
				{
					float[] c = m.fallbackColor != null && m.fallbackColor.Length >= 3 ? m.fallbackColor : new[] { 0.5f, 0.5f, 0.5f };
					// Pack fallbackColor is linear [0, 1]; convert to sRGB for an RGBA32 PNG albedo texture
					Color srgb = new Color(Mathf.Clamp01(c[0]), Mathf.Clamp01(c[1]), Mathf.Clamp01(c[2]), 1f).gamma;
					string colorAsset = folder + "/" + safe + "_color.png";
					WriteColorTexturePng(srgb, colorAsset);
					slot.diffuseAssetPath = colorAsset;
					pending.Add(new PendingTexture { assetPath = colorAsset, kind = TextureKind.Color });
				}
				slots.Add(slot);
			}

			// One synchronous refresh + batched importer updates avoid per-texture domain/DB overhead
			AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
			AssetDatabase.StartAssetEditing();
			try
			{
				foreach (PendingTexture tex in pending)
				{
					if (tex.kind == TextureKind.Color)
						continue;
					if (AssetImporter.GetAtPath(tex.assetPath) is TextureImporter importer)
					{
						if (tex.kind == TextureKind.Normal)
							importer.textureType = TextureImporterType.NormalMap;
						else if (tex.kind == TextureKind.Mask)
							importer.sRGBTexture = false;
						importer.SaveAndReimport();
					}
				}
			}
			finally
			{
				AssetDatabase.StopAssetEditing();
			}

			var layers = new TerrainLayer[slots.Count];
			AssetDatabase.StartAssetEditing();
			try
			{
				for (int i = 0; i < slots.Count; i++)
				{
					LayerSlot slot = slots[i];
					var layer = new TerrainLayer
					{
						tileSize = new Vector2(slot.tileMetres, slot.tileMetres),
						diffuseTexture = AssetDatabase.LoadAssetAtPath<Texture2D>(slot.diffuseAssetPath),
						normalMapTexture = slot.normalAssetPath != null ? AssetDatabase.LoadAssetAtPath<Texture2D>(slot.normalAssetPath) : null,
						maskMapTexture = slot.maskAssetPath != null ? AssetDatabase.LoadAssetAtPath<Texture2D>(slot.maskAssetPath) : null
					};
					if (layer.maskMapTexture != null)
					{
						layer.metallic = 0f;
						layer.smoothness = 1f;
					}
					else
					{
						layer.smoothness = 0.1f;
					}
					AssetDatabase.CreateAsset(layer, folder + "/" + slot.safeName + ".terrainlayer");
					layers[i] = layer;
				}
			}
			finally
			{
				AssetDatabase.StopAssetEditing();
			}
			return layers;
		}

		static void WriteColorTexturePng(Color color, string assetPath)
		{
			var texture = new Texture2D(4, 4, TextureFormat.RGBA32, false);
			try
			{
				var pixels = new Color[16];
				for (int i = 0; i < pixels.Length; i++)
					pixels[i] = color;
				texture.SetPixels(pixels);
				File.WriteAllBytes(DiskPath(assetPath), texture.EncodeToPNG());
			}
			finally
			{
				UnityEngine.Object.DestroyImmediate(texture);
			}
		}

		// Packs Unity Terrain MaskMap: R = metallic (0), G = AO (255), B = detail mask (0), A = smoothness (1 - roughness)
		static bool WriteMaskMapPng(string roughnessSource, string destDiskPath)
		{
			if (!File.Exists(roughnessSource))
				return false;
			var src = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
			Texture2D dst = null;
			try
			{
				if (!src.LoadImage(File.ReadAllBytes(roughnessSource)))
					return false;
				int w = src.width, h = src.height;
				if (w <= 0 || h <= 0)
					return false;
				Color32[] pixels = src.GetPixels32();
				for (int i = 0; i < pixels.Length; i++)
				{
					byte smoothness = (byte)(255 - pixels[i].r);
					pixels[i] = new Color32(0, 255, 0, smoothness);
				}
				dst = new Texture2D(w, h, TextureFormat.RGBA32, false, true);
				dst.SetPixels32(pixels);
				dst.Apply(false, false);
				File.WriteAllBytes(destDiskPath, dst.EncodeToPNG());
				return true;
			}
			finally
			{
				UnityEngine.Object.DestroyImmediate(src);
				if (dst != null)
					UnityEngine.Object.DestroyImmediate(dst);
			}
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
				string splatPath = SafePackPath(packFolder, m.splat.file);
				if (!images.TryGetValue(splatPath, out Color32[] pixels))
				{
					var texture = new Texture2D(2, 2, TextureFormat.RGBA32, false, true);
					try
					{
						if (!texture.LoadImage(File.ReadAllBytes(splatPath)) || texture.width != l.width || texture.height != l.height)
							throw new Exception(m.splat.file + ": not a " + l.width + " x " + l.height + " PNG");
						pixels = texture.GetPixels32();
						images[splatPath] = pixels;
					}
					finally
					{
						UnityEngine.Object.DestroyImmediate(texture);
					}
				}
				share.pixels = pixels;
				share.channel = Mathf.Max(0, "RGBA".IndexOf(string.IsNullOrEmpty(m.splat.channel) ? 'R' : m.splat.channel[0]));
			}
			return shares.Count == layerCount ? shares : null;
		}

		// ---- Vegetation ----

		static void ImportVegetation(Manifest manifest, string packFolder, string assetFolder, Layout l, Terrain[,] terrains, ref bool gltfMissing)
		{
			string jsonPath = SafePackPath(packFolder, manifest.vegetation.file);
			string header = ReadVegetationHeader(jsonPath);
			VegetationFile file = JsonUtility.FromJson<VegetationFile>(header);
			if (file == null || file.models == null)
			{
				Debug.LogWarning("PTG: cannot read " + manifest.vegetation.file + ", no vegetation");
				return;
			}
			bool anyFile = Array.Exists(file.models, m => m != null && !string.IsNullOrEmpty(m.file) && m.count > 0);
			if (!anyFile)
			{
				Debug.LogWarning("PTG: the vegetation models were not exported (\"Vegetation models\" off), no vegetation");
				return;
			}
			// Validate every model path before copying vegetation assets
			foreach (VegetationModel model in file.models)
			{
				if (model != null && !string.IsNullOrEmpty(model.file))
					SafePackPath(packFolder, model.file);
			}
			// The models with their textures (vegetation/ of the pack) into the project
			string vegetationFolder = SafePackPath(packFolder, "vegetation");
			string target = assetFolder + "/Vegetation";
			CopyFolder(vegetationFolder, DiskPath(target));
			AssetDatabase.Refresh();

			string prefabFolder = assetFolder + "/Prefabs";
			Directory.CreateDirectory(DiskPath(prefabFolder));
			AssetDatabase.Refresh();
			List<List<string>>[] variantNodesByModel = ReadVariantNodes(header, file.models.Length);
			var prototypes = new List<TreePrototype>();
			var prototypeOf = new Dictionary<long, int>();		// model * 65536 + variant
			float treeDistance = 0f;							// the farthest a plant is drawn in the app
			for (int mi = 0; mi < file.models.Length; mi++)
			{
				VegetationModel model = file.models[mi];
				if (model == null || string.IsNullOrEmpty(model.file) || model.count <= 0 || model.variants <= 0)
					continue;
				string rel = model.file.Replace('\\', '/');
				if (rel.StartsWith("vegetation/", StringComparison.OrdinalIgnoreCase))
					rel = rel.Substring("vegetation/".Length);
				string glb = target + "/" + rel;
				var asset = AssetDatabase.LoadAssetAtPath<GameObject>(glb);
				if (asset == null)
				{
					gltfMissing = true;
					return;
				}
				LodRange range = ReadLodRange(header, model.layer);
				treeDistance = Mathf.Max(treeDistance, range.max);
				List<List<string>> modelVariants = variantNodesByModel[mi];
				for (int v = 0; v < model.variants; v++)
				{
					List<string> variantNodes = modelVariants != null && v < modelVariants.Count ? modelVariants[v] : null;
					GameObject prefab = MakeTreePrefab(asset, model.name, v, variantNodes, range, prefabFolder);
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
			ForEachInstanceFile(jsonPath, row =>
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
					// Plants vanish where they do in the app, not at Unity's default 5000 m
					terrains[tx, tz].treeDistance = treeDistance > 0f ? treeDistance : 1000f;
					TerrainData data = terrains[tx, tz].terrainData;
					data.treePrototypes = prototypes.ToArray();
					// snapToHeightmap = false: keep the exported Y including sinkPerSlope on slopes
					data.SetTreeInstances(instances[tx, tz].ToArray(), false);
					placed += instances[tx, tz].Count;
					EditorUtility.SetDirty(data);
				}
			}
			Debug.Log(string.Format("PTG: {0} plants of {1} prefabs{2}", placed, prototypes.Count, skipped > 0 ? ", " + skipped + " of models not exported left out" : ""));
		}

		// A prefab of nodes in variantNodes (or <name>_<variant>_LOD<n> as fallback) of an imported glTF with a LODGroup
		static GameObject MakeTreePrefab(GameObject asset, string modelName, int variant, List<string> variantNodes, LodRange range, string prefabFolder)
		{
			var root = new GameObject(modelName + "_" + variant);
			var lods = new List<Renderer[]>();
			if (variantNodes != null && variantNodes.Count > 0)
			{
				foreach (string nodeName in variantNodes)
				{
					Transform node = FindDeep(asset.transform, nodeName);
					if (node == null)
						continue;
					GameObject copy = UnityEngine.Object.Instantiate(node.gameObject, root.transform, false);
					copy.name = node.name;
					copy.transform.localPosition = Vector3.zero;
					copy.transform.localRotation = Quaternion.identity;
					lods.Add(copy.GetComponentsInChildren<Renderer>());
				}
			}
			else
			{
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

		// Streams _vegetation.json while stripping the "instances" array body so huge files never allocate large strings
		internal static string ReadVegetationHeader(string jsonPath)
		{
			var sb = new StringBuilder(4096);
			var keyBuf = new StringBuilder(32);
			bool inString = false;
			bool escaped = false;
			bool capturingKey = false;
			string lastKey = null;
			bool pendingInstancesColon = false;
			bool skippingInstancesArray = false;
			int skippedDepth = 0;
			int braceDepth = 0;
			int bracketDepth = 0;

			using (var reader = new StreamReader(jsonPath, System.Text.Encoding.UTF8, true, StreamBufferSize))
			{
				var buf = new char[StreamBufferSize];
				int read;
				while ((read = reader.Read(buf, 0, buf.Length)) > 0)
				{
					for (int i = 0; i < read; i++)
					{
						char ch = buf[i];
						if (skippingInstancesArray)
						{
							if (inString)
							{
								if (escaped) escaped = false;
								else if (ch == '\\') escaped = true;
								else if (ch == '"') inString = false;
							}
							else
							{
								if (ch == '"') inString = true;
								else if (ch == '[') skippedDepth++;
								else if (ch == ']')
								{
									skippedDepth--;
									if (skippedDepth == 0)
									{
										sb.Append(']');
										skippingInstancesArray = false;
										bracketDepth = 0;
									}
								}
							}
							continue;
						}

						sb.Append(ch);
						if (inString)
						{
							if (escaped)
							{
								escaped = false;
								if (capturingKey) keyBuf.Append(ch);
							}
							else if (ch == '\\')
							{
								escaped = true;
								if (capturingKey) keyBuf.Append(ch);
							}
							else if (ch == '"')
							{
								inString = false;
								if (capturingKey)
								{
									lastKey = keyBuf.ToString();
									capturingKey = false;
								}
							}
							else if (capturingKey && keyBuf.Length < 64)
							{
								keyBuf.Append(ch);
							}
						}
						else
						{
							if (ch == '"')
							{
								inString = true;
								if (braceDepth == 1 && bracketDepth == 0)
								{
									capturingKey = true;
									keyBuf.Clear();
								}
							}
							else if (ch == ':' && braceDepth == 1 && bracketDepth == 0 && lastKey == "instances")
							{
								pendingInstancesColon = true;
							}
							else if (pendingInstancesColon)
							{
								if (ch == '[')
								{
									skippingInstancesArray = true;
									skippedDepth = 1;
									pendingInstancesColon = false;
									lastKey = null;
								}
								else if (!char.IsWhiteSpace(ch))
								{
									pendingInstancesColon = false;
									lastKey = null;
								}
							}
							else if (ch == '{')
							{
								braceDepth++;
								lastKey = null;
							}
							else if (ch == '}')
							{
								braceDepth--;
								lastKey = null;
							}
							else if (ch == '[')
							{
								bracketDepth++;
							}
							else if (ch == ']')
							{
								bracketDepth--;
							}
							else if (!char.IsWhiteSpace(ch))
							{
								lastKey = null;
							}
						}
					}
				}
			}
			return sb.ToString();
		}

		// Reads "variantNodes": [[...], ...] for each entry of "models" in the compact header JSON
		internal static List<List<string>>[] ReadVariantNodes(string headerJson, int modelCount)
		{
			var result = new List<List<string>>[Math.Max(modelCount, 0)];
			if (string.IsNullOrEmpty(headerJson) || modelCount <= 0)
				return result;
			int start = FindJsonKeyValue(headerJson, "\"models\"", 0, headerJson.Length, '[');
			if (start < 0)
				return result;
			int braceDepth = 0;
			int bracketDepth = 1;
			int modelIndex = -1;
			for (int i = start + 1; i < headerJson.Length && bracketDepth > 0; i++)
			{
				char ch = headerJson[i];
				if (ch == '"')
				{
					string str = ReadJsonString(headerJson, ref i);
					if (braceDepth == 1 && bracketDepth == 1 && str == "variantNodes")
					{
						int j = SkipWhitespace(headerJson, i + 1);
						if (j < headerJson.Length && headerJson[j] == ':')
						{
							j = SkipWhitespace(headerJson, j + 1);
							if (j < headerJson.Length && headerJson[j] == '[' && modelIndex >= 0 && modelIndex < result.Length)
							{
								result[modelIndex] = ParseStringMatrix(headerJson, ref j);
								i = j;
							}
						}
					}
				}
				else if (ch == '{')
				{
					if (braceDepth == 0 && bracketDepth == 1)
						modelIndex++;
					braceDepth++;
				}
				else if (ch == '}')
				{
					braceDepth--;
				}
				else if (ch == '[')
				{
					bracketDepth++;
				}
				else if (ch == ']')
				{
					bracketDepth--;
				}
			}
			return result;
		}

		static List<List<string>> ParseStringMatrix(string text, ref int index)
		{
			var matrix = new List<List<string>>();
			int depth = 1;
			List<string> current = null;
			for (index = index + 1; index < text.Length && depth > 0; index++)
			{
				char ch = text[index];
				if (ch == '[')
				{
					depth++;
					if (depth == 2)
						current = new List<string>();
				}
				else if (ch == ']')
				{
					if (depth == 2 && current != null)
					{
						matrix.Add(current);
						current = null;
					}
					depth--;
					if (depth == 0)
						break;
				}
				else if (ch == '"')
				{
					string s = ReadJsonString(text, ref index);
					if (depth == 2 && current != null)
						current.Add(s);
				}
			}
			return matrix;
		}

		static string ReadJsonString(string text, ref int quoteIndex)
		{
			var sb = new StringBuilder();
			for (int i = quoteIndex + 1; i < text.Length; i++)
			{
				char ch = text[i];
				if (ch == '\\' && i + 1 < text.Length)
				{
					char next = text[++i];
					switch (next)
					{
						case '"': sb.Append('"'); break;
						case '\\': sb.Append('\\'); break;
						case '/': sb.Append('/'); break;
						case 'b': sb.Append('\b'); break;
						case 'f': sb.Append('\f'); break;
						case 'n': sb.Append('\n'); break;
						case 'r': sb.Append('\r'); break;
						case 't': sb.Append('\t'); break;
						case 'u':
							if (i + 4 < text.Length && int.TryParse(text.Substring(i + 1, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out int code))
							{
								sb.Append((char)code);
								i += 4;
							}
							break;
						default: sb.Append(next); break;
					}
				}
				else if (ch == '"')
				{
					quoteIndex = i;
					return sb.ToString();
				}
				else
				{
					sb.Append(ch);
				}
			}
			quoteIndex = text.Length - 1;
			return sb.ToString();
		}

		static int SkipWhitespace(string text, int index)
		{
			while (index < text.Length && char.IsWhiteSpace(text[index]))
				index++;
			return index;
		}

		static int FindJsonKeyValue(string text, string quotedKey, int from, int to, char expectedStart)
		{
			int at = text.IndexOf(quotedKey, from, StringComparison.Ordinal);
			if (at < 0 || at >= to)
				return -1;
			int colon = text.IndexOf(':', at + quotedKey.Length);
			if (colon < 0 || colon >= to)
				return -1;
			int val = SkipWhitespace(text, colon + 1);
			return val < to && text[val] == expectedStart ? val : -1;
		}

		// "lodRanges": { "<layer>": { "lod1": .., "lod2": .., "max": .. } } (a dictionary, which JsonUtility cannot read)
		internal static LodRange ReadLodRange(string text, string layer)
		{
			var range = new LodRange { lod1 = 40f, lod2 = 100f, max = 1000f };
			int start = text.IndexOf("\"lodRanges\"", StringComparison.Ordinal);
			if (start < 0 || string.IsNullOrEmpty(layer))
				return range;
			int at = text.IndexOf("\"" + layer + "\"", start, StringComparison.Ordinal);
			if (at < 0)
				return range;
			int end = text.IndexOf('}', at);
			if (end < 0)
				return range;
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

		// Streams [x, y, z, yaw, scale, model, variant] rows from "instances" without allocating large strings or substrings
		internal static void ForEachInstanceFile(string jsonPath, Action<double[]> action)
		{
			var keyBuf = new StringBuilder(32);
			var row = new double[7];
			var numBuf = new char[64];
			int numLen = 0;
			int column = 0;
			int braceDepth = 0;
			int bracketDepth = 0;
			bool inString = false;
			bool escaped = false;
			bool capturingKey = false;
			string lastKey = null;
			bool pendingInstancesColon = false;
			bool inInstancesArray = false;
			int instancesDepth = 0;

			void FlushNumber()
			{
				if (numLen <= 0)
					return;
				if (column < 7 && TryParseDouble(numBuf, numLen, out double parsed))
					row[column] = parsed;
				column++;
				numLen = 0;
			}

			using (var reader = new StreamReader(jsonPath, System.Text.Encoding.UTF8, true, StreamBufferSize))
			{
				var buf = new char[StreamBufferSize];
				int read;
				while ((read = reader.Read(buf, 0, buf.Length)) > 0)
				{
					for (int i = 0; i < read; i++)
					{
						char ch = buf[i];
						if (inInstancesArray)
						{
							if (ch == '[')
							{
								instancesDepth++;
								column = 0;
								numLen = 0;
							}
							else if (ch == ']')
							{
								FlushNumber();
								if (instancesDepth == 2 && column == 7)
									action(row);
								if (--instancesDepth == 0)
									return;
							}
							else if (ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E' || (ch >= '0' && ch <= '9'))
							{
								if (numLen < numBuf.Length)
									numBuf[numLen++] = ch;
							}
							else
							{
								FlushNumber();
							}
							continue;
						}

						if (inString)
						{
							if (escaped)
							{
								escaped = false;
								if (capturingKey && keyBuf.Length < 64) keyBuf.Append(ch);
							}
							else if (ch == '\\')
							{
								escaped = true;
								if (capturingKey && keyBuf.Length < 64) keyBuf.Append(ch);
							}
							else if (ch == '"')
							{
								inString = false;
								if (capturingKey)
								{
									lastKey = keyBuf.ToString();
									capturingKey = false;
								}
							}
							else if (capturingKey && keyBuf.Length < 64)
							{
								keyBuf.Append(ch);
							}
						}
						else
						{
							if (ch == '"')
							{
								inString = true;
								if (braceDepth == 1 && bracketDepth == 0)
								{
									capturingKey = true;
									keyBuf.Clear();
								}
							}
							else if (ch == ':' && braceDepth == 1 && bracketDepth == 0 && lastKey == "instances")
							{
								pendingInstancesColon = true;
							}
							else if (pendingInstancesColon)
							{
								if (ch == '[')
								{
									inInstancesArray = true;
									instancesDepth = 1;
									pendingInstancesColon = false;
									lastKey = null;
								}
								else if (!char.IsWhiteSpace(ch))
								{
									pendingInstancesColon = false;
									lastKey = null;
								}
							}
							else if (ch == '{')
							{
								braceDepth++;
								lastKey = null;
							}
							else if (ch == '}')
							{
								braceDepth--;
								lastKey = null;
							}
							else if (ch == '[')
							{
								bracketDepth++;
							}
							else if (ch == ']')
							{
								bracketDepth--;
							}
							else if (!char.IsWhiteSpace(ch))
							{
								lastKey = null;
							}
						}
					}
				}
			}
		}

		static readonly double[] Pow10Lookup = {
			1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15
		};

		internal static bool TryParseDouble(char[] buf, int len, out double value)
		{
			value = 0.0;
			if (len <= 0)
				return false;
			int idx = 0;
			bool neg = false;
			if (buf[idx] == '-')
			{
				neg = true;
				idx++;
			}
			else if (buf[idx] == '+')
			{
				idx++;
			}
			ulong mantissa = 0;
			int digits = 0;
			int fracDigits = 0;
			bool seenDot = false;
			bool useFallback = false;
			for (; idx < len; idx++)
			{
				char ch = buf[idx];
				if (ch >= '0' && ch <= '9')
				{
					if (digits < 15)
					{
						mantissa = mantissa * 10u + (uint)(ch - '0');
						digits++;
						if (seenDot)
							fracDigits++;
					}
					else if (!seenDot)
					{
						useFallback = true;
						break;
					}
				}
				else if (ch == '.' && !seenDot)
				{
					seenDot = true;
				}
				else
				{
					useFallback = true;
					break;
				}
			}
			if (useFallback)
				return double.TryParse(new string(buf, 0, len), NumberStyles.Float, CultureInfo.InvariantCulture, out value);
			if (digits == 0)
				return false;
			double res = mantissa;
			if (fracDigits > 0)
			{
				res /= fracDigits < Pow10Lookup.Length ? Pow10Lookup[fracDigits] : Math.Pow(10.0, fracDigits);
			}
			value = neg ? -res : res;
			return true;
		}

		// ---- Water ----

		static void ImportWater(Manifest manifest, string packFolder, string assetFolder, Layout l, bool hasSea, Transform parent, ref bool gltfMissing)
		{
			string waterFile = manifest.meshes != null && manifest.meshes.water != null ? manifest.meshes.water.file : null;
			bool validSea = hasSea && manifest.seaLevel != null;
			if (string.IsNullOrEmpty(waterFile) && !validSea)
				return;
			Material material = WaterMaterial(assetFolder + "/Water.mat");
			var water = new GameObject("Water");
			water.transform.SetParent(parent, false);
			bool fromGltf = false;
			if (!string.IsNullOrEmpty(waterFile) && waterFile.EndsWith(".glb", StringComparison.OrdinalIgnoreCase))
			{
				string waterSource = SafePackPath(packFolder, waterFile);
				if (File.Exists(waterSource))
				{
					string assetPath = assetFolder + "/" + Path.GetFileName(waterSource);
					File.Copy(waterSource, DiskPath(assetPath), true);
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
			}
			if (!fromGltf && validSea)
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
			if (!Directory.Exists(from) || (File.GetAttributes(from) & FileAttributes.ReparsePoint) != 0)
				return;
			Directory.CreateDirectory(to);
			foreach (string file in Directory.GetFiles(from))
			{
				if ((File.GetAttributes(file) & FileAttributes.ReparsePoint) != 0)
					continue;
				File.Copy(file, Path.Combine(to, Path.GetFileName(file)), true);
			}
			foreach (string sub in Directory.GetDirectories(from))
			{
				if ((File.GetAttributes(sub) & FileAttributes.ReparsePoint) != 0)
					continue;
				CopyFolder(sub, Path.Combine(to, Path.GetFileName(sub)));
			}
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
