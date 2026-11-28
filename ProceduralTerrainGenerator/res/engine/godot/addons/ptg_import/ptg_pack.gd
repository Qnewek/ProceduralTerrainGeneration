@tool
extends RefCounted
## An export pack of Procedural Terrain Generator: its manifest (<name>.json) and the files next to it.
## Positions in the pack are metres, right-handed, Y up: x = map column, z = map row, the same axes as Godot.

var folder: String = ""         ## absolute folder of the pack (holds <name>.json)
var manifest: Dictionary = {}
var name: String = ""
var error: String = ""          ## why open() failed


static func is_valid_pack_name(pack_name: String) -> bool:
	if pack_name.is_empty():
		return false
	var re := RegEx.new()
	re.compile("^[A-Za-z0-9_-]+$")
	return re.search(pack_name) != null


func safe_pack_path(relative: String) -> String:
	if folder.is_empty() or relative.is_empty() or relative.begins_with("/") or relative.begins_with("\\") or relative.contains(":"):
		return ""
	for seg in relative.replace("\\", "/").split("/"):
		if seg == "..":
			return ""
	var root := folder.replace("\\", "/").simplify_path().trim_suffix("/")
	var joined := folder.path_join(relative).replace("\\", "/").simplify_path()
	if not joined.begins_with(root + "/"):
		return ""
	return joined


static func safe_target_path(target: String, relative: String) -> String:
	if target.is_empty() or relative.is_empty() or relative.begins_with("/") or relative.begins_with("\\") or relative.contains(":"):
		return ""
	for seg in relative.replace("\\", "/").split("/"):
		if seg == "..":
			return ""
	var base := target.replace("\\", "/").trim_suffix("/")
	var prefix := "res://" if base.begins_with("res://") else ""
	var base_tail := (base.substr(6) if not prefix.is_empty() else base).simplify_path().trim_suffix("/")
	var combined_tail := (base_tail + "/" + relative.replace("\\", "/")).simplify_path()
	if not combined_tail.begins_with(base_tail + "/"):
		return ""
	return prefix + combined_tail


## Reads <name>.json; check error when it returns false
func open(manifest_path: String) -> bool:
	var text := FileAccess.get_file_as_string(manifest_path)
	if text.is_empty():
		error = "Cannot read %s" % manifest_path
		return false
	var parsed = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY or parsed.get("generator", "") != "Procedural Terrain Generator":
		error = "%s is not a manifest of a Procedural Terrain Generator pack" % manifest_path
		return false
	var grid = parsed.get("grid")
	if typeof(grid) != TYPE_DICTIONARY or int(grid.get("width", 0)) < 2 or int(grid.get("height", 0)) < 2 or float(grid.get("metresPerTexel", 0.0)) <= 0.0:
		error = "%s has an invalid terrain grid" % manifest_path
		return false
	var pack_name := str(parsed.get("name", manifest_path.get_file().get_basename()))
	if not is_valid_pack_name(pack_name):
		error = "Invalid pack name '%s' (allowed: letters, digits, '_' and '-')" % pack_name
		return false
	manifest = parsed
	name = pack_name
	folder = manifest_path.get_base_dir()
	return true


func width() -> int:
	return int(manifest["grid"]["width"])


func height() -> int:
	return int(manifest["grid"]["height"])


func metres_per_texel() -> float:
	return float(manifest["grid"]["metresPerTexel"])


## Size of the map in metres between the first and the last texel centre (x, z)
func size_metres() -> Vector2:
	return Vector2((width() - 1) * metres_per_texel(), (height() - 1) * metres_per_texel())


func has_sea() -> bool:
	return manifest.has("seaLevel") and typeof(manifest.get("seaLevel")) == TYPE_DICTIONARY


func sea_metres() -> float:
	return float(manifest["seaLevel"]["metres"]) if has_sea() else 0.0


## Entry of the file list with this content (and format, if given); empty if none
func file_entry(content: String, format: String = "") -> Dictionary:
	for f in manifest.get("files", []):
		if typeof(f) == TYPE_DICTIONARY and f.get("content", "") == content and (format.is_empty() or f.get("format", "") == format):
			return f
	return {}


## Heights of the main map in metres, row after row (row 0 first), from its R16 file; empty if there is none
func read_heights_metres() -> PackedFloat32Array:
	var entry := file_entry("height", "r16")
	if entry.is_empty():
		return PackedFloat32Array()
	var path := safe_pack_path(str(entry.get("path", "")))
	if path.is_empty():
		return PackedFloat32Array()
	var bytes := FileAccess.get_file_as_bytes(path)
	var count := width() * height()
	if count <= 0 or bytes.size() < count * 2:
		return PackedFloat32Array()
	var encoding: Dictionary = entry["encoding"]
	var low := float(encoding["metresMin"])
	var span := float(encoding["metresMax"]) - low
	var heights := PackedFloat32Array()
	heights.resize(count)
	for i in count:
		heights[i] = low + float(bytes.decode_u16(i * 2)) / 65535.0 * span
	return heights


## Copies what Godot has to import into a folder of the project, e.g. res://ptg/<name>/: the glTF files, the textures they use
## (vegetation/, biome colors) and the manifest. Height, splat and material files are read from the pack folder by the builder,
## so they are not imported (Godot would compress them)
func copy_to(target: String) -> Error:
	if not target.begins_with("res://ptg/") or target.contains(".."):
		return ERR_INVALID_PARAMETER
	var ptg_root := ProjectSettings.globalize_path("res://ptg/").replace("\\", "/").simplify_path().trim_suffix("/")
	var absolute := ProjectSettings.globalize_path(target).replace("\\", "/").simplify_path().trim_suffix("/")
	if not absolute.begins_with(ptg_root + "/"):
		return ERR_INVALID_PARAMETER
	if DirAccess.dir_exists_absolute(absolute):
		var clean_err := _clear_dir(absolute)
		if clean_err != OK:
			return clean_err
	var result := DirAccess.make_dir_recursive_absolute(absolute)
	if result != OK:
		return result
	var skipped := {}
	for f in manifest.get("files", []):
		if typeof(f) != TYPE_DICTIONARY:
			continue
		if f.get("content", "") in ["height", "splat", "weight", "biome_ids", "engine_script"] or str(f.get("content", "")).begins_with("material_") \
				or str(f.get("content", "")).begins_with("height_"):
			skipped[str(f.get("path", ""))] = true
	return _copy_folder(folder, absolute, "", skipped)


static func _clear_dir(path: String) -> Error:
	var dir := DirAccess.open(path)
	if dir == null:
		return DirAccess.get_open_error()
	for sub in dir.get_directories():
		var err := _clear_dir(path.path_join(sub))
		if err != OK:
			return err
		err = DirAccess.remove_absolute(path.path_join(sub))
		if err != OK:
			return err
	for file in dir.get_files():
		var err := DirAccess.remove_absolute(path.path_join(file))
		if err != OK:
			return err
	return OK


func _copy_folder(from: String, to: String, relative: String, skipped: Dictionary) -> Error:
	var dir := DirAccess.open(from)
	if dir == null:
		return DirAccess.get_open_error()
	DirAccess.make_dir_recursive_absolute(to)
	for sub in dir.get_directories():
		if relative.is_empty() and (sub == "engine" or sub == "materials"):
			continue
		if dir.is_link(sub) if dir.has_method("is_link") else false:
			continue
		var sub_rel := relative + sub
		if safe_pack_path(sub_rel).is_empty():
			return ERR_INVALID_DATA
		var result := _copy_folder(from.path_join(sub), to.path_join(sub), sub_rel + "/", skipped)
		if result != OK:
			return result
	for file in dir.get_files():
		var file_rel := relative + file
		if skipped.has(file_rel):
			continue
		var src := safe_pack_path(file_rel)
		if src.is_empty():
			return ERR_INVALID_DATA
		var result := DirAccess.copy_absolute(src, to.path_join(file))
		if result != OK:
			return result
	return OK
