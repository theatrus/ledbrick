#include "temperature_control.h"
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
      emergency_cooldown_(false), emergency_triggered_ms_(0),
      last_update_ms_(0), last_fan_update_ms_(0),
      last_valid_temp_ms_(0), last_pid_compute_temp_ms_(0),
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
        emergency_cooldown_ = false;
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
        command.fan_enabled = false;
        command.fan_pwm_percent = 0.0f;
        command.emergency_state = false;
        command.override_normal_control = true;
        command.reason = "Temperature control disabled";
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

    for (auto& sensor : sensors_) {
        total_count++;

        // Check if sensor data is recent and temperature is valid (> 0°C)
        if (sensor.valid &&
            (current_time_ms - sensor.last_update_ms) <= config_.sensor_timeout_ms &&
            sensor.temperature_c > 0.0f) {  // Reject 0°C or lower (catches NaN too)
            temp_sum += sensor.temperature_c;
            valid_count++;

            // Track the newest sensor reading timestamp
            if (sensor.last_update_ms > newest_sensor_time) {
                newest_sensor_time = sensor.last_update_ms;
            }
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
    
    if ((current_time_ms - last_valid_temp_ms) > 60000) {  // 60 seconds
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
        if (status_.current_temp_c >= config_.emergency_temp_c) {
            if (emergency_triggered_ms_ == 0) {
                emergency_triggered_ms_ = current_time_ms;
                LOG_WARN("Temperature %.1f°C exceeds emergency threshold %.1f°C - starting countdown",
                         status_.current_temp_c, config_.emergency_temp_c);
            } else if (current_time_ms - emergency_triggered_ms_ >= config_.emergency_delay_ms) {
                should_trigger_emergency = true;
            }
        } else {
            emergency_triggered_ms_ = 0; // Reset countdown if temperature drops
        }
    } else {
        // Check for recovery condition
        if (status_.current_temp_c <= config_.recovery_temp_c) {
            should_clear_emergency = true;
        }
    }
    
    if (should_trigger_emergency) {
        emergency_cooldown_ = true;
        
        LOG_ERROR("THERMAL EMERGENCY ACTIVATED - Temperature: %.1f°C", status_.current_temp_c);
        
        command.fan_enabled = true;
        command.fan_pwm_percent = 100.0f;
        command.emergency_state = true;
        command.override_normal_control = true;
        command.reason = "Thermal emergency triggered";
        return command;
    }
    
    if (should_clear_emergency) {
        emergency_cooldown_ = false;
        emergency_triggered_ms_ = 0;
        
        LOG_INFO("Thermal emergency cleared - Temperature: %.1f°C", status_.current_temp_c);
        
        // Reset PID controller
        pid_controller_.reset();
        
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
    // This prevents feeding the same temperature to PID multiple times
    bool have_new_temp_data = (last_valid_temp_ms_ > last_pid_compute_temp_ms_);

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
    uint32_t dt_ms = (last_pid_compute_temp_ms_ == 0) ?
        config_.fan_update_interval_ms :
        (last_valid_temp_ms_ - last_pid_compute_temp_ms_);

    float pid_output = pid_controller_.compute(status_.current_temp_c, dt_ms);

    // Update the timestamp of the temperature data we just used for PID
    last_pid_compute_temp_ms_ = last_valid_temp_ms_;

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

bool TemperatureControl::import_config_json(const std::string& json) {
    // Simple JSON parsing - in production would use a proper JSON library
        TemperatureControlConfig new_config = config_;
        
        // Parse each field using simple string searching
        // Helper lambda to extract float value
        auto extract_float = [&json](const std::string& key) -> float {
            size_t key_pos = json.find("\"" + key + "\":");
            if (key_pos == std::string::npos) return -1.0f;
            
            size_t value_start = json.find(":", key_pos) + 1;
            size_t value_end = json.find_first_of(",}", value_start);
            
            std::string value_str = json.substr(value_start, value_end - value_start);
            return std::stof(value_str);
        };
        
        // Extract values
        float val;
        if ((val = extract_float("target_temp_c")) > 0) new_config.target_temp_c = val;
        if ((val = extract_float("kp")) > 0) new_config.kp = val;
        if ((val = extract_float("ki")) >= 0) new_config.ki = val;
        if ((val = extract_float("kd")) >= 0) new_config.kd = val;
        if ((val = extract_float("min_fan_pwm")) >= 0) new_config.min_fan_pwm = val;
        if ((val = extract_float("max_fan_pwm")) > 0) new_config.max_fan_pwm = val;
        if ((val = extract_float("emergency_temp_c")) > 0) new_config.emergency_temp_c = val;
        if ((val = extract_float("recovery_temp_c")) > 0) new_config.recovery_temp_c = val;
        
        uint32_t ms_val;
        if ((ms_val = static_cast<uint32_t>(extract_float("fan_update_interval_ms"))) > 0) {
            new_config.fan_update_interval_ms = ms_val;
        }
        if ((ms_val = static_cast<uint32_t>(extract_float("emergency_delay_ms"))) > 0) {
            new_config.emergency_delay_ms = ms_val;
        }
        if ((ms_val = static_cast<uint32_t>(extract_float("sensor_timeout_ms"))) > 0) {
            new_config.sensor_timeout_ms = ms_val;
        }
        
        if ((val = extract_float("temp_filter_alpha")) > 0 && val <= 1.0f) {
            new_config.temp_filter_alpha = val;
        }
        
        // Apply the new configuration
        set_config(new_config);
        
        LOG_INFO("Temperature control configuration imported successfully");
        return true;
        
    
    // If we get here, parsing succeeded
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