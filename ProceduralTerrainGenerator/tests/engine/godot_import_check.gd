extends SceneTree
## Headless check of the Godot importer on the sample packs (tests/EnginePackTests.cpp), run by tools/check_godot_import.bat:
##   godot --headless --path <project> --script res://godot_import_check.gd -- <pack manifest> copy|build
## copy = pack copied into res://ptg/<name>/ (then the runner imports the project), build = scene built twice, saved, reloaded and checked.

const PtgPack = preload("res://addons/ptg_import/ptg_pack.gd")
const PtgBuilder = preload("res://addons/ptg_import/ptg_builder.gd")

var failures := 0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- <manifest> copy|build")
		quit(2)
		return
	var pack = PtgPack.new()
	if not pack.open(args[0]):
		printerr(pack.error)
		quit(1)
		return
	var target := "res://ptg/%s/" % pack.name
	if args[1] == "copy":
		_check_path_security(pack)
		var result: Error = pack.copy_to(target)
		_expect(result == OK, "copy into %s: %s" % [target, error_string(result)])
	else:
		_check_build(pack, target)
	print("%s %s: %s" % [pack.name, args[1], "PASS" if failures == 0 else "FAIL (%d)" % failures])
	quit(0 if failures == 0 else 1)


func _expect(condition: bool, what: String) -> void:
	if not condition:
		failures += 1
		printerr("  FAILED: " + what)


func _check_path_security(pack) -> void:
	_expect(not PtgPack.is_valid_pack_name("../evil"), "rejects parent traversal pack name")
	_expect(not PtgPack.is_valid_pack_name("a/b"), "rejects slash in pack name")
	_expect(not PtgPack.is_valid_pack_name(""), "rejects empty pack name")
	_expect(pack.safe_pack_path("../secret.txt").is_empty(), "rejects parent traversal pack file path")
	_expect(pack.safe_pack_path("/etc/passwd").is_empty(), "rejects rooted unix pack file path")
	_expect(pack.safe_pack_path("C:/Windows/win.ini").is_empty(), "rejects drive pack file path")
	_expect(not pack.safe_pack_path("vegetation/tree.glb").is_empty(), "accepts valid relative pack path")
	_expect(PtgPack.safe_target_path("res://ptg/full/", "../escape.glb").is_empty(), "rejects parent traversal target path")


func _near(a: float, b: float, tolerance: float) -> bool:
	return absf(a - b) <= tolerance


func _check_build(pack, target: String) -> void:
	var root: Node3D = PtgBuilder.build(pack, target)
	if pack.name == "minimal":
		_expect(root == null, "a pack without the glTF mesh builds nothing")
		_expect(PtgBuilder.last_error.contains("needs the glTF terrain mesh"), "message: " + PtgBuilder.last_error)
		return
	_expect(root != null, "build: " + PtgBuilder.last_error)
	if root == null:
		return
	var first_count := _count(root)
	root.free()
	# Built again: the same scene, saved over the first one
	root = PtgBuilder.build(pack, target)
	_expect(root != null and _count(root) == first_count, "second build gives the same nodes")
	var path: String = target + pack.name + ".scn"
	_expect(PtgBuilder.save(root, path) == OK, "save " + path)
	root.free()
	var packed: PackedScene = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
	_expect(packed != null, "reload " + path)
	if packed == null:
		return
	root = packed.instantiate()
	_check_terrain(pack, root)
	_check_collision(pack, root)
	if pack.name == "full":
		_check_water(pack, root)
		_check_vegetation(pack, root)
		_check_missing_splat(pack, target)
	else:
		_expect(root.get_node_or_null("Vegetation") == null, "no vegetation without model files")
		_expect(Array(PtgBuilder.last_warnings).any(func(w): return w.contains("vegetation models were not exported")),
			"warning about the missing models: %s" % [PtgBuilder.last_warnings])
	root.free()


func _count(node: Node) -> int:
	var n := 1
	for child in node.get_children():
		n += _count(child)
	return n


func _check_terrain(pack, root: Node) -> void:
	var terrain := root.get_node_or_null("Terrain")
	_expect(terrain != null, "Terrain node")
	if terrain == null:
		return
	var levels: Array = pack.manifest["meshes"]["levels"]
	var tiles_per_level := int(levels[0]["tiles"])
	_expect(terrain.get_child_count() == tiles_per_level * levels.size(), "one mesh per tile per LOD (%d)" % terrain.get_child_count())
	var bounds := AABB()
	var first := true
	var begins := {}
	var ends := {}
	for child in terrain.get_children():
		var mesh := child as MeshInstance3D
		_expect(mesh != null and mesh.mesh != null, "tile %s has a mesh" % child.name)
		if mesh == null or mesh.mesh == null:
			continue
		_expect(mesh.material_override is ShaderMaterial, "tile %s has the terrain material" % child.name)
		# The headless renderer keeps no texture data, so the arrays can only be checked with a real one
		if first and DisplayServer.get_name() != "headless" and mesh.material_override is ShaderMaterial:
			var albedo = mesh.material_override.get_shader_parameter("albedo_array")
			var count: int = pack.manifest["materials"]["list"].size()
			_expect(albedo is Texture2DArray and albedo.get_layers() == count, "albedo array with %d layers" % count)
			_expect(albedo is Texture2DArray and albedo.get_width() == 1024, "albedo layers of 1024 texels")
		var lod := int(str(child.name).get_slice("LOD", 1))
		begins[lod] = mesh.visibility_range_begin
		ends[lod] = mesh.visibility_range_end
		var box: AABB = mesh.transform * mesh.mesh.get_aabb()
		bounds = box if first else bounds.merge(box)
		first = false
	var size: Vector2 = pack.size_metres()
	_expect(_near(bounds.size.x, size.x, 0.05) and _near(bounds.size.z, size.y, 0.05), "terrain size %s, expected %s" % [bounds.size, size])
	_expect(_near(bounds.position.x, 0.0, 0.05) and _near(bounds.position.z, 0.0, 0.05), "texel (0, 0) at the origin: %s" % bounds.position)
	for lod in range(1, levels.size()):
		_expect(_near(begins[lod], ends[lod - 1], 0.001) and begins[lod] > 0.0, "LOD %d starts where LOD %d ends" % [lod, lod - 1])
	_expect(begins[0] == 0.0 and ends[levels.size() - 1] == 0.0, "nearest LOD from 0, farthest without end")


func _check_collision(pack, root: Node) -> void:
	var shape_node := root.get_node_or_null("Collision/Shape") as CollisionShape3D
	_expect(shape_node != null, "Collision/Shape node")
	if shape_node == null:
		return
	var shape := shape_node.shape as HeightMapShape3D
	_expect(shape != null, "height map shape")
	if shape == null:
		return
	_expect(shape.map_width == pack.width() and shape.map_depth == pack.height(), "shape %dx%d" % [shape.map_width, shape.map_depth])
	var m: float = pack.metres_per_texel()
	_expect(_near(shape_node.scale.x, m, 1e-4) and _near(shape_node.scale.y, m, 1e-4) and _near(shape_node.scale.z, m, 1e-4),
		"uniform scale %s" % shape_node.scale)
	var size: Vector2 = pack.size_metres()
	_expect(_near(shape_node.position.x, size.x * 0.5, 0.01) and _near(shape_node.position.z, size.y * 0.5, 0.01),
		"shape centred on the map: %s" % shape_node.position)
	# Texel (0, 0) of the shape in metres = the first height of the file
	var heights: PackedFloat32Array = pack.read_heights_metres()
	_expect(_near(shape.map_data[0] * m, heights[0], 0.01), "first height %f m, file %f m" % [shape.map_data[0] * m, heights[0]])


# The pack without its first splat file: a warning names it and the shader is told, so the terrain is not drawn black
func _check_missing_splat(pack, target: String) -> void:
	var copy := ProjectSettings.globalize_path("user://ptg_no_splat")
	DirAccess.make_dir_recursive_absolute(copy)
	var splat: String = str(pack.manifest["materials"]["list"][0]["splat"]["file"])
	for file in DirAccess.get_files_at(pack.folder):
		if file != splat:
			DirAccess.copy_absolute(pack.folder.path_join(file), copy.path_join(file))
	DirAccess.remove_absolute(copy.path_join(splat))
	var damaged = PtgPack.new()
	_expect(damaged.open(copy.path_join(pack.name + ".json")), "open the pack without %s" % splat)
	PtgBuilder.last_warnings = PackedStringArray()
	var material: ShaderMaterial = PtgBuilder._terrain_material(damaged, target)
	_expect(Array(PtgBuilder.last_warnings).any(func(w): return w.contains(splat)), "warning about %s: %s" % [splat, PtgBuilder.last_warnings])
	_expect(material.get_shader_parameter("splat_missing") == true, "shader told the splat is missing")


func _check_water(pack, root: Node) -> void:
	var water := root.get_node_or_null("Water")
	_expect(water != null, "Water node")
	if water == null:
		return
	var sea_found := false
	for child in water.get_children():
		var mesh := child as MeshInstance3D
		if mesh != null and mesh.mesh != null:
			var box: AABB = mesh.transform * mesh.mesh.get_aabb()
			sea_found = sea_found or (_near(box.position.y, pack.sea_metres(), 0.05) and box.size.y < 0.05)
	_expect(sea_found, "a flat water surface at the sea level %f m" % pack.sea_metres())


func _check_vegetation(pack, root: Node) -> void:
	var vegetation := root.get_node_or_null("Vegetation")
	_expect(vegetation != null, "Vegetation node")
	if vegetation == null:
		return
	var lod0 := 0
	for child in vegetation.get_children():
		var multi := child as MultiMeshInstance3D
		_expect(multi != null and multi.multimesh != null, "%s is a MultiMesh" % child.name)
		if multi != null and multi.multimesh != null and str(child.name).contains("_LOD0_"):
			lod0 += multi.multimesh.instance_count
			_expect(multi.visibility_range_begin == 0.0 and multi.visibility_range_end > 0.0, "%s LOD0 range" % child.name)
		# Godot measures a visibility range to the node, not to each plant: a node's square has to be small against its range
		if multi != null and multi.visibility_range_end > 0.0:
			var square: float = multi.get_meta("square_metres", INF)
			_expect(square <= multi.visibility_range_end * 0.5, "%s: square %.0f m for a range ending at %.0f m" % [child.name, square, multi.visibility_range_end])
	_expect(lod0 == int(pack.manifest["vegetation"]["instances"]), "LOD0 instances %d, pack %d" % [lod0, int(pack.manifest["vegetation"]["instances"])])
