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
  
  // Update color sensors with initial values
  update_color_sensors();
  
  ESP_LOGCONFIG(TAG, "LEDBrick Scheduler setup complete");
}

void LEDBrickScheduler::update() {
  if (!enabled_) {
    ESP_LOGVV(TAG, "Scheduler disabled, skipping update");
    return;
  }
  
  // Update timezone offset from time source if available
  update_timezone_from_time_source();
  
  // Update astronomical times for dynamic schedule points
  update_astronomical_times_for_scheduler();
  
  // Get current values from standalone scheduler and apply them
  uint16_t current_time = get_current_time_minutes();
  
  // Use astronomical interpolation if we have dynamic points
  auto values = scheduler_.get_values_at_time_with_astro(current_time, scheduler_.get_astronomical_times());
  if (values.valid) {
    apply_values(values);
  }
  
  // Log current interpolated values every 10 seconds (reduce log spam)
  static uint32_t last_log_time = 0;
  uint32_t current_millis = millis();
  if (current_millis - last_log_time > 10000) {
    uint16_t current_time = get_current_time_minutes();
    ESP_LOGD(TAG, "Scheduler active at %02u:%02u - Ch1: PWM=%.1f%%, Current=%.2fA", 
             current_time / 60, current_time % 60,
             values.pwm_values.size() > 0 ? values.pwm_values[0] : 0.0f,
             values.current_values.size() > 0 ? values.current_values[0] : 0.0f);
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
      auto call = light_it->second->current_values;
      // Get brightness as percentage (0-100)
      result.pwm_values[channel] = call.get_brightness() * 100.0f;
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
  
  for (uint8_t channel = 0; channel < num_channels_; channel++) {
    // Apply PWM to light entity
    auto light_it = lights_.find(channel);
    if (light_it != lights_.end() && light_it->second) {
      // Apply PWM scale factor before converting to 0-1 range
      float scaled_pwm = values.pwm_values[channel] * pwm_scale_;
      float brightness = scaled_pwm / 100.0f; // Convert percentage to 0-1
      
      // Only update if value has changed significantly (reduce unnecessary calls)
      if (last_pwm_values_.size() <= channel || 
          abs(brightness - last_pwm_values_[channel]) > 0.001f) {
        
        // Create light call to set brightness
        auto call = light_it->second->make_call();
        call.set_state(brightness > 0.001f); // Turn on if brightness > 0
        if (brightness > 0.001f) {
          call.set_brightness(brightness);
        }
        call.perform();
        
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
      
      // Only update if value has changed significantly
      if (last_current_values_.size() <= channel || 
          abs(target_current - last_current_values_[channel]) > 0.01f) {
        
        // Set the current control value
        current_it->second->publish_state(target_current);
        
        if (last_current_values_.size() <= channel) {
          last_current_values_.resize(num_channels_, -1.0f);
        }
        last_current_values_[channel] = target_current;
        
        ESP_LOGV(TAG, "Updated current %u to %.3fA (limited from %.3fA)", 
                 channel, target_current, values.current_values[channel]);
      }
    } else {
      ESP_LOGVV(TAG, "No current control found for channel %u", channel);
    }
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
    
    // Temporarily set projected times for export
    const_cast<LEDScheduler&>(scheduler_).set_astronomical_times(projected_times);
    
    // Export with projected times
    json_output = scheduler_.export_json();
    
    // Restore actual astronomical times (if needed for normal operation)
    // Note: This is safe because the next update cycle will recalculate
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
    json_output.insert(closing_brace, 
      ",\"timezone\":\"" + timezone_ + 
      "\",\"timezone_offset_hours\":" + std::to_string(timezone_offset_hours_) +
      ",\"current_time_minutes\":" + std::to_string(current_time) +
      ",\"enabled\":" + std::string(enabled_ ? "true" : "false") +
      ",\"latitude\":" + std::to_string(latitude_) +
      ",\"longitude\":" + std::to_string(longitude_) +
      ",\"astronomical_projection\":" + std::string(astronomical_projection_ ? "true" : "false") +
      ",\"time_shift_hours\":" + std::to_string(time_shift_hours_) +
      ",\"time_shift_minutes\":" + std::to_string(time_shift_minutes_));
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
  
  // Use standalone scheduler's JSON import
  bool success = scheduler_.import_json(json_input);
  
  if (success) {
    // Update astronomical calculator with loaded settings (without saving during import)
    astro_calc_.set_location(latitude_, longitude_);
    astro_calc_.set_timezone_offset(timezone_offset_hours_);
    astro_calc_.set_projection_settings(astronomical_projection_, time_shift_hours_, time_shift_minutes_);
    
    save_schedule_to_flash();
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
  if (current_moon.enabled == config.enabled && 
      current_moon.phase_scaling == config.phase_scaling &&
      current_moon.base_intensity.size() == config.base_intensity.size()) {
    bool changed = false;
    for (size_t i = 0; i < config.base_intensity.size(); i++) {
      if (abs(current_moon.base_intensity[i] - config.base_intensity[i]) > 0.01f) {
        changed = true;
        break;
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

void LEDBrickScheduler::update_astronomical_times_for_scheduler() {
  static uint32_t last_astro_update = 0;
  uint32_t current_millis = millis();
  
  // Update astronomical times every 5 minutes (300000 ms)
  if (current_millis - last_astro_update < 300000 && last_astro_update != 0) {
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
  
  // Get moon data
  auto moon_times = astro_calc_.get_moon_rise_set_times(dt);
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

} // namespace ledbrick_scheduler
} // namespace esphome