#include "ledbrick_temperature_control.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace ledbrick_temperature_control {

static const char *const TAG = "ledbrick.temp_control";

void LEDBrickTemperatureControl::setup() {
    ESP_LOGCONFIG(TAG, "Setting up LEDBrick Temperature Control...");
    
    if (!scheduler_) {
        ESP_LOGE(TAG, "Scheduler not configured!");
        this->mark_failed();
        return;
    }
    
    // Initialize preferences with a unique hash
    uint32_t pref_hash = 0x54454D50;  // "TEMP" in hex
    preferences_ = global_preferences->make_preference<uint32_t>(pref_hash);
    
    // Load saved configuration
    load_config_();
    
    // Configure the temperature control module
    temp_control_.set_config(config_);
    
    // Set up callbacks
    temp_control_.set_fan_pwm_callback([this](float pwm) { this->on_fan_pwm_change_(pwm); });
    temp_control_.set_fan_enable_callback([this](bool enabled) { this->on_fan_enable_change_(enabled); });
    temp_control_.set_emergency_callback([this](bool emergency) { this->on_emergency_change_(emergency); });
    
    // Add temperature sensors to the control module
    for (const auto &mapping : temp_sensors_) {
        temp_control_.add_temperature_sensor(mapping.name);
    }
    
    // Initialize sensor publishing
    last_sensor_publish_ = millis();
    
    // Enable temperature control by default
    temp_control_.enable(true);
    
    initialized_ = true;
    ESP_LOGCONFIG(TAG, "LEDBrick Temperature Control setup complete");
    ESP_LOGCONFIG(TAG, "  Fan object: %s", fan_ ? "Connected" : "NOT CONNECTED");
    ESP_LOGCONFIG(TAG, "  Fan power switch: %s", fan_power_switch_ ? "Connected" : "NOT CONNECTED");
}

void LEDBrickTemperatureControl::loop() {
    if (!initialized_) {
        return;
    }
    
    uint32_t now = millis();
    
    // Check if it's time for an update
    if (now - last_update_ < update_interval_) {
        return;
    }
    last_update_ = now;
    
    // Update temperature sensor readings
    update_temperature_sensors_();
    
    // Update fan speed reading
    update_fan_speed_();
    
    // Run temperature control update
    temp_control_.update(now);
    
    // Publish sensor values periodically (every 5 seconds)
    if (now - last_sensor_publish_ >= 5000) {
        publish_sensor_values_();
        last_sensor_publish_ = now;
    }
}

void LEDBrickTemperatureControl::dump_config() {
    ESP_LOGCONFIG(TAG, "LEDBrick Temperature Control:");
    ESP_LOGCONFIG(TAG, "  Target Temperature: %.1f°C", config_.target_temp_c);
    ESP_LOGCONFIG(TAG, "  PID Parameters: Kp=%.2f, Ki=%.3f, Kd=%.2f", config_.kp, config_.ki, config_.kd);
    ESP_LOGCONFIG(TAG, "  Fan PWM Range: %.1f%% - %.1f%%", config_.min_fan_pwm, config_.max_fan_pwm);
    ESP_LOGCONFIG(TAG, "  Emergency Temperature: %.1f°C", config_.emergency_temp_c);
    ESP_LOGCONFIG(TAG, "  Recovery Temperature: %.1f°C", config_.recovery_temp_c);
    ESP_LOGCONFIG(TAG, "  Emergency Delay: %u ms", config_.emergency_delay_ms);
    ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", update_interval_);
    ESP_LOGCONFIG(TAG, "  Temperature Sensors: %zu", temp_sensors_.size());
    
    for (const auto &mapping : temp_sensors_) {
        ESP_LOGCONFIG(TAG, "    - %s", mapping.name.c_str());
    }
}

void LEDBrickTemperatureControl::add_temperature_sensor(const std::string &name, sensor::Sensor *sensor) {
    if (!sensor) {
        ESP_LOGW(TAG, "Cannot add null temperature sensor: %s", name.c_str());
        return;
    }
    
    TempSensorMapping mapping;
    mapping.name = name;
    mapping.sensor = sensor;
    temp_sensors_.push_back(mapping);
    
    ESP_LOGD(TAG, "Added temperature sensor: %s", name.c_str());
}

void LEDBrickTemperatureControl::enable_temperature_control(bool enabled) {
    temp_control_.enable(enabled);
    ESP_LOGI(TAG, "Temperature control %s", enabled ? "enabled" : "disabled");
    
    // Update enable switch state
    if (enable_switch_) {
        enable_switch_->publish_state(enabled);
    }
}

void LEDBrickTemperatureControl::set_manual_target_temperature(float temp) {
    // Validate temperature range
    if (temp < 10.0f || temp > 80.0f) {
        ESP_LOGW(TAG, "Target temperature %.1f°C out of range (10-80°C), ignoring", temp);
        return;
    }
    
    config_.target_temp_c = temp;
    temp_control_.set_config(config_);
    
    ESP_LOGI(TAG, "Target temperature set to %.1f°C", temp);
    
    // Save to preferences
    save_config_();
    
    // Update target temperature sensor
    if (target_temp_sensor_) {
        target_temp_sensor_->publish_state(temp);
    }
}

void LEDBrickTemperatureControl::reset_pid() {
    temp_control_.reset_pid();
    ESP_LOGI(TAG, "PID controller reset");
}

void LEDBrickTemperatureControl::on_fan_pwm_change_(float pwm) {
    ESP_LOGD(TAG, "Setting fan PWM to %.1f%%", pwm);
    
    if (fan_) {
        ESP_LOGI(TAG, "Fan object exists, setting PWM to %.1f%%", pwm);
        if (pwm <= 0.001f) {
            // Turn off fan when PWM is 0
            ESP_LOGI(TAG, "Turning fan OFF");
            auto call = fan_->turn_off();
            call.perform();
        } else {
            // Turn on fan and set speed
            ESP_LOGI(TAG, "Turning fan ON with speed %.1f", pwm);
            auto call = fan_->turn_on();
            call.set_speed(pwm);  // ESPHome fan expects speed in range 0-100
            call.perform();
        }
    } else {
        ESP_LOGW(TAG, "Fan object is NULL - cannot control fan!");
    }
}

void LEDBrickTemperatureControl::on_fan_enable_change_(bool enabled) {
    ESP_LOGD(TAG, "Setting fan power to %s", enabled ? "ON" : "OFF");
    
    if (fan_power_switch_) {
        if (enabled) {
            fan_power_switch_->turn_on();
        } else {
            fan_power_switch_->turn_off();
        }
    }
    
    // Also ensure fan is off when power is disabled
    if (!enabled && fan_) {
        auto call = fan_->turn_off();
        call.perform();
    }
}

void LEDBrickTemperatureControl::on_emergency_change_(bool emergency) {
    if (emergency) {
        ESP_LOGW(TAG, "THERMAL EMERGENCY ACTIVATED!");
        
        // Signal scheduler to enter thermal emergency mode
        if (scheduler_) {
            scheduler_->set_thermal_emergency(true);
        }
    } else {
        ESP_LOGI(TAG, "Thermal emergency cleared");
        
        // Clear scheduler thermal emergency
        if (scheduler_) {
            scheduler_->set_thermal_emergency(false);
        }
    }
    
    // Update emergency sensor
    if (thermal_emergency_sensor_) {
        thermal_emergency_sensor_->publish_state(emergency);
    }
}

void LEDBrickTemperatureControl::update_temperature_sensors_() {
    uint32_t now = millis();
    
    for (const auto &mapping : temp_sensors_) {
        if (mapping.sensor && mapping.sensor->has_state()) {
            float temp = mapping.sensor->state;
            
            // Basic sanity check on temperature value
            if (temp > -50.0f && temp < 150.0f) {
                temp_control_.update_temperature_sensor(mapping.name, temp, now);
            } else {
                ESP_LOGW(TAG, "Invalid temperature reading from %s: %.1f°C", 
                        mapping.name.c_str(), temp);
            }
        }
    }
}

void LEDBrickTemperatureControl::update_fan_speed_() {
    if (fan_speed_sensor_ && fan_speed_sensor_->has_state()) {
        float rpm = fan_speed_sensor_->state;
        temp_control_.update_fan_rpm(rpm);
    }
}

void LEDBrickTemperatureControl::publish_sensor_values_() {
    auto status = temp_control_.get_status();
    
    // Publish current temperature
    if (current_temp_sensor_ && status.current_temp_c > -50.0f) {
        current_temp_sensor_->publish_state(status.current_temp_c);
    }
    
    // Publish target temperature
    if (target_temp_sensor_) {
        target_temp_sensor_->publish_state(status.target_temp_c);
    }
    
    // Publish fan PWM
    if (fan_pwm_sensor_) {
        fan_pwm_sensor_->publish_state(status.fan_pwm_percent);
    }
    
    // Publish PID error
    if (pid_error_sensor_) {
        pid_error_sensor_->publish_state(status.pid_error);
    }
    
    // Publish PID output
    if (pid_output_sensor_) {
        pid_output_sensor_->publish_state(status.pid_output);
    }
    
    // Publish thermal emergency state
    if (thermal_emergency_sensor_) {
        thermal_emergency_sensor_->publish_state(status.thermal_emergency);
    }
    
    // Publish fan enabled state
    if (fan_enabled_sensor_) {
        fan_enabled_sensor_->publish_state(status.fan_enabled);
    }
    
    // Update target temperature number control if value changed
    if (target_temp_number_ && 
        abs(target_temp_number_->state - status.target_temp_c) > 0.1f) {
        target_temp_number_->publish_state(status.target_temp_c);
    }
    
    // Update enable switch if needed
    if (enable_switch_ && enable_switch_->state != status.enabled) {
        enable_switch_->publish_state(status.enabled);
    }
}

bool LEDBrickTemperatureControl::import_config_json(const std::string& json) {
    if (!temp_control_.import_config_json(json)) {
        return false;
    }
    
    // Update our local config
    config_ = temp_control_.get_config();
    
    // Save to preferences
    save_config_();
    
    return true;
}

void LEDBrickTemperatureControl::save_config_() {
    
    // Create a simple hash of the config to detect changes
    uint32_t new_hash = 0;
    new_hash ^= static_cast<uint32_t>(config_.target_temp_c * 100);
    new_hash ^= static_cast<uint32_t>(config_.kp * 1000) << 8;
    new_hash ^= static_cast<uint32_t>(config_.ki * 10000) << 16;
    new_hash ^= static_cast<uint32_t>(config_.kd * 1000) << 24;
    new_hash ^= static_cast<uint32_t>(config_.min_fan_pwm);
    new_hash ^= static_cast<uint32_t>(config_.max_fan_pwm) << 8;
    new_hash ^= static_cast<uint32_t>(config_.emergency_temp_c * 100) << 16;
    new_hash ^= static_cast<uint32_t>(config_.recovery_temp_c * 100) << 24;
    
    // Only save if config has changed
    if (new_hash != config_hash_) {
        config_hash_ = new_hash;
        
        // Save config as JSON string
        std::string json_config = temp_control_.export_config_json();
        
        // ESPHome preferences can't store strings directly, so we'll store individual values
        global_preferences->sync();
        
        // Use multiple preference objects for different config values
        uint32_t base_hash = 0x54454D50;  // "TEMP" in hex
        auto pref_target = global_preferences->make_preference<float>(base_hash ^ 0x1000);
        auto pref_kp = global_preferences->make_preference<float>(base_hash ^ 0x1001);
        auto pref_ki = global_preferences->make_preference<float>(base_hash ^ 0x1002);
        auto pref_kd = global_preferences->make_preference<float>(base_hash ^ 0x1003);
        auto pref_min_fan = global_preferences->make_preference<float>(base_hash ^ 0x1004);
        auto pref_max_fan = global_preferences->make_preference<float>(base_hash ^ 0x1005);
        auto pref_emerg_temp = global_preferences->make_preference<float>(base_hash ^ 0x1006);
        auto pref_recov_temp = global_preferences->make_preference<float>(base_hash ^ 0x1007);
        auto pref_emerg_delay = global_preferences->make_preference<uint32_t>(base_hash ^ 0x1008);
        
        pref_target.save(&config_.target_temp_c);
        pref_kp.save(&config_.kp);
        pref_ki.save(&config_.ki);
        pref_kd.save(&config_.kd);
        pref_min_fan.save(&config_.min_fan_pwm);
        pref_max_fan.save(&config_.max_fan_pwm);
        pref_emerg_temp.save(&config_.emergency_temp_c);
        pref_recov_temp.save(&config_.recovery_temp_c);
        pref_emerg_delay.save(&config_.emergency_delay_ms);
        
        preferences_.save(&config_hash_);
        
        ESP_LOGI(TAG, "Saved temperature control configuration to preferences");
    }
}

void LEDBrickTemperatureControl::load_config_() {
    uint32_t saved_hash = 0;
    if (!preferences_.load(&saved_hash) || saved_hash == 0) {
        ESP_LOGI(TAG, "No saved temperature control configuration found, using defaults");
        return;
    }
    
    // Load individual config values
    uint32_t base_hash = 0x54454D50;  // "TEMP" in hex
    auto pref_target = global_preferences->make_preference<float>(base_hash ^ 0x1000);
    auto pref_kp = global_preferences->make_preference<float>(base_hash ^ 0x1001);
    auto pref_ki = global_preferences->make_preference<float>(base_hash ^ 0x1002);
    auto pref_kd = global_preferences->make_preference<float>(base_hash ^ 0x1003);
    auto pref_min_fan = global_preferences->make_preference<float>(base_hash ^ 0x1004);
    auto pref_max_fan = global_preferences->make_preference<float>(base_hash ^ 0x1005);
    auto pref_emerg_temp = global_preferences->make_preference<float>(base_hash ^ 0x1006);
    auto pref_recov_temp = global_preferences->make_preference<float>(base_hash ^ 0x1007);
    auto pref_emerg_delay = global_preferences->make_preference<uint32_t>(base_hash ^ 0x1008);
    
    // Load each value, keeping defaults if not found
    pref_target.load(&config_.target_temp_c);
    pref_kp.load(&config_.kp);
    pref_ki.load(&config_.ki);
    pref_kd.load(&config_.kd);
    pref_min_fan.load(&config_.min_fan_pwm);
    pref_max_fan.load(&config_.max_fan_pwm);
    pref_emerg_temp.load(&config_.emergency_temp_c);
    pref_recov_temp.load(&config_.recovery_temp_c);
    pref_emerg_delay.load(&config_.emergency_delay_ms);
    
    config_hash_ = saved_hash;
    
    ESP_LOGI(TAG, "Loaded temperature control configuration from preferences");
    ESP_LOGI(TAG, "  Target: %.1f°C, PID: %.2f/%.3f/%.2f", 
             config_.target_temp_c, config_.kp, config_.ki, config_.kd);
}

} // namespace ledbrick_temperature_control
} // namespace esphome