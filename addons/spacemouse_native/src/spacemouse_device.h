#ifndef SPACEMOUSE_DEVICE_H
#define SPACEMOUSE_DEVICE_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "hidapi/hidapi.h"

namespace godot {

class SpaceMouseDevice : public RefCounted {
	GDCLASS(SpaceMouseDevice, RefCounted);

public:
	SpaceMouseDevice();
	~SpaceMouseDevice();

	bool open_first();
	void close();

	Dictionary get_state();

	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }

	void enable_raw_logging(bool p_enabled);
	String get_last_report_hex() const;

private:
	static void _bind_methods();

	void reader_loop();
	void handle_report(const uint8_t *p_data, size_t p_size);
	void handle_translation(const uint8_t *p_data);
	void handle_rotation(const uint8_t *p_data);
	void handle_buttons(const uint8_t *p_data, size_t p_size);
	bool matches_spacemouse(const hid_device_info *p_info) const;

	hid_device *device = nullptr;
	std::thread reader;
	std::atomic_bool running{false};
	std::atomic_bool enabled{true};
	std::atomic_bool connected{false};
	std::atomic_bool raw_logging{false};
	uint8_t last_report_id = 0;
	uint16_t usage_page = 0;
	uint16_t usage = 0;
	String device_path;
	int last_read_result = 0;
	String last_error;
	uint64_t read_count = 0;
	uint64_t error_count = 0;
	uint64_t loop_count = 0;
	uint64_t last_tick_ms = 0;
	std::atomic_bool thread_alive{false};

	mutable std::mutex state_mutex;
	Vector3 translation;
	Vector3 rotation;
	PackedInt32Array buttons;
	std::vector<uint8_t> last_report;
	std::unordered_map<uint8_t, int> seen_reports;
};

} // namespace godot

#endif // SPACEMOUSE_DEVICE_H
