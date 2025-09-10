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

// PID Controller Implementation
PIDController::PIDController(float kp, float ki, float kd, float min_output, float max_output)
    : kp_(kp), ki_(ki), kd_(kd), target_(0.0f), integral_(0.0f), last_input_(0.0f),
      error_(0.0f), derivative_(0.0f), output_(0.0f), 
      min_output_(min_output), max_output_(max_output), first_run_(true) {
}

void PIDController::set_target(float target) {
    target_ = target;
}

void PIDController::set_tunings(float kp, float ki, float kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
}

void PIDController::set_output_limits(float min_output, float max_output) {
    min_output_ = min_output;
    max_output_ = max_output;
    
    // Clamp current output to new limits
    if (output_ > max_output_) output_ = max_output_;
    if (output_ < min_output_) output_ = min_output_;
    
    // Clamp integral to prevent windup
    float max_integral = (max_output_ - min_output_) / ki_;
    if (integral_ > max_integral) integral_ = max_integral;
    if (integral_ < -max_integral) integral_ = -max_integral;
}

void PIDController::reset() {
    integral_ = 0.0f;
    last_input_ = 0.0f;
    error_ = 0.0f;
    derivative_ = 0.0f;
    output_ = 0.0f;
    first_run_ = true;
}

float PIDController::compute(float input, uint32_t dt_ms) {
    if (dt_ms == 0) {
        return output_; // No time passed, return last output
    }
    
    float dt_sec = static_cast<float>(dt_ms) / 1000.0f;
    
    // Calculate error
    error_ = target_ - input;
    
    // Integral term with windup protection
    integral_ += error_ * dt_sec;
    float max_integral = (max_output_ - min_output_) / (ki_ + 1e-6f); // Prevent division by zero
    integral_ = std::max(-max_integral, std::min(max_integral, integral_));
    
    // Derivative term (derivative on measurement to avoid spikes on setpoint changes)
    if (first_run_) {
        derivative_ = 0.0f;
        first_run_ = false;
    } else {
        derivative_ = (last_input_ - input) / dt_sec;
    }
    last_input_ = input;
    
    // Calculate PID output
    output_ = (kp_ * error_) + (ki_ * integral_) + (kd_ * derivative_);
    
    // Apply output limits
    output_ = std::max(min_output_, std::min(max_output_, output_));
    
    return output_;
}

// Temperature Control Implementation
TemperatureControl::TemperatureControl()
    : pid_controller_(2.0f, 0.1f, 0.5f, 0.0f, 100.0f),
      last_update_ms_(0), last_fan_update_ms_(0), emergency_triggered_ms_(0),
      filtered_temperature_(0.0f), emergency_cooldown_(false) {
    
    status_.enabled = false;
    status_.thermal_emergency = false;
    status_.target_temp_c = config_.target_temp_c;
    
    pid_controller_.set_target(config_.target_temp_c);
}

void TemperatureControl::set_config(const TemperatureControlConfig& config) {
    config_ = config;
    
    // Update PID controller
    pid_controller_.set_target(config_.target_temp_c);
    pid_controller_.set_tunings(config_.kp, config_.ki, config_.kd);
    pid_controller_.set_output_limits(config_.min_fan_pwm, config_.max_fan_pwm);
    
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
    } else {
        LOG_INFO("Temperature control disabled");
        
        // Turn off fan when disabled
        if (fan_enable_callback_) {
            fan_enable_callback_(false);
        }
        if (fan_pwm_callback_) {
            fan_pwm_callback_(0.0f);
        }
        
        status_.fan_enabled = false;
        status_.fan_pwm_percent = 0.0f;
    }
}

void TemperatureControl::update(uint32_t current_time_ms) {
    if (!status_.enabled) {
        return;
    }
    
    // Calculate time delta
    uint32_t dt_ms = 0;
    if (last_update_ms_ != 0) {
        dt_ms = current_time_ms - last_update_ms_;
    }
    last_update_ms_ = current_time_ms;
    
    // Update sensor validity and get average temperature
    float avg_temp = get_average_temperature(current_time_ms);
    
    // Apply temperature filtering
    apply_temperature_filter(avg_temp);
    status_.current_temp_c = filtered_temperature_;
    
    // Update emergency state
    update_emergency_state(current_time_ms);
    
    // Update fan control if not in emergency
    if (!status_.thermal_emergency) {
        update_fan_control(current_time_ms);
    }
}

float TemperatureControl::get_average_temperature(uint32_t current_time_ms) {
    float temp_sum = 0.0f;
    uint32_t valid_count = 0;
    uint32_t total_count = 0;
    
    for (auto& sensor : sensors_) {
        total_count++;
        
        // Check if sensor data is recent
        if (sensor.valid && (current_time_ms - sensor.last_update_ms) <= config_.sensor_timeout_ms) {
            temp_sum += sensor.temperature_c;
            valid_count++;
        } else {
            sensor.valid = false; // Mark as invalid if too old
        }
    }
    
    status_.sensors_valid_count = valid_count;
    status_.sensors_total_count = total_count;
    
    if (valid_count == 0) {
        LOG_WARN("No valid temperature sensors available");
        return status_.current_temp_c; // Return last known temperature
    }
    
    return temp_sum / static_cast<float>(valid_count);
}

void TemperatureControl::update_emergency_state(uint32_t current_time_ms) {
    bool should_trigger_emergency = false;
    bool should_clear_emergency = false;
    
    if (!status_.thermal_emergency) {
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
        if (status_.current_temp_c <= config_.recovery_temp_c && !emergency_cooldown_) {
            should_clear_emergency = true;
        }
    }
    
    if (should_trigger_emergency) {
        status_.thermal_emergency = true;
        status_.emergency_start_ms = current_time_ms;
        emergency_cooldown_ = true;
        
        LOG_ERROR("THERMAL EMERGENCY ACTIVATED - Temperature: %.1f°C", status_.current_temp_c);
        
        // Turn on fan at maximum speed
        if (fan_enable_callback_) {
            fan_enable_callback_(true);
        }
        if (fan_pwm_callback_) {
            fan_pwm_callback_(100.0f);
        }
        status_.fan_enabled = true;
        status_.fan_pwm_percent = 100.0f;
        
        // Notify scheduler
        if (emergency_callback_) {
            emergency_callback_(true);
        }
    }
    
    if (should_clear_emergency) {
        status_.thermal_emergency = false;
        emergency_cooldown_ = false;
        emergency_triggered_ms_ = 0;
        
        LOG_INFO("Thermal emergency cleared - Temperature: %.1f°C", status_.current_temp_c);
        
        // Notify scheduler
        if (emergency_callback_) {
            emergency_callback_(false);
        }
        
        // Reset PID controller
        pid_controller_.reset();
    }
}

void TemperatureControl::update_fan_control(uint32_t current_time_ms) {
    // Check if it's time to update fan control
    if (current_time_ms - last_fan_update_ms_ < config_.fan_update_interval_ms) {
        return;
    }
    last_fan_update_ms_ = current_time_ms;
    
    // Calculate PID output
    uint32_t dt_ms = current_time_ms - (last_fan_update_ms_ - config_.fan_update_interval_ms);
    float pid_output = pid_controller_.compute(status_.current_temp_c, dt_ms);
    
    status_.pid_error = pid_controller_.get_error();
    status_.pid_output = pid_output;
    status_.fan_pwm_percent = pid_output;
    
    // Enable fan if PWM > 0
    bool should_enable_fan = pid_output > 0.1f;
    
    if (should_enable_fan != status_.fan_enabled) {
        status_.fan_enabled = should_enable_fan;
        if (fan_enable_callback_) {
            fan_enable_callback_(should_enable_fan);
        }
    }
    
    // Set fan PWM
    if (fan_pwm_callback_) {
        fan_pwm_callback_(should_enable_fan ? pid_output : 0.0f);
    }
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
    oss << "  Emergency: " << (status_.thermal_emergency ? "ACTIVE" : "normal") << "\n";
    oss << "  Current Temp: " << status_.current_temp_c << "°C\n";
    oss << "  Target Temp: " << status_.target_temp_c << "°C\n";
    oss << "  Fan Enabled: " << (status_.fan_enabled ? "YES" : "NO") << "\n";
    oss << "  Fan PWM: " << status_.fan_pwm_percent << "%\n";
    oss << "  Fan RPM: " << status_.fan_rpm << "\n";
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
        size_t pos = 0;
        
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

} // namespace ledbrick