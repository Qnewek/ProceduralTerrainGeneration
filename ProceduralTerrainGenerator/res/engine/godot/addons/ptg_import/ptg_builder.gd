@tool
extends RefCounted
## Builds the scene of a Procedural Terrain Generator pack copied into the project (PtgPack.copy_to):
##   Terrain    - the glTF terrain tiles of every LOD, switched by distance, with the material shader mixing the surface materials
##   Collision  - StaticBody3D with a HeightMapShape3D from the R16 height file
##   Water      - the water glTF (sea, rivers, lakes) or a plane at the sea level
##   Vegetation - MultiMeshInstance3D per model, variant, LOD and square of the map, with the app's LOD distances
## The pack axes are Godot's: x = map column, z = map row (metres), texel (0, 0) at the origin.

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
	if levels.is_empty() or not str(levels[0]["file"]).ends_with(".glb"):
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
		var path: String = target + str(levels[n]["file"])
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
static func _material_array(pack, key: String, fill: Callable) -> Texture2DArray:
	var images: Array[Image] = []
	for material in pack.manifest["materials"]["list"]:
		var image: Image = null
		var textures = material.get("textures")
		# A texture file deleted from the pack: the fallback fill, without an engine error per file
		if typeof(textures) == TYPE_DICTIONARY and textures.get(key) != null and FileAccess.file_exists(pack.folder.path_join(str(textures[key]))):
			image = Image.load_from_file(pack.folder.path_join(str(textures[key])))
		if image == null or image.is_empty():
			image = Image.create_empty(4, 4, false, Image.FORMAT_RGBA8)
			image.fill(fill.call(material))
		image.convert(Image.FORMAT_RGBA8)
		image.resize(MATERIAL_LAYER_SIZE, MATERIAL_LAYER_SIZE, Image.INTERPOLATE_LANCZOS)
		image.generate_mipmaps()
		images.append(image)
		if images.size() == MAX_MATERIALS:
			break
	var array := Texture2DArray.new()
	array.create_from_images(images)
	return array


static func _save_resource(resource: Resource, path: String) -> Resource:
	if ResourceSaver.save(resource, path) == OK:
		resource.take_over_path(path)
	return resource


static func _terrain_material(pack, target: String) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = TERRAIN_SHADER
	var list: Array = pack.manifest.get("materials", {}).get("list", [])
	if list.is_empty():
		var colors: Dictionary = pack.file_entry("biome_colors")
		if not colors.is_empty():
			var image := Image.load_from_file(pack.folder.path_join(colors["path"]))
			if image != null and not image.is_empty():
				material.set_shader_parameter("base_color_map", _save_resource(ImageTexture.create_from_image(image), target + "biome_colors.res"))
		return material
	material.set_shader_parameter("use_materials", true)
	material.set_shader_parameter("material_count", mini(list.size(), MAX_MATERIALS))
	# Splat files in material order: materials 4k..4k+3 are in the RGBA file of material 4k
	for k in range(0, mini(list.size(), MAX_MATERIALS), 4):
		var splat_file := str(list[k]["splat"]["file"])
		var image: Image = null
		if FileAccess.file_exists(pack.folder.path_join(splat_file)):
			image = Image.load_from_file(pack.folder.path_join(splat_file))
		if image != null and not image.is_empty():
			material.set_shader_parameter("splat_%d" % (k / 4), _save_resource(ImageTexture.create_from_image(image), target + "splat_%d.res" % (k / 4)))
		else:
			material.set_shader_parameter("splat_missing", true)
			last_warnings.append("%s: %s could not be read, its materials are mixed evenly" % [pack.name, splat_file])
	var tiles := PackedFloat32Array()
	tiles.resize(MAX_MATERIALS)
	for m in mini(list.size(), MAX_MATERIALS):
		var textures = list[m].get("textures")
		tiles[m] = float(textures["tileMetres"]) if typeof(textures) == TYPE_DICTIONARY else 4.0 * pack.metres_per_texel()
	material.set_shader_parameter("tile_metres", tiles)
	var fallback := func(m: Dictionary) -> Color:
		var c: Array = m.get("fallbackColor", [0.5, 0.5, 0.5])
		return Color(float(c[0]), float(c[1]), float(c[2]))
	material.set_shader_parameter("albedo_array", _save_resource(_material_array(pack, "albedo", fallback), target + "terrain_albedo.res"))
	material.set_shader_parameter("normal_array",
		_save_resource(_material_array(pack, "normal", func(_m): return Color(0.5, 0.5, 1.0)), target + "terrain_normal.res"))
	material.set_shader_parameter("roughness_array",
		_save_resource(_material_array(pack, "roughness", func(_m): return Color(0.9, 0.9, 0.9)), target + "terrain_roughness.res"))
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
		var meshes := _meshes_of(target + str(water_mesh["file"]))
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


static func _build_vegetation(pack, target: String) -> Node3D:
	var info: Dictionary = pack.manifest.get("vegetation", {})
	if info.is_empty():
		return null
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(pack.folder.path_join(str(info["file"]))))
	if typeof(parsed) != TYPE_DICTIONARY:
		last_warnings.append("%s: cannot read %s, no vegetation" % [pack.name, info["file"]])
		return null
	var models: Array = parsed["models"]
	var ranges: Dictionary = parsed.get("lodRanges", {})
	# Meshes per model: [variant][lod]; models without a file are skipped
	var model_meshes := []
	var missing := 0
	for model in models:
		var per_variant := []
		if model.has("file") and int(model["count"]) > 0:
			var meshes := _meshes_of(target + str(model["file"]))
			for nodes in model["variantNodes"]:
				var per_lod := []
				for node_name in nodes:
					per_lod.append(meshes.get(str(node_name), {}).get("mesh"))
				per_variant.append(per_lod)
		elif int(model["count"]) > 0:
			missing += 1
		model_meshes.append(per_variant)
	if missing > 0 and missing == models.filter(func(m): return int(m["count"]) > 0).size():
		last_warnings.append("%s: vegetation models were not exported (Vegetation models off), no vegetation" % pack.name)
		return null
	if missing > 0:
		last_warnings.append("%s: %d vegetation models were not exported and are left out" % [pack.name, missing])

	# Every plant once as the 12 floats of its transform (MultiMesh.buffer layout: basis rows with the origin), turned by yaw around +Y
	# (x' = x cos + z sin, z' = z cos - x sin) and scaled
	var rows: Array = parsed["instances"]
	parsed = null
	var count := rows.size()
	var model_of := PackedInt32Array()
	model_of.resize(count)
	var variant_of := PackedInt32Array()
	variant_of.resize(count)
	var data := PackedFloat32Array()
	data.resize(count * 12)
	for i in count:
		var row: Array = rows[i]
		model_of[i] = int(row[5])
		variant_of[i] = int(row[6])
		var s := float(row[4])
		var c := cos(float(row[3])) * s
		var n := sin(float(row[3])) * s
		var o := i * 12
		data[o] = c; data[o + 1] = 0.0; data[o + 2] = n; data[o + 3] = float(row[0])
		data[o + 4] = 0.0; data[o + 5] = s; data[o + 6] = 0.0; data[o + 7] = float(row[1])
		data[o + 8] = -n; data[o + 9] = 0.0; data[o + 10] = c; data[o + 11] = float(row[2])
	rows = []

	# Godot measures a visibility range to the node, not to each plant, so every LOD has its own grid of squares about half as wide as
	# the distance where that LOD ends; on very large maps the squares grow so that a model has at most ~64 x 64 of them per LOD
	var size: Vector2 = pack.size_metres()
	var smallest_square := maxf(size.x, size.y) / 64.0
	var vegetation := Node3D.new()
	vegetation.name = "Vegetation"
	for lod in 3:
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
			if m >= model_meshes.size() or v >= model_meshes[m].size() or lod >= model_meshes[m][v].size() or model_meshes[m][v][lod] == null:
				continue
			var lods: Array = model_meshes[m][v]
			var range: Dictionary = ranges.get(str(models[m].get("layer", "Trees")), { "lod1": 40.0, "lod2": 100.0, "max": 1000.0 })
			var bounds := [0.0, float(range["lod1"]), float(range["lod2"]), float(range["max"])]
			var begin: float = bounds[mini(lod, 3)]
			var end: float = bounds[3] if lod == lods.size() - 1 else bounds[mini(lod + 1, 3)]
			var square := clampf(end * 0.5, maxf(4.0, smallest_square), maxf(512.0, smallest_square))
			var key := Vector4i(m * 4096 + v, 0, int(floorf(data[i * 12 + 3] / square)), int(floorf(data[i * 12 + 11] / square)))
			var g: int = index_of.get(key, -1)
			if g == -1:
				g = keys.size()
				index_of[key] = g
				keys.append(key)
				squares.append(square)
				ends.append(Vector2(begin, end))
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
