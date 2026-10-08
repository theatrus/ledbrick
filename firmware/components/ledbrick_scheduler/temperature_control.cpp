#include "temperature_control.h"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <sstream>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char* TAG = "TemperatureControl";
#define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#include <iostream>
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

namespace ledbrick {

// Temperature Control Implementation
TemperatureControl::TemperatureControl()
    : pid_controller_(2.0f, 0.1f, 0.5f, 0.0f, 100.0f),
      emergency_countdown_active_(false), emergency_triggered_ms_(0),
      last_update_ms_(0), last_fan_update_ms_(0),
      last_valid_temp_ms_(0), last_pid_compute_temp_ms_(0), pid_has_computed_(false),
      filtered_temperature_(0.0f), ever_had_valid_temp_(false) {

    status_.enabled = false;
    status_.target_temp_c = config_.target_temp_c;

    pid_controller_.set_target(config_.target_temp_c);
}

void TemperatureControl::set_config(const TemperatureControlConfig& config) {
    config_ = config;
    
    // Update PID controller
    pid_controller_.set_target(config_.target_temp_c);
    // For cooling: use negative gains so that when temp > target, we get positive output
    pid_controller_.set_tunings(-config_.kp, -config_.ki, -config_.kd);
    pid_controller_.set_output_limits(0.0f, config_.max_fan_pwm);
    
    status_.target_temp_c = config_.target_temp_c;
    
    LOG_INFO("Temperature control config updated - Target: %.1f°C, PID: %.2f/%.3f/%.2f",
             config_.target_temp_c, config_.kp, config_.ki, config_.kd);
}

void TemperatureControl::add_temperature_sensor(const std::string& name) {
    // Check if sensor already exists
    for (auto& sensor : sensors_) {
        if (sensor.name == name) {
            LOG_WARN("Temperature sensor '%s' already exists", name.c_str());
            return;
        }
    }
    
    TemperatureSensor sensor;
    sensor.name = name;
    sensor.temperature_c = 0.0f;
    sensor.valid = false;
    sensor.last_update_ms = 0;
    
    sensors_.push_back(sensor);
    LOG_INFO("Added temperature sensor: %s", name.c_str());
}

void TemperatureControl::update_temperature_sensor(const std::string& name, float temp_c, uint32_t timestamp_ms) {
    for (auto& sensor : sensors_) {
        if (sensor.name == name) {
            sensor.temperature_c = temp_c;
            sensor.valid = true;
            sensor.last_update_ms = timestamp_ms;
            return;
        }
    }
    
    LOG_WARN("Temperature sensor '%s' not found for update", name.c_str());
}

std::vector<TemperatureSensor> TemperatureControl::get_sensors() const {
    return sensors_;
}

void TemperatureControl::enable(bool enabled) {
    if (status_.enabled == enabled) {
        return;
    }

    status_.enabled = enabled;

    if (enabled) {
        LOG_INFO("Temperature control enabled");
        pid_controller_.reset();
        pid_has_computed_ = false;
        // Note: We don't reset ever_had_valid_temp_ or last_valid_temp_ms_
        // to maintain safety state across enable/disable cycles
    } else {
        LOG_INFO("Temperature control disabled");
        // Note: Hardware state updates will be handled by TemperatureHardwareManager
        // when it receives the disable command
    }
}

TemperatureControlCommand TemperatureControl::compute_control_command(uint32_t current_time_ms) {
    TemperatureControlCommand command;
    
    if (!status_.enabled) {
        // Disabling control must not end an active emergency: keep the LEDs off and the
        // fan at full speed until control is re-enabled and the temperature recovers
        bool emergency = status_.hardware.thermal_emergency;
        command.fan_enabled = emergency;
        command.fan_pwm_percent = emergency ? 100.0f : 0.0f;
        command.emergency_state = emergency;
        command.override_normal_control = true;
        command.reason = emergency ? "Temperature control disabled during emergency"
                                   : "Temperature control disabled";
        return command;
    }
    
    // Update timestamp
    last_update_ms_ = current_time_ms;
    
    // Update sensor validity and get average temperature
    float avg_temp = get_average_temperature(current_time_ms);
    
    // Apply temperature filtering
    apply_temperature_filter(avg_temp);
    status_.current_temp_c = filtered_temperature_;
    
    // First check safety conditions
    TemperatureControlCommand safety_command = evaluate_safety_conditions(
        config_, status_.current_temp_c, ever_had_valid_temp_, 
        last_valid_temp_ms_, status_.sensors_valid_count, current_time_ms);
        
    if (safety_command.override_normal_control) {
        // Losing sensors must not end an active emergency; only a confirmed
        // temperature at or below recovery_temp_c clears it
        safety_command.emergency_state = status_.hardware.thermal_emergency;
        return safety_command;
    }
    
    // Check emergency state
    TemperatureControlCommand emergency_command = evaluate_emergency_state(current_time_ms);
    if (emergency_command.override_normal_control) {
        return emergency_command;
    }
    
    // Normal PID control
    return compute_fan_control(current_time_ms);
}

void TemperatureControl::update_hardware_state(const TemperatureHardwareState& hardware_state) {
    status_.hardware = hardware_state;
}

float TemperatureControl::get_average_temperature(uint32_t current_time_ms) {
    float temp_sum = 0.0f;
    uint32_t valid_count = 0;
    uint32_t total_count = 0;
    uint32_t newest_sensor_time = 0;  // Track the most recent sensor reading time
    float max_temp = 0.0f;

    for (auto& sensor : sensors_) {
        total_count++;

        // Signed difference so a reading stamped slightly after current_time_ms, or one
        // taken across a millis() wrap, does not look about 49 days old
        int32_t age_ms = static_cast<int32_t>(current_time_ms - sensor.last_update_ms);
        if (age_ms < 0) {
            age_ms = 0;
        }

        // Check if sensor data is recent and temperature is valid (> 0°C)
        if (sensor.valid &&
            static_cast<uint32_t>(age_ms) <= config_.sensor_timeout_ms &&
            sensor.temperature_c > 0.0f) {  // Reject 0°C or lower (catches NaN too)
            temp_sum += sensor.temperature_c;
            if (valid_count == 0 || sensor.temperature_c > max_temp) {
                max_temp = sensor.temperature_c;
            }

            // Track the newest sensor reading timestamp
            if (valid_count == 0 ||
                static_cast<int32_t>(sensor.last_update_ms - newest_sensor_time) > 0) {
                newest_sensor_time = sensor.last_update_ms;
            }
            valid_count++;
        } else {
            sensor.valid = false; // Mark as invalid if too old or invalid value
        }
    }

    status_.sensors_valid_count = valid_count;
    status_.sensors_total_count = total_count;

    if (valid_count == 0) {
        LOG_WARN("No valid temperature sensors available");
        return status_.current_temp_c; // Return last known temperature
    }
    status_.max_temp_c = max_temp;

    // We have at least one valid temperature - update tracking
    // Use the actual sensor reading time (newest sensor), not processing time
    last_valid_temp_ms_ = newest_sensor_time;
    if (!ever_had_valid_temp_) {
        ever_had_valid_temp_ = true;
        LOG_INFO("First valid temperature reading received: %.1f°C", temp_sum / static_cast<float>(valid_count));
    }

    return temp_sum / static_cast<float>(valid_count);
}

TemperatureControlCommand TemperatureControl::evaluate_safety_conditions(
    const TemperatureControlConfig& config,
    float current_temp_c,
    bool ever_had_valid_temp,
    uint32_t last_valid_temp_ms,
    uint32_t sensors_valid_count,
    uint32_t current_time_ms) {
    
    TemperatureControlCommand command;
    
    // SAFETY: Run fan at 100% in these conditions:
    // 1. Never received a valid temperature reading
    // 2. No valid temperature for more than 60 seconds
    // 3. No valid sensors currently available
    
    if (!ever_had_valid_temp) {
        command.fan_enabled = true;
        command.fan_pwm_percent = 100.0f;
        command.emergency_state = false;
        command.override_normal_control = true;
        command.reason = "Never received valid temperature";
        return command;
    }
    
    if (sensors_valid_count == 0) {
        command.fan_enabled = true;
        command.fan_pwm_percent = 100.0f;
        command.emergency_state = false;
        command.override_normal_control = true;
        command.reason = "No valid temperature sensors";
        return command;
    }
    
    if (static_cast<int32_t>(current_time_ms - last_valid_temp_ms) > 60000) {  // 60 seconds
        command.fan_enabled = true;
        command.fan_pwm_percent = 100.0f;
        command.emergency_state = false;
        command.override_normal_control = true;
        command.reason = "No valid temperature for >60 seconds";
        return command;
    }
    
    // No safety override needed
    command.override_normal_control = false;
    return command;
}

TemperatureControlCommand TemperatureControl::evaluate_emergency_state(uint32_t current_time_ms) {
    TemperatureControlCommand command;
    
    bool should_trigger_emergency = false;
    bool should_clear_emergency = false;
    
    if (!status_.hardware.thermal_emergency) {
        // Check for emergency condition
        // Judge the emergency on the hottest sensor, so one hot spot is not averaged away
        if (status_.max_temp_c >= config_.emergency_temp_c) {
            if (!emergency_countdown_active_) {
                emergency_countdown_active_ = true;
                emergency_triggered_ms_ = current_time_ms;
                LOG_WARN("Temperature %.1f°C exceeds emergency threshold %.1f°C - starting countdown",
                         status_.max_temp_c, config_.emergency_temp_c);
            } else if (current_time_ms - emergency_triggered_ms_ >= config_.emergency_delay_ms) {
                should_trigger_emergency = true;
            }
        } else {
            emergency_countdown_active_ = false; // Reset countdown if temperature drops
        }
    } else {
        // Check for recovery condition
        // Every working sensor must be at or below recovery, so losing the hottest
        // one cannot clear the emergency while it is still hot
        if (status_.max_temp_c <= config_.recovery_temp_c && status_.sensors_valid_count == status_.sensors_total_count) {
            should_clear_emergency = true;
        }
    }
    
    if (should_trigger_emergency) {
        LOG_ERROR("THERMAL EMERGENCY ACTIVATED - Temperature: %.1f°C", status_.max_temp_c);
        
        command.fan_enabled = true;
        command.fan_pwm_percent = 100.0f;
        command.emergency_state = true;
        command.override_normal_control = true;
        command.reason = "Thermal emergency triggered";
        return command;
    }
    
    if (should_clear_emergency) {
        emergency_countdown_active_ = false;
        
        LOG_INFO("Thermal emergency cleared - Temperature: %.1f°C", status_.max_temp_c);
        
        // Reset PID controller; the next compute uses the normal interval, not the
        // time spent in emergency
        pid_controller_.reset();
        pid_has_computed_ = false;
        
        command.fan_enabled = false;  // Let normal control take over
        command.fan_pwm_percent = 0.0f;
        command.emergency_state = false;
        command.override_normal_control = false;  // Return to normal control
        command.reason = "Emergency cleared, returning to normal control";
        return command;
    }
    
    // If we're in emergency but no state change, maintain emergency
    if (status_.hardware.thermal_emergency) {
        command.fan_enabled = true;
        command.fan_pwm_percent = 100.0f;
        command.emergency_state = true;
        command.override_normal_control = true;
        command.reason = "Maintaining emergency state";
        return command;
    }
    
    // No emergency override needed
    command.override_normal_control = false;
    return command;
}

TemperatureControlCommand TemperatureControl::compute_fan_control(uint32_t current_time_ms) {
    TemperatureControlCommand command;
    
    // Check if it's time to update fan control
    if (current_time_ms - last_fan_update_ms_ < config_.fan_update_interval_ms) {
        // Maintain current state - no update needed yet
        command.fan_enabled = status_.hardware.fan_enabled;
        command.fan_pwm_percent = status_.hardware.fan_pwm_percent;
        command.emergency_state = status_.hardware.thermal_emergency;
        command.override_normal_control = false;
        command.reason = "Fan update interval not reached";
        return command;
    }
    last_fan_update_ms_ = current_time_ms;

    // Only compute PID when we have NEW temperature data
    // This prevents feeding the same temperature to PID multiple times.
    // Compare with != so the check keeps working when millis() wraps.
    bool have_new_temp_data = !pid_has_computed_ || (last_valid_temp_ms_ != last_pid_compute_temp_ms_);

    if (!have_new_temp_data) {
        // No new temperature data - maintain current fan state without recomputing PID
        command.fan_enabled = status_.hardware.fan_enabled;
        command.fan_pwm_percent = status_.hardware.fan_pwm_percent;
        command.emergency_state = status_.hardware.thermal_emergency;
        command.override_normal_control = false;
        command.reason = "No new temperature data";
        return command;
    }

    // We have new temperature data - compute PID with proper time delta
    uint32_t dt_ms = !pid_has_computed_ ?
        config_.fan_update_interval_ms :
        (last_valid_temp_ms_ - last_pid_compute_temp_ms_);

    float pid_output = pid_controller_.compute(status_.current_temp_c, dt_ms);

    // Update the timestamp of the temperature data we just used for PID
    last_pid_compute_temp_ms_ = last_valid_temp_ms_;
    pid_has_computed_ = true;

    status_.pid_error = pid_controller_.get_error();
    status_.pid_output = pid_output;

    // Temperature error: positive = too hot, negative = too cold
    float temp_error = status_.current_temp_c - config_.target_temp_c;

    // Control band and hysteresis thresholds
    const float CONTROL_BAND = 10.0f;        // PID controls within ±10°C of target
    const float TURN_ON_THRESHOLD = -5.0f;   // Turn fan ON when temp > target - 5°C
    const float TURN_OFF_THRESHOLD = -10.0f; // Turn fan OFF when temp < target - 10°C

    // Determine if we should enable the fan (with hysteresis)
    bool should_enable_fan = status_.hardware.fan_enabled; // Start with current hardware state
    float fan_pwm_output = 0.0f;

    if (temp_error < TURN_OFF_THRESHOLD) {
        // Below target - 10°C - turn fan off
        should_enable_fan = false;
        fan_pwm_output = 0.0f;
    } else if (temp_error > TURN_ON_THRESHOLD) {
        // Above target + 1°C - turn fan on
        should_enable_fan = true;
    }
    // If between TURN_OFF and TURN_ON, maintain current fan state (hysteresis)

    // If fan is enabled, determine PWM based on control strategy
    if (should_enable_fan) {
        if (std::abs(temp_error) <= CONTROL_BAND) {
            // Within control band - use PID output
            // PID output will be positive when we need cooling (due to negative gains)
            fan_pwm_output = pid_output;

            // Apply minimum fan PWM floor
            if (fan_pwm_output < config_.min_fan_pwm) {
                fan_pwm_output = config_.min_fan_pwm;
            }
        } else {
            // Outside control band but fan should be on - use PID with min floor
            fan_pwm_output = (pid_output < config_.min_fan_pwm) ? config_.min_fan_pwm : pid_output;
        }
    }
    
    command.fan_enabled = should_enable_fan;
    command.fan_pwm_percent = fan_pwm_output;
    command.emergency_state = false;  // This is normal control, not emergency
    command.override_normal_control = false;
    command.reason = "Normal PID control";
    
    return command;
}

void TemperatureControl::apply_temperature_filter(float new_temp) {
    if (filtered_temperature_ == 0.0f) {
        filtered_temperature_ = new_temp; // Initialize
    } else {
        // Low-pass filter: filtered = alpha * new + (1-alpha) * old
        filtered_temperature_ = config_.temp_filter_alpha * new_temp + 
                               (1.0f - config_.temp_filter_alpha) * filtered_temperature_;
    }
}

void TemperatureControl::reset_pid() {
    pid_controller_.reset();
    LOG_INFO("PID controller reset");
}

std::string TemperatureControl::get_diagnostics() const {
    std::ostringstream oss;
    oss << "Temperature Control Diagnostics:\n";
    oss << "  Enabled: " << (status_.enabled ? "YES" : "NO") << "\n";
    oss << "  Emergency: " << (status_.hardware.thermal_emergency ? "ACTIVE" : "normal") << "\n";
    oss << "  Current Temp: " << status_.current_temp_c << "°C\n";
    oss << "  Target Temp: " << status_.target_temp_c << "°C\n";
    oss << "  Fan Enabled: " << (status_.hardware.fan_enabled ? "YES" : "NO") << "\n";
    oss << "  Fan PWM: " << status_.hardware.fan_pwm_percent << "%\n";
    oss << "  Fan RPM: " << status_.hardware.fan_rpm << "\n";
    oss << "  PID Error: " << status_.pid_error << "\n";
    oss << "  PID Output: " << status_.pid_output << "\n";
    oss << "  Sensors: " << status_.sensors_valid_count << "/" << status_.sensors_total_count << " valid\n";
    
    oss << "  Sensor Details:\n";
    for (const auto& sensor : sensors_) {
        oss << "    " << sensor.name << ": " << sensor.temperature_c << "°C ";
        oss << (sensor.valid ? "[VALID]" : "[INVALID]") << "\n";
    }
    
    return oss.str();
}

std::string TemperatureControl::export_config_json() const {
    std::ostringstream json;
    json << "{";
    json << "\"target_temp_c\":" << config_.target_temp_c << ",";
    json << "\"kp\":" << config_.kp << ",";
    json << "\"ki\":" << config_.ki << ",";
    json << "\"kd\":" << config_.kd << ",";
    json << "\"min_fan_pwm\":" << config_.min_fan_pwm << ",";
    json << "\"max_fan_pwm\":" << config_.max_fan_pwm << ",";
    json << "\"fan_update_interval_ms\":" << config_.fan_update_interval_ms << ",";
    json << "\"emergency_temp_c\":" << config_.emergency_temp_c << ",";
    json << "\"recovery_temp_c\":" << config_.recovery_temp_c << ",";
    json << "\"emergency_delay_ms\":" << config_.emergency_delay_ms << ",";
    json << "\"sensor_timeout_ms\":" << config_.sensor_timeout_ms << ",";
    json << "\"temp_filter_alpha\":" << config_.temp_filter_alpha;
    json << "}";
    return json.str();
}

static const uint32_t MIN_SENSOR_TIMEOUT_MS = 10000;

bool TemperatureControl::validate_config(const TemperatureControlConfig& c, std::string* error) {
    auto fail = [error](const char* message) {
        if (error) *error = message;
        return false;
    };
    // Ranges match the limits in the web UI
    if (!(c.target_temp_c >= 20.0f && c.target_temp_c <= 70.0f)) return fail("target_temp_c must be 20-70");
    if (!(c.emergency_temp_c >= 50.0f && c.emergency_temp_c <= 100.0f)) return fail("emergency_temp_c must be 50-100");
    if (!(c.recovery_temp_c >= 40.0f && c.recovery_temp_c <= 90.0f)) return fail("recovery_temp_c must be 40-90");
    if (!(c.kp >= 0.0f && c.kp <= 10.0f)) return fail("kp must be 0-10");
    if (!(c.ki >= 0.0f && c.ki <= 1.0f)) return fail("ki must be 0-1");
    if (!(c.kd >= 0.0f && c.kd <= 5.0f)) return fail("kd must be 0-5");
    if (!(c.min_fan_pwm >= 0.0f && c.min_fan_pwm <= 100.0f)) return fail("min_fan_pwm must be 0-100");
    if (!(c.max_fan_pwm > 0.0f && c.max_fan_pwm <= 100.0f)) return fail("max_fan_pwm must be above 0 and at most 100");
    if (c.fan_update_interval_ms < 100 || c.fan_update_interval_ms > 10000) return fail("fan_update_interval_ms must be 100-10000");
    if (c.emergency_delay_ms > 60000) return fail("emergency_delay_ms must be 0-60000");
    // Readings are stamped when the sensor publishes (every 5 s for the DS18B20s), so a
    // shorter timeout marks every sensor stale and the emergency check never runs
    if (c.sensor_timeout_ms < MIN_SENSOR_TIMEOUT_MS || c.sensor_timeout_ms > 60000) return fail("sensor_timeout_ms must be 10000-60000");
    if (!(c.temp_filter_alpha > 0.0f && c.temp_filter_alpha <= 1.0f)) return fail("temp_filter_alpha must be above 0 and at most 1");
    if (c.min_fan_pwm > c.max_fan_pwm) return fail("min_fan_pwm must not exceed max_fan_pwm");
    if (c.recovery_temp_c >= c.emergency_temp_c) return fail("recovery_temp_c must be below emergency_temp_c");
    if (c.target_temp_c >= c.emergency_temp_c) return fail("target_temp_c must be below emergency_temp_c");
    return true;
}

void TemperatureControl::repair_config(TemperatureControlConfig& c) {
    auto fix = [](const char* name, float& value, float low, float high) {
        float fixed = std::max(low, std::min(high, value));
        if (fixed != value) {
            LOG_WARN("Saved %s %.2f out of range, using %.2f", name, value, fixed);
            value = fixed;
        }
    };
    auto fix_ms = [](const char* name, uint32_t& value, uint32_t low, uint32_t high) {
        uint32_t fixed = std::max(low, std::min(high, value));
        if (fixed != value) {
            LOG_WARN("Saved %s %u out of range, using %u", name, static_cast<unsigned>(value), static_cast<unsigned>(fixed));
            value = fixed;
        }
    };
    // Never raise the emergency or recovery temperature: lower is the safe side
    fix("emergency_temp_c", c.emergency_temp_c, 0.0f, 100.0f);
    fix("recovery_temp_c", c.recovery_temp_c, 0.0f, 90.0f);
    fix("target_temp_c", c.target_temp_c, 0.0f, 70.0f);
    if (c.recovery_temp_c >= c.emergency_temp_c) {
        fix("recovery_temp_c", c.recovery_temp_c, 0.0f, c.emergency_temp_c - 5.0f);
    }
    if (c.target_temp_c >= c.emergency_temp_c) {
        fix("target_temp_c", c.target_temp_c, 0.0f, c.emergency_temp_c - 5.0f);
    }
    fix("kp", c.kp, 0.0f, 10.0f);
    fix("ki", c.ki, 0.0f, 1.0f);
    fix("kd", c.kd, 0.0f, 5.0f);
    fix("max_fan_pwm", c.max_fan_pwm, 1.0f, 100.0f);
    fix("min_fan_pwm", c.min_fan_pwm, 0.0f, c.max_fan_pwm);
    fix("temp_filter_alpha", c.temp_filter_alpha, 0.01f, 1.0f);
    fix_ms("fan_update_interval_ms", c.fan_update_interval_ms, 100, 10000);
    fix_ms("emergency_delay_ms", c.emergency_delay_ms, 0, 60000);
    fix_ms("sensor_timeout_ms", c.sensor_timeout_ms, MIN_SENSOR_TIMEOUT_MS, 60000);
}

bool TemperatureControl::import_config_json(const std::string& json, std::string* error, bool repair) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        if (error) *error = "Invalid JSON";
        LOG_WARN("Temperature control config rejected: invalid JSON");
        return false;
    }

    TemperatureControlConfig new_config = config_;
    std::string bad_field;

    // Missing keys keep their current value; a present key must be a finite number
    auto read_float = [&](const char* key, float& out) {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (item == nullptr) return;
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) {
            if (bad_field.empty()) bad_field = key;
            return;
        }
        out = static_cast<float>(item->valuedouble);
    };
    auto read_ms = [&](const char* key, uint32_t& out) {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (item == nullptr) return;
        if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0.0 && item->valuedouble <= 4294967295.0)) {
            if (bad_field.empty()) bad_field = key;
            return;
        }
        out = static_cast<uint32_t>(item->valuedouble);
    };

    read_float("target_temp_c", new_config.target_temp_c);
    read_float("kp", new_config.kp);
    read_float("ki", new_config.ki);
    read_float("kd", new_config.kd);
    read_float("min_fan_pwm", new_config.min_fan_pwm);
    read_float("max_fan_pwm", new_config.max_fan_pwm);
    read_float("emergency_temp_c", new_config.emergency_temp_c);
    read_float("recovery_temp_c", new_config.recovery_temp_c);
    read_float("temp_filter_alpha", new_config.temp_filter_alpha);
    read_ms("fan_update_interval_ms", new_config.fan_update_interval_ms);
    read_ms("emergency_delay_ms", new_config.emergency_delay_ms);
    read_ms("sensor_timeout_ms", new_config.sensor_timeout_ms);
    cJSON_Delete(root);

    if (repair) {
        // Fields that failed to parse kept their current values
        if (!bad_field.empty()) {
            LOG_WARN("Saved temperature setting %s is not a number, keeping current value", bad_field.c_str());
        }
        repair_config(new_config);
        set_config(new_config);
        LOG_INFO("Temperature control configuration loaded");
        return true;
    }

    if (!bad_field.empty()) {
        if (error) *error = bad_field + " must be a number";
        LOG_WARN("Temperature control config rejected: %s is not a valid number", bad_field.c_str());
        return false;
    }

    std::string validation_error;
    if (!validate_config(new_config, &validation_error)) {
        if (error) *error = validation_error;
        LOG_WARN("Temperature control config rejected: %s", validation_error.c_str());
        return false;
    }

    set_config(new_config);
    LOG_INFO("Temperature control configuration imported successfully");
    return true;
}

std::vector<TemperatureControl::FanCurvePoint> TemperatureControl::get_fan_curve() const {
    std::vector<FanCurvePoint> curve;
    
    // Simple linear fan curve with key points
    // Below target - proportional response
    // Above target - aggressive cooling
    // At emergency - full speed
    
    float margin = 10.0f;  // Temperature margin for curve
    
    // Key points for the fan curve
    curve.push_back({config_.target_temp_c - margin, config_.min_fan_pwm});  // Well below target
    curve.push_back({config_.target_temp_c - 5.0f, config_.min_fan_pwm});   // Approaching target
    curve.push_back({config_.target_temp_c, 30.0f});                         // At target
    curve.push_back({config_.target_temp_c + 5.0f, 60.0f});                  // Above target
    curve.push_back({config_.recovery_temp_c, 80.0f});                       // Near recovery
    curve.push_back({config_.emergency_temp_c, 100.0f});                     // Emergency threshold
    curve.push_back({config_.emergency_temp_c + 5.0f, 100.0f});              // Above emergency
    
    return curve;
}

// TemperatureHardwareManager Implementation
TemperatureHardwareManager::TemperatureHardwareManager() {
    hardware_state_.fan_enabled = false;
    hardware_state_.fan_pwm_percent = 0.0f;
    hardware_state_.fan_rpm = 0.0f;
    hardware_state_.thermal_emergency = false;
    hardware_state_.emergency_start_ms = 0;
}

void TemperatureHardwareManager::apply_command(const TemperatureControlCommand& command, uint32_t current_time_ms) {
    bool state_changed = false;
    
    // Update emergency state
    if (hardware_state_.thermal_emergency != command.emergency_state) {
        hardware_state_.thermal_emergency = command.emergency_state;
        if (command.emergency_state) {
            hardware_state_.emergency_start_ms = current_time_ms;
        } else {
            hardware_state_.emergency_start_ms = 0;
        }
        
        if (emergency_callback_) {
            emergency_callback_(command.emergency_state);
        }
        state_changed = true;
    }
    
    // Check what's changing before we update state
    bool enable_changing = (hardware_state_.fan_enabled != command.fan_enabled);
    bool pwm_changing = (hardware_state_.fan_pwm_percent != command.fan_pwm_percent);
    bool any_fan_change = enable_changing || pwm_changing;
    
    // Update fan enable state
    if (enable_changing) {
        hardware_state_.fan_enabled = command.fan_enabled;
        if (fan_enable_callback_) {
            fan_enable_callback_(command.fan_enabled);
        }
        state_changed = true;
    }
    
    // Update fan PWM - always update PWM and call callback if any fan state changed
    hardware_state_.fan_pwm_percent = command.fan_pwm_percent;
    if (any_fan_change && fan_pwm_callback_) {
        fan_pwm_callback_(command.fan_pwm_percent);
    }
    
    if (pwm_changing) {
        state_changed = true;
    }
    
    // Log significant state changes
    if (state_changed && command.override_normal_control) {
        LOG_INFO("Hardware state updated: %s - Fan: %s %.1f%%, Emergency: %s", 
                command.reason.c_str(),
                command.fan_enabled ? "ON" : "OFF",
                command.fan_pwm_percent,
                command.emergency_state ? "YES" : "NO");
    }
}

} // namespace ledbrick