#include "ledbrick_scheduler.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

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
  
  // Load schedule from flash storage
  load_schedule_from_flash();
  
  // Load default sunrise/sunset schedule if no points exist
  if (scheduler_.is_schedule_empty()) {
    ESP_LOGI(TAG, "No saved schedule found, loading default preset");
    load_preset("sunrise_sunset");
    save_schedule_to_flash();  // Save the default schedule
  }
  
  ESP_LOGCONFIG(TAG, "LEDBrick Scheduler setup complete");
}

void LEDBrickScheduler::update() {
  if (!enabled_) {
    ESP_LOGVV(TAG, "Scheduler disabled, skipping update");
    return;
  }
  
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
  ESP_LOGCONFIG(TAG, "  Timezone: %s", timezone_.c_str());
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
  
  // Use the standalone scheduler to create the preset with astronomical data
  const_cast<LEDScheduler&>(scheduler_).create_sunrise_sunset_preset(sunrise_minutes, sunset_minutes);
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
  // Use standalone scheduler's serialization
  auto serialized = scheduler_.serialize();
  
  ScheduleStorage storage;
  memset(&storage, 0, sizeof(storage));
  
  storage.num_points = serialized.num_points;
  
  // Copy serialized data, limiting to storage size
  size_t copy_size = std::min(serialized.data.size(), sizeof(storage.data));
  std::memcpy(storage.data, serialized.data.data(), copy_size);
  
  if (schedule_pref_.save(&storage)) {
    ESP_LOGD(TAG, "Saved schedule to flash (%u points, %zu bytes)", storage.num_points, copy_size);
  } else {
    ESP_LOGW(TAG, "Failed to save schedule to flash");
  }
}

void LEDBrickScheduler::load_schedule_from_flash() {
  ScheduleStorage storage;
  
  if (!schedule_pref_.load(&storage)) {
    ESP_LOGD(TAG, "No saved schedule found in flash");
    return;
  }
  
  ESP_LOGD(TAG, "Loading schedule from flash (%u points)", storage.num_points);
  
  // Create serialized data structure from storage
  LEDScheduler::SerializedData serialized;
  serialized.num_points = storage.num_points;
  serialized.num_channels = num_channels_;
  serialized.data.assign(storage.data, storage.data + sizeof(storage.data));
  
  // Use standalone scheduler's deserialization
  if (scheduler_.deserialize(serialized)) {
    ESP_LOGI(TAG, "Loaded %zu schedule points from flash", scheduler_.get_schedule_size());
  } else {
    ESP_LOGW(TAG, "Failed to deserialize schedule from flash");
  }
}

void LEDBrickScheduler::export_schedule_json(std::string &json_output) const {
  // Use standalone scheduler's JSON export, then augment with ESPHome-specific data
  json_output = scheduler_.export_json();
  
  // Parse and modify the JSON to add ESPHome-specific fields
  // For simplicity, we'll create a new JSON string
  uint16_t current_time = get_current_time_minutes();
  auto current_values = get_current_values();
  
  size_t closing_brace = json_output.rfind('}');
  if (closing_brace != std::string::npos) {
    json_output.insert(closing_brace, 
      ",\"timezone\":\"" + timezone_ + 
      "\",\"current_time_minutes\":" + std::to_string(current_time) +
      ",\"enabled\":" + std::string(enabled_ ? "true" : "false"));
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
  
  // Use standalone scheduler's JSON import
  bool success = scheduler_.import_json(json_input);
  
  if (success) {
    save_schedule_to_flash();
    ESP_LOGI(TAG, "Successfully imported %zu schedule points, enabled=%s", 
             scheduler_.get_schedule_size(), enabled_ ? "true" : "false");
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
  if (sun_times.rise_valid && sun_times.set_valid && sun_times.set_minutes > sun_times.rise_minutes) {
    solar_noon = (sun_times.rise_minutes + sun_times.set_minutes) / 2;
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

} // namespace ledbrick_scheduler
} // namespace esphome