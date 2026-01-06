#include "spacemouse_device.h"

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <locale>

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

String wchar_to_string(const wchar_t *p_str) {
	if (p_str == nullptr) {
		return String();
	}
	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
	std::wstring ws(p_str);
	std::string utf8 = conv.to_bytes(ws);
	return String::utf8(utf8.c_str());
}

int16_t read_le_i16(const uint8_t *p_data) {
	return static_cast<int16_t>(p_data[0] | (p_data[1] << 8));
}

} // namespace

void SpaceMouseDevice::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_first"), &SpaceMouseDevice::open_first);
	ClassDB::bind_method(D_METHOD("close"), &SpaceMouseDevice::close);
	ClassDB::bind_method(D_METHOD("get_state"), &SpaceMouseDevice::get_state);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &SpaceMouseDevice::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &SpaceMouseDevice::is_enabled);
	ClassDB::bind_method(D_METHOD("enable_raw_logging", "enabled"), &SpaceMouseDevice::enable_raw_logging);
	ClassDB::bind_method(D_METHOD("get_last_report_hex"), &SpaceMouseDevice::get_last_report_hex);
}

SpaceMouseDevice::SpaceMouseDevice() {
	hid_init();
}

SpaceMouseDevice::~SpaceMouseDevice() {
	close();
}

bool SpaceMouseDevice::matches_spacemouse(const hid_device_info *p_info) const {
	if (p_info == nullptr) {
		return false;
	}
	String manufacturer = wchar_to_string(p_info->manufacturer_string).to_lower();
	String product = wchar_to_string(p_info->product_string).to_lower();
	if (manufacturer.find("3dconnexion") != -1) {
		return true;
	}
	if (product.find("space") != -1) {
		return true;
	}
	return false;
}

bool SpaceMouseDevice::open_first() {
	close();

	hid_device_info *info = hid_enumerate(0, 0);
	hid_device_info *cur = info;
	const hid_device_info *best = nullptr;
	int best_score = -1;
	while (cur != nullptr) {
		if (!matches_spacemouse(cur)) {
			cur = cur->next;
			continue;
		}
		String path_str = String::utf8(cur->path).to_lower();
		// Prefer the multi-axis controller interface (usage_page 0x01, usage 0x08),
		// then mouse (0x01, 0x02), then vendor pages.
		int score = 1;
		if (cur->usage_page == 0x01 && cur->usage == 0x08) {
			score = 100;
		} else if (cur->usage_page == 0x01 && cur->usage == 0x02) {
			score = 80;
		} else if ((cur->usage_page & 0xFF00) == 0xFF00) {
			score = 60;
		}
		// Some devices expose multiple collections; prefer the first collection.
		if (path_str.find("col01") != -1 || path_str.find("mi_00") != -1) {
			score += 20;
		}
		if (score > best_score) {
			best_score = score;
			best = cur;
		}
		cur = cur->next;
	}

	if (best != nullptr) {
		device = hid_open_path(best->path);
		std::lock_guard<std::mutex> lock(state_mutex);
		usage_page = best->usage_page;
		usage = best->usage;
		device_path = String::utf8(best->path);
	} else {
		usage_page = 0;
		usage = 0;
		device_path = "";
	}
	hid_free_enumeration(info);

	if (device == nullptr) {
		UtilityFunctions::push_warning("SpaceMouse: no compatible HID device found.");
		return false;
	}

	hid_set_nonblocking(device, 1);
	translation = Vector3();
	rotation = Vector3();
	buttons = PackedInt32Array();
	last_report.clear();
	last_report_id = 0;
	seen_reports.clear();
	read_count = 0;
	error_count = 0;
	loop_count = 0;
	last_tick_ms = 0;
	thread_alive = false;
	connected = true;
	running = true;
	reader = std::thread(&SpaceMouseDevice::reader_loop, this);
	return true;
}

void SpaceMouseDevice::close() {
	running = false;
	if (reader.joinable()) {
		reader.join();
	}

	if (device != nullptr) {
		hid_close(device);
		device = nullptr;
	}
	connected = false;
}

void SpaceMouseDevice::set_enabled(bool p_enabled) {
	enabled = p_enabled;
}

void SpaceMouseDevice::enable_raw_logging(bool p_enabled) {
	raw_logging = p_enabled;
}

String SpaceMouseDevice::get_last_report_hex() const {
	std::lock_guard<std::mutex> lock(state_mutex);
	if (last_report.empty()) {
		return "";
	}
	String hex;
	for (size_t i = 0; i < last_report.size(); i++) {
		hex += String::num_int64(last_report[i], 16).pad_zeros(2);
		if (i + 1 < last_report.size()) {
			hex += " ";
		}
	}
	return hex;
}

Dictionary SpaceMouseDevice::get_state() {
	Dictionary state;
	// If the background reader hasn't started, opportunistically poll here (non-blocking).
	if (!thread_alive.load() && device != nullptr && enabled.load()) {
		std::vector<uint8_t> tmp(64);
		while (true) {
			int res = hid_read(device, tmp.data(), static_cast<int>(tmp.size()));
			std::lock_guard<std::mutex> lock(state_mutex);
			last_read_result = res;
			if (res > 0) {
				handle_report(tmp.data(), static_cast<size_t>(res));
				read_count++;
				continue;
			} else if (res < 0) {
				error_count++;
				const wchar_t *err = hid_error(device);
				if (err != nullptr) {
					last_error = wchar_to_string(err);
				} else {
					last_error = "Unknown HID error (poll)";
				}
			}
			break;
		}
	}
	std::lock_guard<std::mutex> lock(state_mutex);
	state["t"] = translation;
	state["r"] = rotation;
	state["buttons"] = buttons;
	state["report_id"] = last_report_id;
	state["usage_page"] = usage_page;
	state["usage"] = usage;
	state["path"] = device_path;
	state["last_read_result"] = last_read_result;
	state["last_error"] = last_error;
	state["read_count"] = (int64_t)read_count;
	state["error_count"] = (int64_t)error_count;
	state["loop_count"] = (int64_t)loop_count;
	state["last_tick_ms"] = (int64_t)last_tick_ms;
	state["thread_alive"] = thread_alive.load();
	Dictionary seen;
	for (const auto &kv : seen_reports) {
		seen[String::num_int64(kv.first)] = kv.second;
	}
	state["seen_reports"] = seen;
	state["connected"] = connected.load();
	return state;
}

void SpaceMouseDevice::reader_loop() {
	std::vector<uint8_t> buffer(64);

	thread_alive = true;
	while (running) {
		{
			std::lock_guard<std::mutex> lock(state_mutex);
			loop_count++;
			auto now = std::chrono::steady_clock::now().time_since_epoch();
			last_tick_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
		}

		if (!enabled) {
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			continue;
		}

		if (device == nullptr) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			continue;
		}

		// Use a short timeout to avoid busy-waiting but keep responsive updates.
		int res = hid_read_timeout(device, buffer.data(), static_cast<int>(buffer.size()), 16);
		{
			std::lock_guard<std::mutex> lock(state_mutex);
			last_read_result = res;
			if (res < 0) {
				error_count++;
				const wchar_t *err = hid_error(device);
				if (err != nullptr) {
					last_error = wchar_to_string(err);
				} else {
					last_error = "Unknown HID error";
				}
			}
			if (res >= 0) {
				read_count++;
			}
		}
		if (res > 0) {
			handle_report(buffer.data(), static_cast<size_t>(res));
		} else if (res == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		} else {
			connected = false;
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
	}

	connected = false;
	thread_alive = false;
}

void SpaceMouseDevice::handle_report(const uint8_t *p_data, size_t p_size) {
	if (p_size == 0 || p_data == nullptr) {
		return;
	}

	uint8_t report_id = p_data[0];
	connected = true;
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		last_report_id = report_id;
		last_report.assign(p_data, p_data + p_size);
		seen_reports[report_id]++;
	}

	switch (report_id) {
		// Many 3Dconnexion devices pack both translation and rotation into report 0x01 (12 bytes after id).
		case 0x01: // combined or translation-only
			if (p_size >= 13) {
				// Primary interpretation: first 3 axes = translation, next 3 = rotation.
				Vector3 primary_t(
						(double)read_le_i16(p_data + 1),
						(double)read_le_i16(p_data + 3),
						(double)read_le_i16(p_data + 5));
				Vector3 primary_r(
						(double)read_le_i16(p_data + 7),
						(double)read_le_i16(p_data + 9),
						(double)read_le_i16(p_data + 11));
				// Alternate interpretation for devices that swap blocks.
				Vector3 alt_t(
						(double)read_le_i16(p_data + 7),
						(double)read_le_i16(p_data + 9),
						(double)read_le_i16(p_data + 11));
				Vector3 alt_r(
						(double)read_le_i16(p_data + 1),
						(double)read_le_i16(p_data + 3),
						(double)read_le_i16(p_data + 5));

				// Heuristic: if rotation block is all ~zero but alt_r is not, use the alternate mapping.
				bool primary_r_zero = primary_r.length_squared() < 1.0;
				bool primary_t_zero = primary_t.length_squared() < 1.0;
				if (primary_r_zero && alt_r.length_squared() >= 1.0) {
					primary_r = alt_r;
				}
				if (primary_t_zero && alt_t.length_squared() >= 1.0) {
					primary_t = alt_t;
				}

				std::lock_guard<std::mutex> lock(state_mutex);
				translation = primary_t;
				rotation = primary_r;
			} else if (p_size >= 7) {
				handle_translation(p_data + 1);
			}
			break;
		case 0x02: // rotation-only (some devices)
			if (p_size >= 7) {
				handle_rotation(p_data + 1);
			}
			break;
		case 0x03: // buttons (common)
		case 0x05: // buttons (wireless/extended)
		case 0x06: // buttons (wireless/extended)
			if (p_size >= 2) {
				handle_buttons(p_data + 1, p_size - 1);
			}
			break;
		case 0x04: // rotation on some, buttons on others
			if (p_size >= 7) {
				handle_rotation(p_data + 1);
			} else if (p_size >= 2) {
				handle_buttons(p_data + 1, p_size - 1);
			}
			break;
		default:
			if (raw_logging) {
				UtilityFunctions::print(String("SpaceMouse: unhandled report id: ") + String::num_int64(report_id, 16));
			}
			break;
	}

	if (raw_logging) {
		UtilityFunctions::print("SpaceMouse report id ", report_id, " size ", (int)p_size, " -> ", get_last_report_hex());
	}
}

void SpaceMouseDevice::handle_translation(const uint8_t *p_data) {
	int16_t x = read_le_i16(p_data + 0);
	int16_t y = read_le_i16(p_data + 2);
	int16_t z = read_le_i16(p_data + 4);

	std::lock_guard<std::mutex> lock(state_mutex);
	translation = Vector3((double)x, (double)y, (double)z);
}

void SpaceMouseDevice::handle_rotation(const uint8_t *p_data) {
	int16_t rx = read_le_i16(p_data + 0);
	int16_t ry = read_le_i16(p_data + 2);
	int16_t rz = read_le_i16(p_data + 4);

	std::lock_guard<std::mutex> lock(state_mutex);
	rotation = Vector3((double)rx, (double)ry, (double)rz);
}

void SpaceMouseDevice::handle_buttons(const uint8_t *p_data, size_t p_size) {
	uint32_t mask = 0;
	size_t limit = std::min(p_size, sizeof(uint32_t));
	for (size_t i = 0; i < limit; i++) {
		mask |= static_cast<uint32_t>(p_data[i]) << (8 * i);
	}

	PackedInt32Array btns;
	for (int i = 0; i < 32; i++) {
		if (mask & (1u << i)) {
			btns.push_back(i);
		}
	}

	std::lock_guard<std::mutex> lock(state_mutex);
	buttons = btns;
}
