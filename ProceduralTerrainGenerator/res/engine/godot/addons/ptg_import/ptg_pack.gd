@tool
extends RefCounted
## An export pack of Procedural Terrain Generator: its manifest (<name>.json) and the files next to it.
## Positions in the pack are metres, right-handed, Y up: x = map column, z = map row, the same axes as Godot.

var folder: String = ""         ## absolute folder of the pack (holds <name>.json)
var manifest: Dictionary = {}
var name: String = ""
var error: String = ""          ## why open() failed


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
	manifest = parsed
	name = str(manifest.get("name", manifest_path.get_file().get_basename()))
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
	return manifest.has("seaLevel")


func sea_metres() -> float:
	return float(manifest["seaLevel"]["metres"]) if has_sea() else 0.0


## Entry of the file list with this content (and format, if given); empty if none
func file_entry(content: String, format: String = "") -> Dictionary:
	for f in manifest.get("files", []):
		if f.get("content", "") == content and (format.is_empty() or f.get("format", "") == format):
			return f
	return {}


## Heights of the main map in metres, row after row (row 0 first), from its R16 file; empty if there is none
func read_heights_metres() -> PackedFloat32Array:
	var entry := file_entry("height", "r16")
	if entry.is_empty():
		return PackedFloat32Array()
	var bytes := FileAccess.get_file_as_bytes(folder.path_join(entry["path"]))
	var count := width() * height()
	if bytes.size() < count * 2:
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
	var absolute := ProjectSettings.globalize_path(target)
	var result := DirAccess.make_dir_recursive_absolute(absolute)
	if result != OK:
		return result
	var skipped := {}
	for f in manifest.get("files", []):
		if f.get("content", "") in ["height", "splat", "weight", "biome_ids", "engine_script"] or str(f.get("content", "")).begins_with("material_") \
				or str(f.get("content", "")).begins_with("height_"):
			skipped[str(f["path"])] = true
	return _copy_folder(folder, absolute, "", skipped)


func _copy_folder(from: String, to: String, relative: String, skipped: Dictionary) -> Error:
	var dir := DirAccess.open(from)
	if dir == null:
		return DirAccess.get_open_error()
	DirAccess.make_dir_recursive_absolute(to)
	for sub in dir.get_directories():
		if relative.is_empty() and (sub == "engine" or sub == "materials"):
			continue
		var result := _copy_folder(from.path_join(sub), to.path_join(sub), relative + sub + "/", skipped)
		if result != OK:
			return result
	for file in dir.get_files():
		if skipped.has(relative + file):
			continue
		var result := DirAccess.copy_absolute(from.path_join(file), to.path_join(file))
		if result != OK:
			return result
	return OK
