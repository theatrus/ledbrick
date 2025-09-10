#include "ledbrick_scheduler.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

// The standalone implementations are compiled separately by ESPHome
// No need to include the .cpp files here

namespace esphome {
namespace ledbrick_scheduler {

static const char *const TAG = "ledbrick_scheduler";

void LEDBrickScheduler::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LEDBrick Scheduler...");
  
  // Initialize the standalone scheduler
  scheduler_.set_num_channels(num_channels_);
  
  // Initialize the astronomical calculator with default location
  astro_calc_.set_location(latitude_, longitude_);
  astro_calc_.set_projection_settings(astronomical_projection_, time_shift_hours_, time_shift_minutes_);
  
  // Initialize persistent storage
  schedule_pref_ = global_preferences->make_preference<ScheduleStorage>(SCHEDULE_HASH);
  
  // Load schedule from flash storage (includes all settings in JSON)
  load_schedule_from_flash();
  
  // Initialize temperature control
  temp_control_.set_config(temp_config_);
  
  // Set up temperature control callbacks
  temp_control_.set_fan_pwm_callback([this](float pwm) { this->on_fan_pwm_change(pwm); });
  temp_control_.set_fan_enable_callback([this](bool enabled) { this->on_fan_enable_change(enabled); });
  temp_control_.set_emergency_callback([this](bool emergency) { this->on_emergency_change(emergency); });
  
  // Add temperature sensors to the control module
  for (const auto &mapping : temp_sensors_) {
    temp_control_.add_temperature_sensor(mapping.name);
  }
  
  // Enable temperature control by default
  temp_control_.enable(true);
  temp_control_initialized_ = true;
  
  // Try to get initial timezone from time source
  if (time_source_) {
    auto time = time_source_->now();
    if (time.is_valid()) {
      double detected_offset = static_cast<double>(time.timezone_offset()) / 3600.0;
      if (abs(detected_offset - timezone_offset_hours_) > 0.01) {
        ESP_LOGI(TAG, "Initial timezone offset detected: %.1fh (was %.1fh)", 
                 detected_offset, timezone_offset_hours_);
        timezone_offset_hours_ = detected_offset;
        astro_calc_.set_timezone_offset(timezone_offset_hours_);
      }
    }
  }
  
  // Load default astronomical schedule if no points exist
  if (scheduler_.is_schedule_empty()) {
    ESP_LOGI(TAG, "No saved schedule found, loading default preset");
    load_preset("default");
    save_schedule_to_flash();  // Save the default schedule
  }
  
  ESP_LOGI(TAG, "Schedule loaded - %zu points, %u channels, enabled: %s", 
           scheduler_.get_schedule_size(), num_channels_, enabled_ ? "true" : "false");
  
  // Update color sensors with initial values
  update_color_sensors();
  
  ESP_LOGCONFIG(TAG, "LEDBrick Scheduler setup complete");
  ESP_LOGCONFIG(TAG, "Temperature Control:");
  ESP_LOGCONFIG(TAG, "  Fan object: %s", fan_ ? "Connected" : "NOT CONNECTED");
  ESP_LOGCONFIG(TAG, "  Fan power switch: %s", fan_power_switch_ ? "Connected" : "NOT CONNECTED");
  ESP_LOGCONFIG(TAG, "  Temperature sensors: %zu", temp_sensors_.size());
  
  // Mark boot as complete and perform any deferred saves
  boot_complete_ = true;
  if (save_pending_) {
    ESP_LOGI(TAG, "Boot complete - performing deferred schedule save");
    save_pending_ = false;
    save_schedule_to_flash();
  }
}

void LEDBrickScheduler::update() {
  // ALWAYS check thermal emergency first, even if scheduler is disabled
  if (thermal_emergency_) {
    // During thermal emergency, continuously force all outputs to zero
    // This prevents manual control from setting unsafe values
    for (uint8_t channel = 0; channel < num_channels_; channel++) {
      force_channel_output(channel, 0.0f, 0.0f);
    }
    // Skip to temperature control update
  } else if (!enabled_) {
    ESP_LOGV(TAG, "Scheduler disabled, skipping schedule update");
    // When scheduler is disabled but not in emergency, we don't change outputs
    // This allows manual control to work
  } else {
    // Normal scheduler operation
    // Update timezone offset from time source if available
    update_timezone_from_time_source();
    
    // Update astronomical times for dynamic schedule points
    update_astronomical_times_for_scheduler();
    
    // Get current values from standalone scheduler and apply them
    uint16_t current_time = get_current_time_minutes();
    
    // Use astronomical interpolation if we have dynamic points
    auto values = scheduler_.get_values_at_time_with_astro(current_time, scheduler_.get_astronomical_times());
    ESP_LOGD(TAG, "Scheduler values at %02d:%02d - valid: %s, channels: %zu, schedule_points: %zu", 
             current_time / 60, current_time % 60, 
             values.valid ? "true" : "false", 
             values.pwm_values.size(),
             scheduler_.get_schedule_size());
             
    // Check if moon simulation is active
    if (scheduler_.get_moon_simulation().enabled && values.valid && values.pwm_values.size() > 0) {
      // Get moon visibility status
      auto moon_times = get_moon_rise_set_times();
      bool moon_visible = (moon_times.rise_minutes < moon_times.set_minutes) ?
                         (current_time >= moon_times.rise_minutes && current_time <= moon_times.set_minutes) :
                         (current_time >= moon_times.rise_minutes || current_time <= moon_times.set_minutes);
      
      // Check if channels are dark
      bool all_dark = true;
      for (size_t i = 0; i < values.pwm_values.size(); i++) {
        if (values.pwm_values[i] > 0.1f) {
          all_dark = false;
          break;
        }
      }
      
      ESP_LOGD(TAG, "Moon sim status - Enabled: yes, Moon visible: %s, Channels dark: %s, Phase: %.1f%%, Ch1: PWM=%.1f%%, Current=%.3fA", 
               moon_visible ? "yes" : "no",
               all_dark ? "yes" : "no",
               get_moon_phase() * 100.0f,
               values.pwm_values[0], values.current_values[0]);
    }
    
    if (values.valid) {
      apply_values(values);
    } else {
      ESP_LOGW(TAG, "Scheduler returned invalid values at time %02d:%02d, schedule has %zu points", 
               current_time / 60, current_time % 60, scheduler_.get_schedule_size());
    }
  }
  
  // Update temperature control
  uint32_t current_millis = millis();
  if (temp_control_initialized_ && current_millis - last_temp_update_ >= 1000) {  // Update every second
    last_temp_update_ = current_millis;
    
    // Update temperature sensors
    update_temperature_sensors();
    
    // Update fan speed
    update_fan_speed();
    
    // Run temperature control update
    temp_control_.update(current_millis);
    
    // Publish sensor values periodically (every 5 seconds)
    if (current_millis - last_sensor_publish_ >= 5000) {
      publish_temp_sensor_values();
      last_sensor_publish_ = current_millis;
    }
  }
  
  // Log status every 10 seconds (reduce log spam)
  static uint32_t last_log_time = 0;
  if (current_millis - last_log_time > 10000) {
    if (thermal_emergency_) {
      ESP_LOGD(TAG, "THERMAL EMERGENCY - All outputs forced to zero");
    } else if (!enabled_) {
      ESP_LOGD(TAG, "Scheduler disabled - manual control active");
    } else {
      uint16_t log_time = get_current_time_minutes();
      auto log_values = scheduler_.get_values_at_time_with_astro(log_time, scheduler_.get_astronomical_times());
      ESP_LOGD(TAG, "Scheduler active at %02u:%02u - Ch1: PWM=%.1f%%, Ch2: PWM=%.1f%%, Moon: %s", 
               log_time / 60, log_time % 60,
               log_values.pwm_values.size() > 0 ? log_values.pwm_values[0] : 0.0f,
               log_values.pwm_values.size() > 1 ? log_values.pwm_values[1] : 0.0f,
               scheduler_.get_moon_simulation().enabled ? "enabled" : "disabled");
      
      // Also log if we recently recovered from emergency
      static bool was_emergency = false;
      static uint32_t recovery_time = 0;
      if (was_emergency && !thermal_emergency_) {
        recovery_time = current_millis;
        ESP_LOGI(TAG, "Recently recovered from thermal emergency");
      }
      if (recovery_time > 0 && current_millis - recovery_time < 30000) {  // Log for 30 seconds after recovery
        ESP_LOGD(TAG, "Post-emergency recovery: %d seconds ago", (current_millis - recovery_time) / 1000);
      }
      was_emergency = thermal_emergency_;
    }
    last_log_time = current_millis;
  }
}

void LEDBrickScheduler::dump_config() {
  ESP_LOGCONFIG(TAG, "LEDBrick Scheduler:");
  ESP_LOGCONFIG(TAG, "  Channels: %u", num_channels_);
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", update_interval_);
  ESP_LOGCONFIG(TAG, "  Enabled: %s", enabled_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  PWM Scale: %.2f (%.0f%%)", pwm_scale_, pwm_scale_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Timezone: %s (UTC%+.1fh)", timezone_.c_str(), timezone_offset_hours_);
  ESP_LOGCONFIG(TAG, "  Location: %.4f°N, %.4f°W", latitude_, -longitude_);
  ESP_LOGCONFIG(TAG, "  Schedule Points: %zu", scheduler_.get_schedule_size());
  ESP_LOGCONFIG(TAG, "  Time Source: %s", time_source_ ? "CONFIGURED" : "NOT SET");
  
  if (astronomical_projection_) {
    ESP_LOGCONFIG(TAG, "  Astronomical Projection: ENABLED");
    ESP_LOGCONFIG(TAG, "  Time Shift: %+dh %+dm", time_shift_hours_, time_shift_minutes_);
  } else {
    ESP_LOGCONFIG(TAG, "  Astronomical Projection: DISABLED");
  }
  
  if (time_source_) {
    uint16_t current_time = get_current_time_minutes();
    ESP_LOGCONFIG(TAG, "  Current Local Time: %02u:%02u (%u minutes)", 
                  current_time / 60, current_time % 60, current_time);
  }
  
  // Temperature control configuration
  ESP_LOGCONFIG(TAG, "Temperature Control:");
  ESP_LOGCONFIG(TAG, "  Target Temperature: %.1f°C", temp_config_.target_temp_c);
  ESP_LOGCONFIG(TAG, "  PID Parameters: Kp=%.2f, Ki=%.3f, Kd=%.2f", temp_config_.kp, temp_config_.ki, temp_config_.kd);
  ESP_LOGCONFIG(TAG, "  Fan PWM Range: %.1f%% - %.1f%%", temp_config_.min_fan_pwm, temp_config_.max_fan_pwm);
  ESP_LOGCONFIG(TAG, "  Emergency Temperature: %.1f°C", temp_config_.emergency_temp_c);
  ESP_LOGCONFIG(TAG, "  Temperature Sensors: %zu", temp_sensors_.size());
}

void LEDBrickScheduler::add_schedule_point(const SchedulePoint &point) {
  scheduler_.add_schedule_point(point);
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGD(TAG, "Added schedule point at %02u:%02u", 
           point.time_minutes / 60, point.time_minutes % 60);
}

void LEDBrickScheduler::set_schedule_point(uint16_t time_minutes, const std::vector<float> &pwm_values, 
                                          const std::vector<float> &current_values) {
  scheduler_.set_schedule_point(time_minutes, pwm_values, current_values);
  
  // Save to flash automatically
  save_schedule_to_flash();
}

void LEDBrickScheduler::add_dynamic_schedule_point(LEDScheduler::DynamicTimeType type, int16_t offset_minutes,
                                                  const std::vector<float> &pwm_values,
                                                  const std::vector<float> &current_values) {
  scheduler_.add_dynamic_schedule_point(type, offset_minutes, pwm_values, current_values);
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGD(TAG, "Added dynamic schedule point: %s %+d minutes", 
           LEDScheduler::dynamic_time_type_to_string(type).c_str(), offset_minutes);
}

void LEDBrickScheduler::remove_schedule_point(uint16_t time_minutes) {
  scheduler_.remove_schedule_point(time_minutes);
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGD(TAG, "Removed schedule point at %02u:%02u", time_minutes / 60, time_minutes % 60);
}

void LEDBrickScheduler::remove_dynamic_schedule_point(LEDScheduler::DynamicTimeType type, int16_t offset_minutes) {
  scheduler_.remove_dynamic_schedule_point(type, offset_minutes);
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGD(TAG, "Removed dynamic schedule point: %s %+d minutes", 
           LEDScheduler::dynamic_time_type_to_string(type).c_str(), offset_minutes);
}

void LEDBrickScheduler::clear_schedule() {
  scheduler_.clear_schedule();
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGD(TAG, "Cleared all schedule points");
}

void LEDBrickScheduler::load_preset(const std::string &preset_name) {
  if (preset_name == "sunrise_sunset") {
    create_sunrise_sunset_preset_with_astro_data();
  } else {
    scheduler_.load_preset(preset_name);
  }
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGI(TAG, "Loaded preset '%s' with %zu points", preset_name.c_str(), scheduler_.get_schedule_size());
}

void LEDBrickScheduler::save_preset(const std::string &preset_name) {
  scheduler_.save_preset(preset_name);
  ESP_LOGI(TAG, "Saved current schedule as preset '%s'", preset_name.c_str());
}

uint16_t LEDBrickScheduler::get_current_time_minutes() const {
  if (!time_source_) {
    ESP_LOGW(TAG, "No time source configured");
    return 0;
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    ESP_LOGW(TAG, "Time source not available");
    return 0;
  }
  
  return time.hour * 60 + time.minute;
}

InterpolationResult LEDBrickScheduler::get_current_values() const {
  uint16_t current_time = get_current_time_minutes();
  return scheduler_.get_values_at_time(current_time);
}

InterpolationResult LEDBrickScheduler::get_actual_channel_values() const {
  InterpolationResult result;
  result.pwm_values.resize(num_channels_, 0.0f);
  result.current_values.resize(num_channels_, 0.0f);
  result.valid = true;
  
  // Get actual values from the ESPHome components
  for (uint8_t channel = 0; channel < num_channels_; channel++) {
    // Get PWM value from light entity
    auto light_it = lights_.find(channel);
    if (light_it != lights_.end() && light_it->second) {
      // Get actual current state (not the call values)
      auto remote_values = light_it->second->remote_values;
      // Check if light is on - if off, report 0% regardless of brightness setting
      if (remote_values.is_on()) {
        // Get brightness as percentage (0-100)
        result.pwm_values[channel] = remote_values.get_brightness() * 100.0f;
      } else {
        // Light is off, report 0%
        result.pwm_values[channel] = 0.0f;
      }
    }
    
    // Get current value from current control
    auto current_it = current_controls_.find(channel);
    if (current_it != current_controls_.end() && current_it->second) {
      result.current_values[channel] = current_it->second->state;
    }
  }
  
  return result;
}

void LEDBrickScheduler::set_pwm_scale(float scale) {
  if (scale < 0.0f) {
    scale = 0.0f;
  } else if (scale > 1.0f) {
    scale = 1.0f;
  }
  
  if (abs(pwm_scale_ - scale) > 0.001f) {
    pwm_scale_ = scale;
    ESP_LOGI(TAG, "PWM scale set to %.2f (%.0f%%)", pwm_scale_, pwm_scale_ * 100.0f);
    
    // Force immediate update to apply new scale
    update();
  }
}

void LEDBrickScheduler::create_sunrise_sunset_preset_with_astro_data() const {
  // Calculate actual sunrise and sunset times using astronomical calculator
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  auto sun_times = astro_calc_.get_projected_sun_rise_set_times(dt);
  
  // Use calculated times or fallback to defaults
  uint16_t sunrise_minutes = sun_times.rise_valid ? sun_times.rise_minutes : 420;  // Default 7:00 AM
  uint16_t sunset_minutes = sun_times.set_valid ? sun_times.set_minutes : 1080;    // Default 6:00 PM
  
  ESP_LOGI(TAG, "Creating astronomical sunrise/sunset preset: sunrise=%02u:%02u, sunset=%02u:%02u", 
           sunrise_minutes / 60, sunrise_minutes % 60,
           sunset_minutes / 60, sunset_minutes % 60);
  
  // Use the standalone scheduler to create the default astronomical preset
  // Note: The new default preset is dynamic and doesn't use specific sunrise/sunset times
  const_cast<LEDScheduler&>(scheduler_).create_default_astronomical_preset();
}

void LEDBrickScheduler::apply_values(const InterpolationResult &values) {
  static std::vector<float> last_pwm_values_(num_channels_, -1.0f);
  static std::vector<float> last_current_values_(num_channels_, -1.0f);
  
  ESP_LOGD(TAG, "apply_values called - num_channels: %u, values_size: %zu, first_pwm: %.1f%%", 
           num_channels_, values.pwm_values.size(), 
           values.pwm_values.empty() ? 0.0f : values.pwm_values[0]);
  
  for (uint8_t channel = 0; channel < num_channels_; channel++) {
    // Apply PWM to light entity
    auto light_it = lights_.find(channel);
    if (light_it != lights_.end() && light_it->second) {
      // Apply PWM scale factor before converting to 0-1 range
      float scaled_pwm = values.pwm_values[channel] * pwm_scale_;
      float brightness = scaled_pwm / 100.0f; // Convert percentage to 0-1
      
      // Only update if value has changed significantly (reduce unnecessary calls) or force update
      bool should_update = force_next_update_ ||
                          (last_pwm_values_.size() <= channel || 
                           abs(brightness - last_pwm_values_[channel]) > 0.001f);
      
      if (should_update) {
        ESP_LOGD(TAG, "Updating light %u: scaled_pwm=%.2f%%, brightness=%.3f (was %.3f)", 
                 channel, scaled_pwm, brightness, 
                 (last_pwm_values_.size() > channel) ? last_pwm_values_[channel] : -1.0f);
        
        // Create light call to set brightness
        auto call = light_it->second->make_call();
        call.set_state(brightness > 0.001f); // Turn on if brightness > 0
        if (brightness > 0.001f) {
          call.set_brightness(brightness);
        }
        call.set_transition_length(0);  // No transition for immediate response
        call.perform();
        
        ESP_LOGV(TAG, "Light %u call performed", channel);
        
        if (last_pwm_values_.size() <= channel) {
          last_pwm_values_.resize(num_channels_, -1.0f);
        }
        last_pwm_values_[channel] = brightness;
        
        ESP_LOGV(TAG, "Updated light %u brightness to %.3f (%.1f%% * %.2f scale = %.1f%%)", 
                 channel, brightness, values.pwm_values[channel], pwm_scale_, scaled_pwm);
      }
    } else {
      ESP_LOGVV(TAG, "No light entity found for channel %u", channel);
    }
    
    // Apply current control with limiting
    auto current_it = current_controls_.find(channel);
    auto max_current_it = max_current_controls_.find(channel);
    
    if (current_it != current_controls_.end() && current_it->second) {
      float target_current = values.current_values[channel];
      
      // Apply maximum current limiting
      if (max_current_it != max_current_controls_.end() && max_current_it->second) {
        float max_current = max_current_it->second->state;
        target_current = std::min(target_current, max_current);
      }
      
      // Only update if value has changed significantly or force update
      if (force_next_update_ ||
          last_current_values_.size() <= channel || 
          abs(target_current - last_current_values_[channel]) > 0.01f) {
        
        // Set the current control value
        current_it->second->publish_state(target_current);
        
        if (last_current_values_.size() <= channel) {
          last_current_values_.resize(num_channels_, -1.0f);
        }
        last_current_values_[channel] = target_current;
        
        // Enhanced logging for channel 1 when values change
        if (channel == 0) {
          ESP_LOGD(TAG, "Ch1 current update: %.3fA (input: %.3fA, max limit: %.3fA, was: %.3fA, forced: %s)", 
                   target_current, values.current_values[channel],
                   max_current_it != max_current_controls_.end() && max_current_it->second ? 
                   max_current_it->second->state : 999.0f,
                   last_current_values_.size() > channel ? last_current_values_[channel] : -1.0f,
                   force_next_update_ ? "yes" : "no");
        }
        
        ESP_LOGV(TAG, "Updated current %u to %.3fA (limited from %.3fA)", 
                 channel, target_current, values.current_values[channel]);
      }
    } else {
      ESP_LOGVV(TAG, "No current control found for channel %u", channel);
    }
  }
  
  // Clear force update flag after applying all values
  if (force_next_update_) {
    ESP_LOGD(TAG, "Force update completed - clearing flag");
    force_next_update_ = false;
  }
}


void LEDBrickScheduler::add_light(uint8_t channel, light::LightState *light) {
  lights_[channel] = light;
  ESP_LOGD(TAG, "Added light entity for channel %u", channel);
}

void LEDBrickScheduler::add_current_control(uint8_t channel, number::Number *control) {
  current_controls_[channel] = control;
  ESP_LOGD(TAG, "Added current control for channel %u", channel);
}

void LEDBrickScheduler::add_max_current_control(uint8_t channel, number::Number *control) {
  max_current_controls_[channel] = control;
  ESP_LOGD(TAG, "Added max current control for channel %u", channel);
}



void LEDBrickScheduler::save_schedule_to_flash() {
  // Prevent saving during boot to avoid race conditions
  if (!boot_complete_) {
    save_pending_ = true;
    ESP_LOGD(TAG, "Boot in progress - deferring schedule save");
    return;
  }
  
  // Export the complete schedule as JSON (includes moon settings, channel configs, etc.)
  std::string json_output;
  export_schedule_json(json_output);
  
  // Allocate storage on heap to avoid stack overflow
  std::unique_ptr<ScheduleStorage> storage(new (std::nothrow) ScheduleStorage);
  if (!storage) {
    ESP_LOGE(TAG, "Failed to allocate memory for schedule storage");
    return;
  }
  
  memset(storage.get(), 0, sizeof(ScheduleStorage));
  
  storage->version = 2;  // Version 2 uses JSON format
  storage->json_length = static_cast<uint32_t>(json_output.length());
  
  // Ensure it fits in our storage
  if (storage->json_length >= sizeof(storage->json_data) - 1) {
    ESP_LOGW(TAG, "Schedule JSON too large (%u bytes), truncating", storage->json_length);
    storage->json_length = sizeof(storage->json_data) - 1;
  }
  
  // Copy JSON data
  std::memcpy(storage->json_data, json_output.c_str(), storage->json_length);
  storage->json_data[storage->json_length] = '\0';  // Null terminate
  
  bool success = schedule_pref_.save(storage.get());
  if (success) {
    ESP_LOGD(TAG, "Saved schedule to flash (JSON format, %u bytes)", storage->json_length);
  } else {
    ESP_LOGW(TAG, "Failed to save schedule to flash");
  }
  // unique_ptr automatically deletes
}

void LEDBrickScheduler::load_schedule_from_flash() {
  // Allocate storage on heap to avoid stack overflow
  std::unique_ptr<ScheduleStorage> storage(new (std::nothrow) ScheduleStorage);
  if (!storage) {
    ESP_LOGE(TAG, "Failed to allocate memory for schedule storage");
    return;
  }
  
  bool loaded = schedule_pref_.load(storage.get());
  if (!loaded) {
    ESP_LOGD(TAG, "No saved schedule found in flash");
    return;
  }
  
  // Check version
  if (storage->version == 2) {
    // Version 2: JSON format
    if (storage->json_length > 0 && storage->json_length < sizeof(storage->json_data)) {
      storage->json_data[storage->json_length] = '\0';  // Ensure null terminated
      std::string json_input(storage->json_data);
      
      ESP_LOGD(TAG, "Loading schedule from flash (JSON format, %u bytes)", storage->json_length);
      
      if (import_schedule_json(json_input)) {
        ESP_LOGI(TAG, "Successfully loaded schedule from flash (JSON format)");
        ESP_LOGI(TAG, "Loaded: %zu points, Moon: %s, Location: %.4f,%.4f", 
                 scheduler_.get_schedule_size(),
                 scheduler_.get_moon_simulation().enabled ? "ON" : "OFF",
                 latitude_, longitude_);
      } else {
        ESP_LOGW(TAG, "Failed to import schedule JSON from flash");
      }
    }
  } else if (storage->version == 0 || storage->version == 1) {
    // Legacy format - try to migrate
    ESP_LOGW(TAG, "Found legacy schedule format in flash, migration not supported");
  } else {
    ESP_LOGW(TAG, "Unknown schedule storage version %u", storage->version);
  }
  // unique_ptr automatically deletes
}

void LEDBrickScheduler::export_schedule_json(std::string &json_output) const {
  // If astronomical projection is enabled, we need to update the scheduler's
  // astronomical times with projected values before exporting
  if (astronomical_projection_) {
    // Get current time for calculations
    auto dt = esphome_time_to_datetime();
    
    // Get projected sun times (with time shift applied)
    auto projected_sun_times = astro_calc_.get_projected_sun_rise_set_times(dt);
    
    // Calculate solar noon from projected times
    uint16_t solar_noon = 720;  // Default to noon
    if (projected_sun_times.rise_valid && projected_sun_times.set_valid) {
      // Handle case where sunset happens after midnight
      int rise_mins = static_cast<int>(projected_sun_times.rise_minutes);
      int set_mins = static_cast<int>(projected_sun_times.set_minutes);
      
      // If sunset is before sunrise, it means sunset is on the next day
      if (set_mins < rise_mins) {
        set_mins += 1440;  // Add 24 hours
      }
      
      solar_noon = (rise_mins + set_mins) / 2;
      
      // Wrap back to 0-1439 range
      while (solar_noon >= 1440) {
        solar_noon -= 1440;
      }
    }
    
    // Calculate civil twilight times
    uint16_t civil_dawn = projected_sun_times.rise_valid ? 
      (projected_sun_times.rise_minutes > 30 ? projected_sun_times.rise_minutes - 30 : 0) : 390;
    uint16_t civil_dusk = projected_sun_times.set_valid ? 
      (projected_sun_times.set_minutes < 1410 ? projected_sun_times.set_minutes + 30 : 1439) : 1110;
    
    // Build projected astronomical times
    LEDScheduler::AstronomicalTimes projected_times;
    projected_times.sunrise_minutes = projected_sun_times.rise_valid ? projected_sun_times.rise_minutes : 420;
    projected_times.sunset_minutes = projected_sun_times.set_valid ? projected_sun_times.set_minutes : 1080;
    projected_times.solar_noon_minutes = solar_noon;
    projected_times.civil_dawn_minutes = civil_dawn;
    projected_times.civil_dusk_minutes = civil_dusk;
    projected_times.nautical_dawn_minutes = civil_dawn > 30 ? civil_dawn - 30 : 0;
    projected_times.nautical_dusk_minutes = civil_dusk < 1410 ? civil_dusk + 30 : 1439;
    projected_times.astronomical_dawn_minutes = civil_dawn > 60 ? civil_dawn - 60 : 0;
    projected_times.astronomical_dusk_minutes = civil_dusk < 1380 ? civil_dusk + 60 : 1439;
    projected_times.valid = true;
    
    // Get current astronomical times before modification
    LEDScheduler::AstronomicalTimes original_times = scheduler_.get_astronomical_times();
    
    // Temporarily set projected times for export
    const_cast<LEDScheduler&>(scheduler_).set_astronomical_times(projected_times);
    
    // Export with projected times
    json_output = scheduler_.export_json();
    
    // Restore original astronomical times to prevent moon simulation corruption
    const_cast<LEDScheduler&>(scheduler_).set_astronomical_times(original_times);
  } else {
    // Use normal export without projection
    json_output = scheduler_.export_json();
  }
  
  // Parse and modify the JSON to add ESPHome-specific fields
  // For simplicity, we'll create a new JSON string
  uint16_t current_time = get_current_time_minutes();
  auto current_values = get_current_values();
  
  size_t closing_brace = json_output.rfind('}');
  if (closing_brace != std::string::npos) {
    // Add temperature control configuration
    std::string temp_config_json = temp_control_.export_config_json();
    
    json_output.insert(closing_brace, 
      ",\"timezone\":\"" + timezone_ + 
      "\",\"timezone_offset_hours\":" + std::to_string(timezone_offset_hours_) +
      ",\"current_time_minutes\":" + std::to_string(current_time) +
      ",\"enabled\":" + std::string(enabled_ ? "true" : "false") +
      ",\"latitude\":" + std::to_string(latitude_) +
      ",\"longitude\":" + std::to_string(longitude_) +
      ",\"astronomical_projection\":" + std::string(astronomical_projection_ ? "true" : "false") +
      ",\"time_shift_hours\":" + std::to_string(time_shift_hours_) +
      ",\"time_shift_minutes\":" + std::to_string(time_shift_minutes_) +
      ",\"temperature_control\":" + temp_config_json);
  }
}

bool LEDBrickScheduler::import_schedule_json(const std::string &json_input) {
  ESP_LOGI(TAG, "Importing schedule from JSON (%zu chars)", json_input.length());
  
  // Extract enabled state from ESPHome-specific fields
  size_t enabled_pos = json_input.find("\"enabled\":");
  if (enabled_pos != std::string::npos) {
    size_t value_start = json_input.find_first_not_of(" \t", enabled_pos + 10);
    if (value_start != std::string::npos) {
      enabled_ = json_input.substr(value_start, 4) == "true";
    }
  }
  
  // Extract location if present
  size_t lat_pos = json_input.find("\"latitude\":");
  if (lat_pos != std::string::npos) {
    size_t lat_start = json_input.find_first_of("-0123456789.", lat_pos + 11);
    size_t lat_end = json_input.find_first_not_of("-0123456789.", lat_start);
    if (lat_start != std::string::npos && lat_end != std::string::npos) {
      double lat = std::stod(json_input.substr(lat_start, lat_end - lat_start));
      if (lat >= -90.0 && lat <= 90.0) {
        latitude_ = lat;
      }
    }
  }
  
  size_t lon_pos = json_input.find("\"longitude\":");
  if (lon_pos != std::string::npos) {
    size_t lon_start = json_input.find_first_of("-0123456789.", lon_pos + 12);
    size_t lon_end = json_input.find_first_not_of("-0123456789.", lon_start);
    if (lon_start != std::string::npos && lon_end != std::string::npos) {
      double lon = std::stod(json_input.substr(lon_start, lon_end - lon_start));
      if (lon >= -180.0 && lon <= 180.0) {
        longitude_ = lon;
      }
    }
  }
  
  // Extract time shift settings
  size_t proj_pos = json_input.find("\"astronomical_projection\":");
  if (proj_pos != std::string::npos) {
    size_t value_start = json_input.find_first_not_of(" \t", proj_pos + 26);
    if (value_start != std::string::npos) {
      astronomical_projection_ = json_input.substr(value_start, 4) == "true";
    }
  }
  
  size_t hours_pos = json_input.find("\"time_shift_hours\":");
  if (hours_pos != std::string::npos) {
    size_t hours_start = json_input.find_first_of("-0123456789", hours_pos + 19);
    size_t hours_end = json_input.find_first_not_of("-0123456789", hours_start);
    if (hours_start != std::string::npos && hours_end != std::string::npos) {
      time_shift_hours_ = std::stoi(json_input.substr(hours_start, hours_end - hours_start));
    }
  }
  
  size_t mins_pos = json_input.find("\"time_shift_minutes\":");
  if (mins_pos != std::string::npos) {
    size_t mins_start = json_input.find_first_of("-0123456789", mins_pos + 21);
    size_t mins_end = json_input.find_first_not_of("-0123456789", mins_start);
    if (mins_start != std::string::npos && mins_end != std::string::npos) {
      time_shift_minutes_ = std::stoi(json_input.substr(mins_start, mins_end - mins_start));
    }
  }
  
  // Extract timezone offset if present
  size_t tz_offset_pos = json_input.find("\"timezone_offset_hours\":");
  if (tz_offset_pos != std::string::npos) {
    size_t offset_start = json_input.find_first_of("-0123456789.", tz_offset_pos + 24);
    size_t offset_end = json_input.find_first_not_of("-0123456789.", offset_start);
    if (offset_start != std::string::npos && offset_end != std::string::npos) {
      timezone_offset_hours_ = std::stod(json_input.substr(offset_start, offset_end - offset_start));
    }
  }
  
  // Extract temperature control configuration if present
  size_t temp_config_pos = json_input.find("\"temperature_control\":");
  if (temp_config_pos != std::string::npos) {
    // Find the opening brace for the temperature_control object
    size_t temp_start = json_input.find('{', temp_config_pos + 22);
    if (temp_start != std::string::npos) {
      // Find the matching closing brace
      int brace_depth = 1;
      size_t pos = temp_start + 1;
      while (pos < json_input.length() && brace_depth > 0) {
        if (json_input[pos] == '{') {
          brace_depth++;
        } else if (json_input[pos] == '}') {
          brace_depth--;
        }
        pos++;
      }
      
      if (brace_depth == 0) {
        std::string temp_config_json = json_input.substr(temp_start, pos - temp_start);
        temp_control_.import_config_json(temp_config_json);
        temp_config_ = temp_control_.get_config();
        ESP_LOGI(TAG, "Imported temperature control configuration");
      }
    }
  }
  
  // Use standalone scheduler's JSON import
  bool success = scheduler_.import_json(json_input);
  
  if (success) {
    // Update astronomical calculator with loaded settings (without saving during import)
    astro_calc_.set_location(latitude_, longitude_);
    astro_calc_.set_timezone_offset(timezone_offset_hours_);
    astro_calc_.set_projection_settings(astronomical_projection_, time_shift_hours_, time_shift_minutes_);
    
    // Don't save after import - we just loaded from flash, saving is redundant and causes race conditions
    ESP_LOGI(TAG, "Successfully imported %zu schedule points, enabled=%s", 
             scheduler_.get_schedule_size(), enabled_ ? "true" : "false");
    ESP_LOGI(TAG, "Location: %.4f, %.4f; Time shift: %s %+d:%02d",
             latitude_, longitude_, 
             astronomical_projection_ ? "enabled" : "disabled",
             time_shift_hours_, abs(time_shift_minutes_));
    
    // Update max current controls from imported channel configs
    for (uint8_t ch = 0; ch < num_channels_; ch++) {
      auto config = scheduler_.get_channel_config(ch);
      auto max_current_it = max_current_controls_.find(ch);
      if (max_current_it != max_current_controls_.end() && max_current_it->second) {
        max_current_it->second->publish_state(config.max_current);
        ESP_LOGD(TAG, "Updated channel %u max current to %.2fA", ch, config.max_current);
      }
    }
    
    // Force update to refresh text sensors and color sensors
    update();
    update_color_sensors();
    
    ESP_LOGI(TAG, "Channel configurations updated: Ch0 color=%s", 
             scheduler_.get_channel_color(0).c_str());
  } else {
    ESP_LOGW(TAG, "Failed to import JSON schedule");
  }
  
  return success;
}


float LEDBrickScheduler::get_moon_phase() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_moon_phase(dt);
}

AstronomicalCalculator::MoonTimes LEDBrickScheduler::get_moon_rise_set_times() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  // Use projected times if projection is enabled
  if (is_astronomical_projection_enabled()) {
    return astro_calc_.get_projected_moon_rise_set_times(dt);
  }
  return astro_calc_.get_moon_rise_set_times(dt);
}

AstronomicalCalculator::SunTimes LEDBrickScheduler::get_sun_rise_set_times() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_sun_rise_set_times(dt);
}

AstronomicalCalculator::SunTimes LEDBrickScheduler::get_projected_sun_rise_set_times() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_projected_sun_rise_set_times(dt);
}



float LEDBrickScheduler::get_moon_intensity() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_moon_intensity(dt);
}

float LEDBrickScheduler::get_sun_intensity() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_sun_intensity(dt);
}


float LEDBrickScheduler::get_projected_sun_intensity() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_projected_sun_intensity(dt);
}

float LEDBrickScheduler::get_projected_moon_intensity() const {
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  return astro_calc_.get_projected_moon_intensity(dt);
}

AstronomicalCalculator::DateTime LEDBrickScheduler::esphome_time_to_datetime() const {
  if (!time_source_) {
    // Return a default time if no time source
    return AstronomicalCalculator::DateTime(2025, 1, 8, 12, 0, 0);
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    // Return a default time if invalid
    return AstronomicalCalculator::DateTime(2025, 1, 8, 12, 0, 0);
  }
  
  return AstronomicalCalculator::DateTime(
    time.year, time.month, time.day_of_month, 
    time.hour, time.minute, time.second
  );
}

void LEDBrickScheduler::update_astro_calculator_settings() const {
  // Update location, timezone, and projection settings in the calculator
  astro_calc_.set_location(latitude_, longitude_);
  astro_calc_.set_timezone_offset(timezone_offset_hours_);
  astro_calc_.set_projection_settings(astronomical_projection_, time_shift_hours_, time_shift_minutes_);
}

void LEDBrickScheduler::update_timezone_from_time_source() {
  static uint32_t last_tz_update = 0;
  uint32_t current_millis = millis();
  
  // Only check every 60 seconds to reduce overhead
  if (current_millis - last_tz_update < 60000 && last_tz_update != 0) {
    return;
  }
  
  last_tz_update = current_millis;
  
  if (!time_source_) {
    return;
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    return;
  }
  
  // ESPHome's ESPTime struct has a UTC offset in seconds
  // Convert to hours for our astronomical calculator
  double new_timezone_offset = static_cast<double>(time.timezone_offset()) / 3600.0;
  
  // Check if timezone offset has changed
  if (abs(new_timezone_offset - timezone_offset_hours_) > 0.01) {
    ESP_LOGI(TAG, "Timezone offset updated from %.1fh to %.1fh", 
             timezone_offset_hours_, new_timezone_offset);
    timezone_offset_hours_ = new_timezone_offset;
    
    // Update the astronomical calculator with new timezone
    astro_calc_.set_timezone_offset(timezone_offset_hours_);
    
    // Force recalculation of astronomical times
    update_astronomical_times_for_scheduler();
    
    // Save the new timezone offset
    save_schedule_to_flash();
  }
}

void LEDBrickScheduler::set_location(double latitude, double longitude) {
  // Check if location actually changed
  if (abs(latitude_ - latitude) < 0.0001 && abs(longitude_ - longitude) < 0.0001) {
    return;  // No change, skip save
  }
  
  latitude_ = latitude;
  longitude_ = longitude;
  
  // Update astronomical calculator
  astro_calc_.set_location(latitude_, longitude_);
  
  // Force immediate recalculation of astronomical times for dynamic schedule points
  update_astronomical_times_for_scheduler(true);
  
  // Force next update to re-evaluate schedule with new astronomical times
  force_next_update_ = true;
  
  // Save complete schedule (includes all settings)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Location updated to %.4f, %.4f and saved", latitude, longitude);
}

void LEDBrickScheduler::set_astronomical_projection(bool enabled) {
  // Check if value actually changed
  if (astronomical_projection_ == enabled) {
    return;  // No change, skip save
  }
  
  astronomical_projection_ = enabled;
  astro_calc_.set_projection_settings(astronomical_projection_, time_shift_hours_, time_shift_minutes_);
  
  // Force immediate recalculation of astronomical times for dynamic schedule points
  update_astronomical_times_for_scheduler(true);
  
  // Force next update to re-evaluate schedule with new astronomical times
  force_next_update_ = true;
  
  // Save complete schedule (includes all settings)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Astronomical projection %s and saved", enabled ? "enabled" : "disabled");
}

void LEDBrickScheduler::set_time_shift(int hours, int minutes) {
  // Check if values actually changed
  if (time_shift_hours_ == hours && time_shift_minutes_ == minutes) {
    return;  // No change, skip save
  }
  
  time_shift_hours_ = hours;
  time_shift_minutes_ = minutes;
  astro_calc_.set_projection_settings(astronomical_projection_, time_shift_hours_, time_shift_minutes_);
  
  // Force immediate recalculation of astronomical times for dynamic schedule points
  update_astronomical_times_for_scheduler(true);
  
  // Force next update to re-evaluate schedule with new astronomical times
  force_next_update_ = true;
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Time shift updated to %+d:%02d and saved", hours, abs(minutes));
}

void LEDBrickScheduler::set_channel_color(uint8_t channel, const std::string& rgb_hex) {
  // Check if value actually changed
  std::string current_color = scheduler_.get_channel_color(channel);
  if (current_color == rgb_hex) {
    return;  // No change, skip save
  }
  
  scheduler_.set_channel_color(channel, rgb_hex);
  
  // Update the color text sensor for this channel
  auto color_sensor_it = color_text_sensors_.find(channel);
  if (color_sensor_it != color_text_sensors_.end() && color_sensor_it->second) {
    color_sensor_it->second->publish_state(rgb_hex);
  }
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Channel %u color updated to %s and saved", channel, rgb_hex.c_str());
}

void LEDBrickScheduler::set_channel_max_current(uint8_t channel, float max_current) {
  // Check if value actually changed
  float current_max = scheduler_.get_channel_max_current(channel);
  if (abs(current_max - max_current) < 0.01f) {
    return;  // No change, skip save
  }
  
  scheduler_.set_channel_max_current(channel, max_current);
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Channel %u max current updated to %.2fA and saved", channel, max_current);
}

void LEDBrickScheduler::add_color_text_sensor(uint8_t channel, text_sensor::TextSensor *sensor) {
  color_text_sensors_[channel] = sensor;
  ESP_LOGD(TAG, "Added color text sensor for channel %u", channel);
}

void LEDBrickScheduler::update_color_sensors() {
  static std::map<uint8_t, std::string> last_colors_;
  
  for (auto& pair : color_text_sensors_) {
    uint8_t channel = pair.first;
    text_sensor::TextSensor* sensor = pair.second;
    if (sensor) {
      std::string color = scheduler_.get_channel_color(channel);
      
      // Only update if color has changed
      auto last_it = last_colors_.find(channel);
      if (last_it == last_colors_.end() || last_it->second != color) {
        sensor->publish_state(color);
        last_colors_[channel] = color;
        ESP_LOGD(TAG, "Updated color sensor for channel %u: %s", channel, color.c_str());
      }
    }
  }
}

void LEDBrickScheduler::enable_moon_simulation(bool enabled) {
  // Check if value actually changed
  if (scheduler_.get_moon_simulation().enabled == enabled) {
    return;  // No change, skip save
  }
  
  scheduler_.enable_moon_simulation(enabled);
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Moon simulation %s and saved", enabled ? "enabled" : "disabled");
}

void LEDBrickScheduler::set_moon_base_intensity(const std::vector<float>& intensity) {
  // Check if values actually changed
  auto current_moon = scheduler_.get_moon_simulation();
  if (current_moon.base_intensity.size() == intensity.size()) {
    bool changed = false;
    for (size_t i = 0; i < intensity.size(); i++) {
      if (abs(current_moon.base_intensity[i] - intensity[i]) > 0.01f) {
        changed = true;
        break;
      }
    }
    if (!changed) {
      return;  // No change, skip save
    }
  }
  
  scheduler_.set_moon_base_intensity(intensity);
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Moon base intensity updated and saved");
}

void LEDBrickScheduler::set_moon_base_current(const std::vector<float>& current) {
  // Check if values actually changed
  auto current_moon = scheduler_.get_moon_simulation();
  if (current_moon.base_current.size() == current.size()) {
    bool changed = false;
    for (size_t i = 0; i < current.size(); i++) {
      if (abs(current_moon.base_current[i] - current[i]) > 0.01f) {
        changed = true;
        break;
      }
    }
    if (!changed) return;  // No change, skip save
  }
  
  scheduler_.set_moon_base_current(current);
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Moon base current updated and saved");
}

void LEDBrickScheduler::set_moon_simulation(const LEDScheduler::MoonSimulation& config) {
  // Check if values actually changed  
  auto current_moon = scheduler_.get_moon_simulation();
  
  ESP_LOGD(TAG, "Current moon: enabled=%d, pwm_scale=%d, curr_scale=%d, min_curr=%.3f",
           current_moon.enabled, current_moon.phase_scaling_pwm, 
           current_moon.phase_scaling_current, current_moon.min_current_threshold);
  ESP_LOGD(TAG, "New moon: enabled=%d, pwm_scale=%d, curr_scale=%d, min_curr=%.3f",
           config.enabled, config.phase_scaling_pwm, 
           config.phase_scaling_current, config.min_current_threshold);
  if (current_moon.enabled == config.enabled && 
      current_moon.phase_scaling_pwm == config.phase_scaling_pwm &&
      current_moon.phase_scaling_current == config.phase_scaling_current &&
      abs(current_moon.min_current_threshold - config.min_current_threshold) < 0.001f &&
      current_moon.base_intensity.size() == config.base_intensity.size() &&
      current_moon.base_current.size() == config.base_current.size()) {
    bool changed = false;
    
    // Check intensity values
    for (size_t i = 0; i < config.base_intensity.size(); i++) {
      if (abs(current_moon.base_intensity[i] - config.base_intensity[i]) > 0.01f) {
        changed = true;
        break;
      }
    }
    
    // Check current values
    if (!changed) {
      for (size_t i = 0; i < config.base_current.size(); i++) {
        if (abs(current_moon.base_current[i] - config.base_current[i]) > 0.001f) {
          changed = true;
          break;
        }
      }
    }
    
    if (!changed) {
      return;  // No change, skip save
    }
  }
  
  scheduler_.set_moon_simulation(config);
  
  // Save to persistent storage (scheduler JSON)
  save_schedule_to_flash();
  ESP_LOGI(TAG, "Moon simulation configuration updated and saved");
}

void LEDBrickScheduler::update_astronomical_times_for_scheduler(bool force) {
  static uint32_t last_astro_update = 0;
  uint32_t current_millis = millis();
  
  // Update astronomical times every 5 minutes (300000 ms) unless forced
  if (!force && current_millis - last_astro_update < 300000 && last_astro_update != 0) {
    return; // Skip update if less than 5 minutes have passed
  }
  
  last_astro_update = current_millis;
  
  // Calculate current astronomical times
  update_astro_calculator_settings();
  auto dt = esphome_time_to_datetime();
  
  // Get sunrise/sunset times (with projection if enabled)
  auto sun_times = astronomical_projection_ ? 
    astro_calc_.get_projected_sun_rise_set_times(dt) : 
    astro_calc_.get_sun_rise_set_times(dt);
  
  // Calculate solar noon (midpoint between sunrise and sunset)
  uint16_t solar_noon = 720; // Default noon
  if (sun_times.rise_valid && sun_times.set_valid) {
    // Handle case where sunset happens after midnight
    int rise_mins = static_cast<int>(sun_times.rise_minutes);
    int set_mins = static_cast<int>(sun_times.set_minutes);
    
    // If sunset is before sunrise, it means sunset is on the next day
    if (set_mins < rise_mins) {
      set_mins += 1440;  // Add 24 hours
    }
    
    solar_noon = (rise_mins + set_mins) / 2;
    
    // Wrap back to 0-1439 range
    while (solar_noon >= 1440) {
      solar_noon -= 1440;
    }
  }
  
  // Calculate civil twilight times (sun 6° below horizon)
  // For now, approximate as 30 minutes before/after sunrise/sunset
  uint16_t civil_dawn = sun_times.rise_valid ? 
    (sun_times.rise_minutes > 30 ? sun_times.rise_minutes - 30 : 0) : 390;
  uint16_t civil_dusk = sun_times.set_valid ? 
    (sun_times.set_minutes < 1410 ? sun_times.set_minutes + 30 : 1439) : 1110;
  
  // Build astronomical times structure
  LEDScheduler::AstronomicalTimes astro_times;
  astro_times.sunrise_minutes = sun_times.rise_valid ? sun_times.rise_minutes : 420;
  astro_times.sunset_minutes = sun_times.set_valid ? sun_times.set_minutes : 1080;
  astro_times.solar_noon_minutes = solar_noon;
  astro_times.civil_dawn_minutes = civil_dawn;
  astro_times.civil_dusk_minutes = civil_dusk;
  
  // For nautical and astronomical twilight, use further approximations
  astro_times.nautical_dawn_minutes = civil_dawn > 30 ? civil_dawn - 30 : 0;
  astro_times.nautical_dusk_minutes = civil_dusk < 1410 ? civil_dusk + 30 : 1439;
  astro_times.astronomical_dawn_minutes = civil_dawn > 60 ? civil_dawn - 60 : 0;
  astro_times.astronomical_dusk_minutes = civil_dusk < 1380 ? civil_dusk + 60 : 1439;
  
  // Get moon data (with projection if enabled)
  auto moon_times = astronomical_projection_ ?
    astro_calc_.get_projected_moon_rise_set_times(dt) :
    astro_calc_.get_moon_rise_set_times(dt);
  float moon_phase = astro_calc_.get_moon_phase(dt);
  
  astro_times.moonrise_minutes = moon_times.rise_valid ? moon_times.rise_minutes : 0;
  astro_times.moonset_minutes = moon_times.set_valid ? moon_times.set_minutes : 0;
  astro_times.moon_phase = moon_phase;
  astro_times.valid = true;
  
  // Update the scheduler with new astronomical times
  scheduler_.set_astronomical_times(astro_times);
  
  ESP_LOGD(TAG, "Updated astronomical times - Sunrise: %02u:%02u, Sunset: %02u:%02u, Solar Noon: %02u:%02u",
           astro_times.sunrise_minutes / 60, astro_times.sunrise_minutes % 60,
           astro_times.sunset_minutes / 60, astro_times.sunset_minutes % 60,
           astro_times.solar_noon_minutes / 60, astro_times.solar_noon_minutes % 60);
  
  if (moon_times.rise_valid || moon_times.set_valid) {
    ESP_LOGD(TAG, "Moon data - Rise: %02u:%02u, Set: %02u:%02u, Phase: %.1f%%",
             astro_times.moonrise_minutes / 60, astro_times.moonrise_minutes % 60,
             astro_times.moonset_minutes / 60, astro_times.moonset_minutes % 60,
             moon_phase * 100.0f);
  }
}

LEDBrickScheduler::SchedulePointInfo LEDBrickScheduler::get_schedule_point_info(size_t index) const {
  SchedulePointInfo info;
  
  if (index >= scheduler_.get_schedule_size()) {
    info.time_type = "invalid";
    info.offset_minutes = 0;
    info.time_minutes = 0;
    return info;
  }
  
  // Get the raw schedule point data
  auto points = scheduler_.get_schedule_points();
  if (index >= points.size()) {
    info.time_type = "invalid";
    info.offset_minutes = 0;
    info.time_minutes = 0;
    return info;
  }
  
  const auto& point = points[index];
  
  // Convert time type enum to string
  switch (point.time_type) {
    case LEDScheduler::DynamicTimeType::FIXED:
      info.time_type = "fixed";
      break;
    case LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE:
      info.time_type = "sunrise_relative";
      break;
    case LEDScheduler::DynamicTimeType::SUNSET_RELATIVE:
      info.time_type = "sunset_relative";
      break;
    case LEDScheduler::DynamicTimeType::SOLAR_NOON:
      info.time_type = "solar_noon";
      break;
    case LEDScheduler::DynamicTimeType::CIVIL_DAWN:
      info.time_type = "civil_dawn";
      break;
    case LEDScheduler::DynamicTimeType::CIVIL_DUSK:
      info.time_type = "civil_dusk";
      break;
    case LEDScheduler::DynamicTimeType::NAUTICAL_DAWN:
      info.time_type = "nautical_dawn";
      break;
    case LEDScheduler::DynamicTimeType::NAUTICAL_DUSK:
      info.time_type = "nautical_dusk";
      break;
    default:
      info.time_type = "unknown";
  }
  
  info.offset_minutes = point.offset_minutes;
  info.time_minutes = point.time_minutes;
  
  return info;
}

void LEDBrickScheduler::set_channel_manual_control(uint8_t channel, float pwm, float current) {
  // Only allow manual control when scheduler is disabled
  if (enabled_) {
    ESP_LOGW(TAG, "Manual control rejected - scheduler is enabled");
    return;
  }
  
  if (channel >= num_channels_) {
    ESP_LOGW(TAG, "Invalid channel %u for manual control", channel);
    return;
  }
  
  ESP_LOGI(TAG, "Setting manual control for channel %u: PWM=%.1f%%, Current=%.2fA", 
           channel, pwm, current);
  
  // Set light brightness (PWM)
  auto light_it = lights_.find(channel);
  if (light_it != lights_.end() && light_it->second) {
    if (pwm <= 0.001f) {
      // Turn off light when PWM is 0
      auto call = light_it->second->turn_off();
      call.perform();
    } else {
      // Turn on and set brightness
      auto call = light_it->second->turn_on();
      call.set_brightness(pwm / 100.0f);  // Convert percentage to 0-1
      call.perform();
    }
  }
  
  // Set current control
  auto current_it = current_controls_.find(channel);
  if (current_it != current_controls_.end() && current_it->second) {
    // Apply max current limiting
    auto max_current_it = max_current_controls_.find(channel);
    if (max_current_it != max_current_controls_.end() && max_current_it->second) {
      float max_current = max_current_it->second->state;
      current = std::min(current, max_current);
    }
    current_it->second->publish_state(current);
  }
}

void LEDBrickScheduler::set_thermal_emergency(bool emergency) {
  if (thermal_emergency_ == emergency) {
    return; // No change
  }
  
  thermal_emergency_ = emergency;
  
  if (emergency) {
    ESP_LOGW(TAG, "THERMAL EMERGENCY ACTIVATED - Forcing all channels to 0%%/0A");
    
    // Force all channels to zero PWM and current immediately
    for (uint8_t channel = 0; channel < num_channels_; channel++) {
      force_channel_output(channel, 0.0f, 0.0f);
    }
  } else {
    ESP_LOGI(TAG, "Thermal emergency cleared - Normal operation resumed");
  }
}

void LEDBrickScheduler::force_channel_output(uint8_t channel, float pwm, float current) {
  // Force specific PWM and current values regardless of schedule
  
  // Set light brightness using same method as apply_values for consistency
  auto light_it = lights_.find(channel);
  if (light_it != lights_.end() && light_it->second) {
    float brightness = pwm / 100.0f;  // Convert percentage to 0-1
    
    // Use make_call() to ensure consistent state management
    auto call = light_it->second->make_call();
    call.set_state(brightness > 0.001f); // Turn on if brightness > 0
    if (brightness > 0.001f) {
      call.set_brightness(brightness);
    }
    call.set_transition_length(0);  // No transition for immediate response
    call.perform();
    
    ESP_LOGV(TAG, "Force channel %u: PWM=%.1f%%, brightness=%.3f", channel, pwm, brightness);
  }
  
  // Set current control
  auto current_it = current_controls_.find(channel);
  if (current_it != current_controls_.end() && current_it->second) {
    current_it->second->publish_state(current);
  }
  
  ESP_LOGD(TAG, "Forced channel %u to PWM: %.1f%%, Current: %.2fA", channel, pwm, current);
}

void LEDBrickScheduler::add_temperature_sensor(const std::string &name, sensor::Sensor *sensor) {
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

void LEDBrickScheduler::enable_temperature_control(bool enabled) {
  temp_control_.enable(enabled);
  ESP_LOGI(TAG, "Temperature control %s", enabled ? "enabled" : "disabled");
  
  // Update enable switch state
  if (temp_enable_switch_) {
    temp_enable_switch_->publish_state(enabled);
  }
}

bool LEDBrickScheduler::set_temperature_config_json(const std::string& json) {
  if (!temp_control_.import_config_json(json)) {
    return false;
  }
  
  // Update our local config
  temp_config_ = temp_control_.get_config();
  
  // Save to flash with the schedule
  save_schedule_to_flash();
  
  return true;
}

void LEDBrickScheduler::update_temperature_sensors() {
  uint32_t now = millis();
  
  // Auto-discover temperature sensors if we don't have any yet
  static uint32_t last_discovery_check = 0;
  if (temp_sensors_.empty() && (now - last_discovery_check > 10000)) {  // Check every 10 seconds
    last_discovery_check = now;
    ESP_LOGI(TAG, "Searching for temperature sensors...");
    
    // Scan all sensors for temperature sensors (by unit of measurement)
    for (auto* sensor : App.get_sensors()) {
      std::string unit = sensor->get_unit_of_measurement().c_str();
      std::string name = sensor->get_name().c_str();
      
      // Debug log all sensors with units to help troubleshoot
      ESP_LOGD(TAG, "Checking sensor '%s' with unit '%s'", name.c_str(), unit.c_str());
      
      if ((unit == "°C" || unit == "°F" || unit == "C" || unit == "F") && sensor->has_state()) {
        // Exclude template sensors that are used for temperature control monitoring
        if (name.find("Temperature Control") != std::string::npos || 
            name.find("Control Current") != std::string::npos ||
            name.find("Control Target") != std::string::npos ||
            name.find("Control Fan") != std::string::npos ||
            name.find("PID") != std::string::npos) {
          ESP_LOGD(TAG, "Skipping template sensor: %s", name.c_str());
          continue;
        }
        
        // Check if we already have this sensor
        bool found = false;
        for (const auto &existing : temp_sensors_) {
          if (existing.sensor == sensor) {
            found = true;
            break;
          }
        }
        
        if (!found) {
          temp_sensors_.push_back({name, sensor});
          // Also add the sensor to the temperature control system
          temp_control_.add_temperature_sensor(name);
          ESP_LOGI(TAG, "Auto-discovered temperature sensor: %s (%s)", name.c_str(), unit.c_str());
        }
      }
    }
  }
  
  // Update temperature sensors
  if (!temp_sensors_.empty()) {
    ESP_LOGD(TAG, "Updating %zu temperature sensors", temp_sensors_.size());
  }
  
  for (const auto &mapping : temp_sensors_) {
    if (mapping.sensor && mapping.sensor->has_state()) {
      float temp = mapping.sensor->state;
      ESP_LOGD(TAG, "Temperature sensor '%s': %.2f°C", mapping.name.c_str(), temp);
      
      // Basic sanity check on temperature value
      if (temp > -50.0f && temp < 150.0f) {
        temp_control_.update_temperature_sensor(mapping.name, temp, now);
      } else {
        ESP_LOGW(TAG, "Invalid temperature reading from %s: %.1f°C", 
                mapping.name.c_str(), temp);
      }
    } else {
      ESP_LOGD(TAG, "Temperature sensor '%s': no valid state", mapping.name.c_str());
    }
  }
}

void LEDBrickScheduler::update_fan_speed() {
  if (fan_speed_sensor_ && fan_speed_sensor_->has_state()) {
    float rpm = fan_speed_sensor_->state;
    temp_control_.update_fan_rpm(rpm);
  }
}

void LEDBrickScheduler::publish_temp_sensor_values() {
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
  if (temp_enable_switch_ && temp_enable_switch_->state != status.enabled) {
    temp_enable_switch_->publish_state(status.enabled);
  }
}

void LEDBrickScheduler::on_fan_pwm_change(float pwm) {
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

void LEDBrickScheduler::on_fan_enable_change(bool enabled) {
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

void LEDBrickScheduler::on_emergency_change(bool emergency) {
  if (emergency) {
    ESP_LOGW(TAG, "THERMAL EMERGENCY ACTIVATED!");
    
    // Set thermal emergency flag which affects light output
    thermal_emergency_ = true;
    
    // Force all channels to zero immediately
    for (uint8_t channel = 0; channel < num_channels_; channel++) {
      force_channel_output(channel, 0.0f, 0.0f);
    }
  } else {
    ESP_LOGI(TAG, "Thermal emergency cleared - restoring normal operation");
    
    thermal_emergency_ = false;
    
    // Force the next update to apply all values
    force_next_update_ = true;
    
    // Immediately restore normal operation if scheduler is enabled
    if (enabled_) {
      // Get current values and apply them
      uint16_t current_time = get_current_time_minutes();
      auto values = scheduler_.get_values_at_time_with_astro(current_time, scheduler_.get_astronomical_times());
      
      if (values.valid) {
        ESP_LOGI(TAG, "Restoring scheduled values at %02d:%02d", current_time / 60, current_time % 60);
        // Log the values we're trying to restore
        for (size_t i = 0; i < values.pwm_values.size() && i < 3; i++) {
          ESP_LOGI(TAG, "  Channel %d: PWM=%.1f%%, Current=%.3fA", 
                   i, values.pwm_values[i], values.current_values[i]);
        }
        apply_values(values);
      }
    }
  }
  
  // Update emergency sensor
  if (thermal_emergency_sensor_) {
    thermal_emergency_sensor_->publish_state(emergency);
  }
}

} // namespace ledbrick_scheduler
} // namespace esphome
