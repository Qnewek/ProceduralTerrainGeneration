@tool
extends RefCounted
## Builds the scene of a Procedural Terrain Generator pack copied into the project (PtgPack.copy_to):
##   Terrain    - the glTF terrain tiles of every LOD, switched by distance, with the material shader mixing the surface materials
##   Collision  - StaticBody3D with a HeightMapShape3D from the R16 height file
##   Water      - the water glTF (sea, rivers, lakes) or a plane at the sea level
##   Vegetation - MultiMeshInstance3D per model, variant, LOD and square of the map, with the app's LOD distances
## The pack axes are Godot's: x = map column, z = map row (metres), texel (0, 0) at the origin.

const PtgPack = preload("ptg_pack.gd")
const TERRAIN_SHADER = preload("ptg_terrain.gdshader")
const WATER_SHADER = preload("ptg_water.gdshader")
## A terrain LOD shows from the distance at which its height error is ~1.5 px at 1080p and a 75° field of view
const ERROR_TO_DISTANCE := 500.0
const MATERIAL_LAYER_SIZE := 1024
const MAX_MATERIALS := 16

static var last_error: String = ""
static var last_warnings: PackedStringArray = PackedStringArray()


## @param target - folder of the project the pack was copied to, e.g. "res://ptg/<name>/"; generated resources are saved there too
## @return the root of the scene, or null (last_error says why)
static func build(pack, target: String) -> Node3D:
	last_error = ""
	last_warnings = PackedStringArray()
	var levels: Array = pack.manifest.get("meshes", {}).get("levels", [])
	if levels.is_empty() or typeof(levels[0]) != TYPE_DICTIONARY or not str(levels[0].get("file", "")).ends_with(".glb"):
		last_error = "%s: Godot import needs the glTF terrain mesh — export the pack with Terrain mesh = glTF binary" % pack.name
		return null
	var root := Node3D.new()
	root.name = pack.name
	var terrain := _build_terrain(pack, target, levels)
	if terrain == null:
		root.free()
		return null
	root.add_child(terrain)
	var collision := _build_collision(pack)
	if collision != null:
		root.add_child(collision)
	var water := _build_water(pack, target)
	if water != null:
		root.add_child(water)
	var vegetation := _build_vegetation(pack, target)
	if vegetation != null:
		root.add_child(vegetation)
	return root


## Saves a built scene (every node owned by the root) as a PackedScene
static func save(root: Node, path: String) -> Error:
	_own(root, root)
	var packed := PackedScene.new()
	var result := packed.pack(root)
	if result != OK:
		return result
	return ResourceSaver.save(packed, path)


static func _own(node: Node, owner: Node) -> void:
	for child in node.get_children():
		child.owner = owner
		_own(child, owner)


## Transform of node relative to the root of its (not added) scene
static func _scene_transform(node: Node) -> Transform3D:
	var t := Transform3D.IDENTITY
	var n := node
	while n != null:
		if n is Node3D:
			t = (n as Node3D).transform * t
		n = n.get_parent()
	return t


## Every MeshInstance3D of an imported glTF scene by node name
static func _meshes_of(path: String) -> Dictionary:
	var meshes := {}
	if path.is_empty():
		return meshes
	var scene := ResourceLoader.load(path) as PackedScene
	if scene == null:
		return meshes
	var instance := scene.instantiate()
	for node in instance.find_children("*", "MeshInstance3D", true, false):
		var mesh_instance := node as MeshInstance3D
		if mesh_instance.mesh != null:
			# A copy owned by the new scene, not a sub-resource of the imported file
			meshes[str(node.name)] = { "mesh": mesh_instance.mesh.duplicate(true), "transform": _scene_transform(node) }
	instance.free()
	return meshes


static func _build_terrain(pack, target: String, levels: Array) -> Node3D:
	var terrain := Node3D.new()
	terrain.name = "Terrain"
	var material := _terrain_material(pack, target)
	var begins: Array[float] = []
	for n in levels.size():
		begins.append(0.0 if n == 0 else maxf(float(levels[n]["errorMetres"]) * ERROR_TO_DISTANCE, begins[n - 1] + 1.0))
	for n in levels.size():
		var path := PtgPack.safe_target_path(target, str(levels[n].get("file", "")))
		var meshes := _meshes_of(path)
		if meshes.is_empty():
			last_error = "%s: cannot load %s (was the pack copied and imported?)" % [pack.name, path]
			terrain.free()
			return null
		var begin: float = begins[n]
		var end: float = begins[n + 1] if n + 1 < levels.size() else 0.0
		for tile_name in meshes:
			var entry: Dictionary = meshes[tile_name]
			var tile := MeshInstance3D.new()
			tile.name = tile_name
			tile.mesh = entry["mesh"]
			tile.transform = entry["transform"]
			tile.material_override = material
			tile.visibility_range_begin = begin
			tile.visibility_range_end = end
			tile.visibility_range_begin_margin = begin * 0.1
			tile.visibility_range_end_margin = end * 0.1
			terrain.add_child(tile)
	return terrain


## A 2D texture array of one image per material (MATERIAL_LAYER_SIZE², mipmapped); a missing image = a flat fill
static func _material_array(pack, key: String, fill: Callable, format: Image.Format = Image.FORMAT_RGBA8,
		compress_source: Image.CompressSource = Image.COMPRESS_SOURCE_GENERIC) -> Texture2DArray:
	var can_compress := format == Image.FORMAT_RGBA8 and DisplayServer.get_name() != "headless"
	var images: Array[Image] = []
	for material in pack.manifest["materials"]["list"]:
		var image: Image = null
		var textures = material.get("textures")
		# A texture file deleted from the pack: the fallback fill, without an engine error per file
		if typeof(textures) == TYPE_DICTIONARY and textures.get(key) != null:
			var img_path: String = pack.safe_pack_path(str(textures[key]))
			if not img_path.is_empty() and FileAccess.file_exists(img_path):
				image = Image.load_from_file(img_path)
		if image == null or image.is_empty():
			image = Image.create_empty(4, 4, false, format)
			image.fill(fill.call(material))
		image.convert(format)
		image.resize(MATERIAL_LAYER_SIZE, MATERIAL_LAYER_SIZE, Image.INTERPOLATE_LANCZOS)
		image.generate_mipmaps()
		if can_compress:
			image.compress(Image.COMPRESS_BPTC, compress_source)
		images.append(image)
		if images.size() == MAX_MATERIALS:
			break
	var array := Texture2DArray.new()
	array.create_from_images(images)
	return array


static func _save_resource(resource: Resource, path: String) -> Resource:
	if not path.is_empty() and ResourceSaver.save(resource, path) == OK:
		resource.take_over_path(path)
	return resource


static func _channel_value(c: Color, channel: int) -> float:
	match channel:
		0: return c.r
		1: return c.g
		2: return c.b
		_: return c.a


static func _terrain_material(pack, target: String) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = TERRAIN_SHADER
	var list: Array = pack.manifest.get("materials", {}).get("list", [])
	if list.is_empty():
		var colors: Dictionary = pack.file_entry("biome_colors")
		if not colors.is_empty():
			var colors_path: String = pack.safe_pack_path(str(colors.get("path", "")))
			if not colors_path.is_empty():
				var image := Image.load_from_file(colors_path)
				if image != null and not image.is_empty():
					material.set_shader_parameter("base_color_map", _save_resource(ImageTexture.create_from_image(image), target + "biome_colors.res"))
		return material
	material.set_shader_parameter("use_materials", true)
	var mat_count := mini(list.size(), MAX_MATERIALS)
	material.set_shader_parameter("material_count", mat_count)

	# Cache loaded splat images by file path and respect each material's splat.file + splat.channel
	var splat_cache := {}
	var load_splat := func(splat_file: String) -> Image:
		if splat_file.is_empty():
			return null
		if splat_cache.has(splat_file):
			return splat_cache[splat_file]
		var full_path: String = pack.safe_pack_path(splat_file)
		var img: Image = null
		if not full_path.is_empty() and FileAccess.file_exists(full_path):
			img = Image.load_from_file(full_path)
			if img != null and not img.is_empty() and img.get_format() != Image.FORMAT_RGBA8:
				img.convert(Image.FORMAT_RGBA8)
		splat_cache[splat_file] = img
		if img == null or img.is_empty():
			last_warnings.append("%s: %s could not be read, its materials are mixed evenly" % [pack.name, splat_file])
		return img

	for k in range(0, mat_count, 4):
		var group_end := mini(k + 4, mat_count)
		var first_file := ""
		var first_splat = list[k].get("splat")
		if typeof(first_splat) == TYPE_DICTIONARY:
			first_file = str(first_splat.get("file", ""))
		var fast_path := not first_file.is_empty()
		for m in range(k, group_end):
			var sp = list[m].get("splat")
			var f := str(sp.get("file", "")) if typeof(sp) == TYPE_DICTIONARY else ""
			var ch := str(sp.get("channel", "")) if typeof(sp) == TYPE_DICTIONARY else ""
			if f != first_file or ch != "RGBA"[m - k]:
				fast_path = false
				break

		var group_img: Image = null
		if fast_path:
			group_img = load_splat.call(first_file)
			if group_img == null or group_img.is_empty():
				material.set_shader_parameter("splat_missing", true)
		else:
			var src_imgs: Array[Image] = [null, null, null, null]
			var src_chs: PackedInt32Array = PackedInt32Array([0, 0, 0, 0])
			var w := 0
			var h := 0
			var missing_in_group := false
			for m in range(k, group_end):
				var sp = list[m].get("splat")
				var f := str(sp.get("file", "")) if typeof(sp) == TYPE_DICTIONARY else ""
				var ch_str := str(sp.get("channel", "R")) if typeof(sp) == TYPE_DICTIONARY else "R"
				var ch_idx := maxi(0, "RGBA".find(ch_str.substr(0, 1)))
				var img: Image = load_splat.call(f)
				if img == null or img.is_empty():
					if not f.is_empty():
						missing_in_group = true
				else:
					src_imgs[m - k] = img
					src_chs[m - k] = ch_idx
					if w == 0:
						w = img.get_width()
						h = img.get_height()
			if missing_in_group or w == 0 or h == 0:
				material.set_shader_parameter("splat_missing", true)
			if w > 0 and h > 0:
				group_img = Image.create_empty(w, h, false, Image.FORMAT_RGBA8)
				for y in h:
					for x in w:
						var vals := [0.0, 0.0, 0.0, 0.0]
						for s in range(group_end - k):
							var si: Image = src_imgs[s]
							if si != null and x < si.get_width() and y < si.get_height():
								vals[s] = _channel_value(si.get_pixel(x, y), src_chs[s])
						group_img.set_pixel(x, y, Color(vals[0], vals[1], vals[2], vals[3]))

		if group_img != null and not group_img.is_empty():
			material.set_shader_parameter("splat_%d" % (k / 4), _save_resource(ImageTexture.create_from_image(group_img), target + "splat_%d.res" % (k / 4)))

	var tiles := PackedFloat32Array()
	tiles.resize(MAX_MATERIALS)
	for m in mat_count:
		var textures = list[m].get("textures")
		tiles[m] = float(textures["tileMetres"]) if typeof(textures) == TYPE_DICTIONARY and float(textures.get("tileMetres", 0.0)) > 0.0 else 4.0 * pack.metres_per_texel()
	material.set_shader_parameter("tile_metres", tiles)
	var fallback := func(m: Dictionary) -> Color:
		var c: Array = m.get("fallbackColor", [0.5, 0.5, 0.5])
		if c.size() < 3:
			return Color(0.5, 0.5, 0.5).linear_to_srgb()
		return Color(float(c[0]), float(c[1]), float(c[2])).linear_to_srgb()
	material.set_shader_parameter("albedo_array",
		_save_resource(_material_array(pack, "albedo", fallback, Image.FORMAT_RGBA8, Image.COMPRESS_SOURCE_SRGB), target + "terrain_albedo.res"))
	material.set_shader_parameter("normal_array",
		_save_resource(_material_array(pack, "normal", func(_m): return Color(0.5, 0.5, 1.0), Image.FORMAT_RGBA8, Image.COMPRESS_SOURCE_NORMAL), target + "terrain_normal.res"))
	material.set_shader_parameter("roughness_array",
		_save_resource(_material_array(pack, "roughness", func(_m): return Color(0.9, 0.9, 0.9), Image.FORMAT_R8, Image.COMPRESS_SOURCE_GENERIC), target + "terrain_roughness.res"))
	return material


static func _build_collision(pack) -> StaticBody3D:
	var heights: PackedFloat32Array = pack.read_heights_metres()
	if heights.is_empty():
		last_warnings.append("%s: no R16 height file, no collision (export with Engine import scripts on)" % pack.name)
		return null
	var m: float = pack.metres_per_texel()
	# The shape spaces its samples 1 unit apart and is centred on its node: heights in texels, the node scaled uniformly by the texel size
	var data := PackedFloat32Array()
	data.resize(heights.size())
	for i in heights.size():
		data[i] = heights[i] / m
	var shape := HeightMapShape3D.new()
	shape.map_width = pack.width()
	shape.map_depth = pack.height()
	shape.map_data = data
	var body := StaticBody3D.new()
	body.name = "Collision"
	var node := CollisionShape3D.new()
	node.name = "Shape"
	node.shape = shape
	node.scale = Vector3.ONE * m
	var size: Vector2 = pack.size_metres()
	node.position = Vector3(size.x * 0.5, 0.0, size.y * 0.5)
	body.add_child(node)
	return body


static func _build_water(pack, target: String) -> Node3D:
	var water_mesh: Dictionary = pack.manifest.get("meshes", {}).get("water", {})
	if water_mesh.is_empty() and not pack.has_sea():
		return null
	var material := ShaderMaterial.new()
	material.shader = WATER_SHADER
	var water := Node3D.new()
	water.name = "Water"
	if not water_mesh.is_empty():
		var water_path := PtgPack.safe_target_path(target, str(water_mesh.get("file", "")))
		var meshes := _meshes_of(water_path)
		for node_name in meshes:
			var surface := MeshInstance3D.new()
			surface.name = node_name
			surface.mesh = meshes[node_name]["mesh"]
			surface.transform = meshes[node_name]["transform"]
			surface.material_override = material
			water.add_child(surface)
	if water.get_child_count() == 0 and pack.has_sea():
		var size: Vector2 = pack.size_metres()
		var plane := PlaneMesh.new()
		plane.size = size
		var sea := MeshInstance3D.new()
		sea.name = "sea"
		sea.mesh = plane
		sea.position = Vector3(size.x * 0.5, pack.sea_metres(), size.y * 0.5)
		sea.material_override = material
		water.add_child(sea)
	return water


static func _lod_distance_bounds(lod: int, lod_count: int, mr: Array) -> Vector2:
	var lod1: float = mr[0]
	var lod2: float = mr[1]
	var max_d: float = mr[2]
	var edge := func(k: int) -> float:
		if k <= 0:
			return 0.0
		if k >= lod_count:
			return max_d
		if k == 1:
			return lod1
		if k == 2:
			return lod2
		return lerpf(lod2, max_d, float(k - 2) / float(maxi(lod_count - 2, 1)))
	return Vector2(edge.call(lod), edge.call(lod + 1))


static func _build_vegetation(pack, target: String) -> Node3D:
	var info: Dictionary = pack.manifest.get("vegetation", {})
	if info.is_empty():
		return null
	var veg_path: String = pack.safe_pack_path(str(info.get("file", "")))
	if veg_path.is_empty():
		last_warnings.append("%s: invalid vegetation path %s, no vegetation" % [pack.name, info.get("file", "")])
		return null
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(veg_path))
	if typeof(parsed) != TYPE_DICTIONARY or typeof(parsed.get("models")) != TYPE_ARRAY or typeof(parsed.get("instances")) != TYPE_ARRAY:
		last_warnings.append("%s: cannot read %s, no vegetation" % [pack.name, info["file"]])
		return null
	var models: Array = parsed["models"]
	var ranges: Dictionary = parsed.get("lodRanges", {}) if typeof(parsed.get("lodRanges")) == TYPE_DICTIONARY else {}
	# Meshes per model: [variant][lod]; models without a file are skipped
	var model_meshes := []
	var max_lods := 0
	var missing := 0
	for model in models:
		var per_variant := []
		if typeof(model) == TYPE_DICTIONARY and model.has("file") and int(model.get("count", 0)) > 0:
			var model_path := PtgPack.safe_target_path(target, str(model.get("file", "")))
			var meshes := _meshes_of(model_path)
			for nodes in model.get("variantNodes", []):
				var per_lod := []
				for node_name in nodes:
					per_lod.append(meshes.get(str(node_name), {}).get("mesh"))
				max_lods = maxi(max_lods, per_lod.size())
				per_variant.append(per_lod)
		elif typeof(model) == TYPE_DICTIONARY and int(model.get("count", 0)) > 0:
			missing += 1
		model_meshes.append(per_variant)
	if missing > 0 and missing == models.filter(func(m): return typeof(m) == TYPE_DICTIONARY and int(m.get("count", 0)) > 0).size():
		last_warnings.append("%s: vegetation models were not exported (Vegetation models off), no vegetation" % pack.name)
		return null
	if missing > 0:
		last_warnings.append("%s: %d vegetation models were not exported and are left out" % [pack.name, missing])
	if max_lods == 0:
		return null

	# Precompute LOD distance bounds once per model instead of inside the per-instance loop
	var model_ranges := []
	for model in models:
		var layer_name := str(model.get("layer", "Trees")) if typeof(model) == TYPE_DICTIONARY else "Trees"
		var rng = ranges.get(layer_name, {})
		if typeof(rng) != TYPE_DICTIONARY:
			rng = {}
		model_ranges.append([float(rng.get("lod1", 40.0)), float(rng.get("lod2", 100.0)), float(rng.get("max", 1000.0))])

	# Every plant once as the 12 floats of its transform (MultiMesh.buffer layout: basis rows with the origin), turned by yaw around +Y
	# (x' = x cos + z sin, z' = z cos - x sin) and scaled
	var rows: Array = parsed["instances"]
	parsed = null
	var raw_count := rows.size()
	var model_of := PackedInt32Array()
	model_of.resize(raw_count)
	var variant_of := PackedInt32Array()
	variant_of.resize(raw_count)
	var data := PackedFloat32Array()
	data.resize(raw_count * 12)
	var count := 0
	for i in raw_count:
		if typeof(rows[i]) != TYPE_ARRAY:
			continue
		var row: Array = rows[i]
		if row.size() < 7:
			continue
		model_of[count] = int(row[5])
		variant_of[count] = int(row[6])
		var s := float(row[4])
		var c := cos(float(row[3])) * s
		var n := sin(float(row[3])) * s
		var o := count * 12
		data[o] = c; data[o + 1] = 0.0; data[o + 2] = n; data[o + 3] = float(row[0])
		data[o + 4] = 0.0; data[o + 5] = s; data[o + 6] = 0.0; data[o + 7] = float(row[1])
		data[o + 8] = -n; data[o + 9] = 0.0; data[o + 10] = c; data[o + 11] = float(row[2])
		count += 1
	rows = []

	# Godot measures a visibility range to the node, not to each plant, so every LOD has its own grid of squares about half as wide as
	# the distance where that LOD ends; on very large maps the squares grow so that a model has at most ~64 x 64 of them per LOD
	var size: Vector2 = pack.size_metres()
	var smallest_square := maxf(size.x, size.y) / 64.0
	var vegetation := Node3D.new()
	vegetation.name = "Vegetation"
	for lod in max_lods:
		var group_of := PackedInt32Array()
		group_of.resize(count)
		var index_of := {}
		var keys: Array[Vector4i] = []
		var squares: Array[float] = []
		var ends: Array[Vector2] = []	# visibility range begin, end
		for i in count:
			var m := model_of[i]
			var v := variant_of[i]
			group_of[i] = -1
			if m < 0 or v < 0 or m >= model_meshes.size() or v >= model_meshes[m].size() or lod >= model_meshes[m][v].size() or model_meshes[m][v][lod] == null:
				continue
			var bounds: Vector2 = _lod_distance_bounds(lod, model_meshes[m][v].size(), model_ranges[m])
			var square := clampf(bounds.y * 0.5, maxf(4.0, smallest_square), maxf(512.0, smallest_square))
			var key := Vector4i(m * 4096 + v, 0, int(floorf(data[i * 12 + 3] / square)), int(floorf(data[i * 12 + 11] / square)))
			var g: int = index_of.get(key, -1)
			if g == -1:
				g = keys.size()
				index_of[key] = g
				keys.append(key)
				squares.append(square)
				ends.append(bounds)
			group_of[i] = g
		if keys.is_empty():
			continue
		# Plants sorted by group: counts, offsets, then one buffer cut into the groups
		var sizes := PackedInt32Array()
		sizes.resize(keys.size())
		for i in count:
			if group_of[i] >= 0:
				sizes[group_of[i]] += 1
		var offsets := PackedInt32Array()
		offsets.resize(keys.size() + 1)
		for g in keys.size():
			offsets[g + 1] = offsets[g] + sizes[g]
		var cursor := offsets.duplicate()
		var ordered := PackedFloat32Array()
		ordered.resize(offsets[keys.size()] * 12)
		for i in count:
			var g := group_of[i]
			if g < 0:
				continue
			var to := cursor[g] * 12
			var from := i * 12
			for k in 12:
				ordered[to + k] = data[from + k]
			cursor[g] += 1
		for g in keys.size():
			var key: Vector4i = keys[g]
			var m := key.x / 4096
			var v := key.x % 4096
			var multimesh := MultiMesh.new()
			multimesh.transform_format = MultiMesh.TRANSFORM_3D
			multimesh.mesh = model_meshes[m][v][lod]
			multimesh.instance_count = sizes[g]
			multimesh.buffer = ordered.slice(offsets[g] * 12, offsets[g + 1] * 12)
			var node := MultiMeshInstance3D.new()
			node.name = "%s_%d_LOD%d_%d_%d" % [str(models[m]["name"]).validate_node_name(), v, lod, key.z, key.w]
			node.multimesh = multimesh
			node.visibility_range_begin = ends[g].x
			node.visibility_range_end = ends[g].y
			node.visibility_range_begin_margin = ends[g].x * 0.1
			node.visibility_range_end_margin = ends[g].y * 0.1
			node.set_meta("square_metres", squares[g])
			vegetation.add_child(node)
	return vegetation
