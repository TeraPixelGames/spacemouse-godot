@tool
extends EditorPlugin

const MENU_ENABLE := 0
const MENU_DEBUG_OVERLAY := 1
const MENU_PRINT_REPORT := 2
const MENU_LOG_STATE := 3

var device: Object
var enabled := true
var show_debug := true

var trans_scale := 5
var rot_scale := 0.01
var damping := 0.8

var _vel_t := Vector3.ZERO
var _vel_r := Vector3.ZERO

var _menu := PopupMenu.new()
var _debug_panel: PanelContainer
var _debug_label: RichTextLabel
var _update_timer: Timer
var _last_update_ms := 0
const _GDEXT_PATH := "res://spacemouse_native/spacemouse_native.gdextension"

func _enter_tree() -> void:
	_load_native_extension()
	if not ClassDB.class_exists("SpaceMouseDevice"):
		push_warning("SpaceMouse native library not found. Build spacemouse_native and ensure the .gdextension is reachable.")
		return

	device = SpaceMouseDevice.new()
	if device.open_first() == false:
		push_warning("SpaceMouse not detected. Connect the device and click 'SpaceMouse: Enabled' toggle to retry.")
	device.enable_raw_logging(show_debug)

	_setup_menu()
	_setup_overlay()
	_start_updater()


func _exit_tree() -> void:
	set_process(false)
	if _menu:
		remove_tool_menu_item("SpaceMouse")
		_menu = null
	if is_instance_valid(_debug_panel):
		remove_control_from_bottom_panel(_debug_panel)
		if is_instance_valid(_update_timer):
			_update_timer.queue_free()
		_debug_panel.queue_free()
		_debug_panel = null
		_debug_label = null
		_update_timer = null
	if device:
		device.close()
		device = null


func _setup_menu() -> void:
	_menu.name = "SpaceMouseMenu"
	_menu.id_pressed.connect(_on_menu_id)
	_menu.add_check_item("SpaceMouse: Enabled", MENU_ENABLE)
	_menu.set_item_checked(_menu.get_item_index(MENU_ENABLE), enabled)
	_menu.add_check_item("SpaceMouse: Show Debug Overlay", MENU_DEBUG_OVERLAY)
	_menu.set_item_checked(_menu.get_item_index(MENU_DEBUG_OVERLAY), show_debug)
	_menu.add_separator()
	_menu.add_item("Print last report (hex)", MENU_PRINT_REPORT)
	_menu.add_item("Log current state", MENU_LOG_STATE)
	add_tool_submenu_item("SpaceMouse", _menu)


func _setup_overlay() -> void:
	if _debug_panel:
		return
	_debug_panel = PanelContainer.new()
	_debug_panel.name = "SpaceMouseDebugPanel"
	_debug_panel.custom_minimum_size = Vector2(320, 160)
	var vb := VBoxContainer.new()
	vb.anchor_right = 1.0
	vb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vb.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var title := Label.new()
	title.text = "SpaceMouse Debug"
	title.add_theme_color_override("font_color", Color(0.95, 0.95, 0.95, 0.95))

	_debug_label = RichTextLabel.new()
	_debug_label.scroll_active = true
	_debug_label.selection_enabled = true
	_debug_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_debug_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	vb.add_child(title)
	vb.add_child(_debug_label)
	_debug_panel.add_child(vb)

	add_control_to_bottom_panel(_debug_panel, "SpaceMouse")
	_debug_panel.visible = show_debug


func _start_updater() -> void:
	_last_update_ms = Time.get_ticks_msec()
	_update_timer = Timer.new()
	_update_timer.name = "SpaceMouseUpdateTimer"
	_update_timer.one_shot = false
	_update_timer.wait_time = 0.03
	_update_timer.autostart = true
	_update_timer.process_mode = Node.PROCESS_MODE_ALWAYS
	_update_timer.timeout.connect(_on_update_timer)
	_debug_panel.add_child(_update_timer)


func _on_update_timer() -> void:
	var now := Time.get_ticks_msec()
	var delta := float(now - _last_update_ms) / 1000.0
	_last_update_ms = now
	_update_state(delta)


func _update_state(delta: float) -> void:
	if device == null:
		return

	if not enabled:
		if _debug_label and show_debug:
			_debug_label.text = "SpaceMouse disabled"
		return

	var state: Dictionary = device.get_state()
	if not state.get("connected", false):
		if _debug_label and show_debug:
			_debug_label.text = "SpaceMouse not connected"
		return

	var t: Vector3 = state.get("t", Vector3.ZERO)
	var r: Vector3 = state.get("r", Vector3.ZERO)

	_vel_t = _vel_t * damping + t * trans_scale * (1.0 - damping)
	_vel_r = _vel_r * damping + r * rot_scale * (1.0 - damping)

	var cam := _get_editor_camera()
	if cam:
		_apply_to_camera(cam, delta)

	if _debug_label and show_debug:
		var btns: PackedInt32Array = state.get("buttons", PackedInt32Array())
		var report_id := state.get("report_id", -1)
		var seen := state.get("seen_reports", {})
		var usage_page := state.get("usage_page", 0)
		var usage := state.get("usage", 0)
		var path := state.get("path", "")
		var last_read := state.get("last_read_result", 0)
		var last_err := state.get("last_error", "")
		var read_count := state.get("read_count", 0)
		var error_count := state.get("error_count", 0)
		var loop_count := state.get("loop_count", 0)
		var last_tick_ms := state.get("last_tick_ms", 0)
		var connected := state.get("connected", false)
		var cam_path := cam.get_path() if cam else ""
		var cam_pos := cam.global_position if cam else Vector3.ZERO
		var thread_alive := state.get("thread_alive", false)
		_debug_label.text = "SpaceMouse\nRaw T: %s\nRaw R: %s\nVel T: %s\nVel R: %s\nButtons: %s\nCamera: %s @ %s\nUsage page: 0x%X\nUsage: 0x%X\nPath: %s\nConnected: %s\nThread alive: %s\nLoop count: %s\nLast tick ms: %s\nLast read: %s (errors: %s, reads: %s)\nLast error: %s\nLast report id: %s\nSeen ids: %s\nLast report: %s" % [
			t, r, _vel_t, _vel_r, btns, cam_path, cam_pos, usage_page, usage, path, connected, thread_alive, loop_count, last_tick_ms, last_read, error_count, read_count, last_err, report_id, seen, device.get_last_report_hex()
		]
	if _debug_panel:
		_debug_panel.visible = show_debug


func _apply_to_camera(cam: Camera3D, delta: float) -> void:
	# Axis expectations (normalized):
	# T.x: +left / -right -> local X (invert pan both directions)
	# T.y: +out / -in (zoom) -> local Z (forward/back)
	# T.z: +up / -down -> local Y (invert)
	var move := Vector3(_vel_t.x, -_vel_t.z, _vel_t.y) * delta
	if move.length() > 0.00001:
		cam.translate_object_local(move)

	if _vel_r.length() > 0.00001:
		# R.x: +pitch down / -pitch up, R.y: +roll left / -roll right, R.z: +yaw left / -yaw right.
		# Invert yaw/roll; restore pitch direction.
		var yaw := -_vel_r.z * delta
		var pitch := _vel_r.x * delta
		var roll := -_vel_r.y * 0.25 * delta
		cam.rotate_object_local(Vector3.UP, yaw)
		cam.rotate_object_local(Vector3.RIGHT, pitch)
		cam.rotate_object_local(Vector3.FORWARD, roll)


func _get_editor_camera() -> Camera3D:
	var base := get_editor_interface().get_base_control()
	if base == null:
		return null
	var cameras := base.find_children("*", "Camera3D", true, false)
	for cam in cameras:
		if cam is Camera3D and cam.is_visible_in_tree():
			return cam
	return null


func _load_native_extension() -> void:
	if not ResourceLoader.exists(_GDEXT_PATH):
		return
	var res := ResourceLoader.load(_GDEXT_PATH)
	if res:
		# Loading the resource ensures the binary is registered before we instantiate the class.
		pass


func _on_menu_id(id: int) -> void:
	match id:
		MENU_ENABLE:
			enabled = not enabled
			_menu.set_item_checked(_menu.get_item_index(MENU_ENABLE), enabled)
			if enabled and device:
				if device.is_enabled() == false:
					device.set_enabled(true)
				if device.get_state().get("connected", false) == false:
					device.open_first()
			elif device:
				device.set_enabled(false)
		MENU_DEBUG_OVERLAY:
			show_debug = not show_debug
			_menu.set_item_checked(_menu.get_item_index(MENU_DEBUG_OVERLAY), show_debug)
			if device:
				device.enable_raw_logging(show_debug)
		MENU_PRINT_REPORT:
			if device:
				var report: String = device.get_last_report_hex()
				if report.is_empty():
					push_warning("SpaceMouse: no report captured yet.")
				else:
					print("SpaceMouse last report: ", report)
		MENU_LOG_STATE:
			if device:
				var st: Dictionary = device.get_state()
				print("SpaceMouse state => T:", st.get("t"), " R:", st.get("r"), " Buttons:", st.get("buttons"), " Connected:", st.get("connected"))
