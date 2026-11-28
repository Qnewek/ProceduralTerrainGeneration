@tool
extends EditorPlugin
## Project → Tools → Import PTG pack...: choose <name>.json of an export pack of Procedural Terrain Generator; the pack is copied to
## res://ptg/<name>/, imported, and the scene res://ptg/<name>/<name>.scn is built and opened (replaced if it exists).

const PtgPack = preload("ptg_pack.gd")
const PtgBuilder = preload("ptg_builder.gd")
const MENU_ITEM := "Import PTG pack..."

var dialog: EditorFileDialog


func _enter_tree() -> void:
	add_tool_menu_item(MENU_ITEM, _choose_pack)


func _exit_tree() -> void:
	remove_tool_menu_item(MENU_ITEM)
	if dialog != null:
		dialog.queue_free()
		dialog = null


func _choose_pack() -> void:
	if dialog == null:
		dialog = EditorFileDialog.new()
		dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
		dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
		dialog.title = "Choose <name>.json of a PTG export pack"
		dialog.add_filter("*.json", "PTG pack manifest")
		dialog.file_selected.connect(_import_pack)
		EditorInterface.get_base_control().add_child(dialog)
	dialog.popup_file_dialog()


func _import_pack(manifest_path: String) -> void:
	var pack = PtgPack.new()
	if not pack.open(manifest_path):
		_fail(pack.error)
		return
	var target := "res://ptg/%s/" % pack.name
	var result: Error = pack.copy_to(target)
	if result != OK:
		_fail("Cannot copy the pack to %s: %s" % [target, error_string(result)])
		return
	# The glTF files have to be imported before the builder loads them
	var file_system := EditorInterface.get_resource_filesystem()
	file_system.scan()
	while file_system.is_scanning():
		await file_system.filesystem_changed
	await get_tree().process_frame

	var root: Node3D = PtgBuilder.build(pack, target)
	for warning in PtgBuilder.last_warnings:
		push_warning(warning)
	if root == null:
		_fail(PtgBuilder.last_error)
		return
	var path: String = target + pack.name + ".scn"
	result = PtgBuilder.save(root, path)
	root.free()
	if result != OK:
		_fail("Cannot save %s: %s" % [path, error_string(result)])
		return
	file_system.update_file(path)
	if path in EditorInterface.get_open_scenes():
		EditorInterface.reload_scene_from_path(path)
	else:
		EditorInterface.open_scene_from_path(path)
	print("PTG pack imported: %s" % path)


func _fail(message: String) -> void:
	push_error(message)
	var alert := AcceptDialog.new()
	alert.title = "PTG pack import"
	alert.dialog_text = message
	alert.confirmed.connect(alert.queue_free)
	alert.canceled.connect(alert.queue_free)
	EditorInterface.get_base_control().add_child(alert)
	alert.popup_centered()
