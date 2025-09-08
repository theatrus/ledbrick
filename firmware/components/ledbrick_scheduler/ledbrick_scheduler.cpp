#include "ledbrick_scheduler.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace esphome {
namespace ledbrick_scheduler {

static const char *const TAG = "ledbrick_scheduler";

void LEDBrickScheduler::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LEDBrick Scheduler...");
  
  // Initialize persistent storage
  schedule_pref_ = global_preferences->make_preference<ScheduleStorage>(SCHEDULE_HASH);
  
  // Initialize presets
  initialize_presets();
  
  // Load schedule from flash storage
  load_schedule_from_flash();
  
  // Load default sunrise/sunset schedule if no points exist
  if (schedule_points_.empty()) {
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
  
  // Get current values and apply them
  auto values = get_current_values();
  apply_values(values);
  
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
  ESP_LOGCONFIG(TAG, "  Timezone: %s", timezone_.c_str());
  ESP_LOGCONFIG(TAG, "  Location: %.4f°N, %.4f°W", latitude_, -longitude_);
  ESP_LOGCONFIG(TAG, "  Schedule Points: %zu", schedule_points_.size());
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
  // Validate point
  if (point.pwm_values.size() != num_channels_ || point.current_values.size() != num_channels_) {
    ESP_LOGW(TAG, "Schedule point has wrong number of channels (expected %u)", num_channels_);
    return;
  }
  
  // Remove existing point at same time
  remove_schedule_point(point.time_minutes);
  
  // Add new point
  schedule_points_.push_back(point);
  sort_schedule_points();
  
  // Save to flash automatically
  save_schedule_to_flash();
  
  ESP_LOGD(TAG, "Added schedule point at %02u:%02u", 
           point.time_minutes / 60, point.time_minutes % 60);
}

void LEDBrickScheduler::set_schedule_point(uint16_t time_minutes, const std::vector<float> &pwm_values, 
                                          const std::vector<float> &current_values) {
  SchedulePoint point(time_minutes, pwm_values, current_values);
  add_schedule_point(point);
}

void LEDBrickScheduler::remove_schedule_point(uint16_t time_minutes) {
  auto it = std::remove_if(schedule_points_.begin(), schedule_points_.end(),
                          [time_minutes](const SchedulePoint &p) {
                            return p.time_minutes == time_minutes;
                          });
  
  if (it != schedule_points_.end()) {
    schedule_points_.erase(it, schedule_points_.end());
    ESP_LOGD(TAG, "Removed schedule point at %02u:%02u", time_minutes / 60, time_minutes % 60);
  }
}

void LEDBrickScheduler::clear_schedule() {
  schedule_points_.clear();
  ESP_LOGD(TAG, "Cleared all schedule points");
}

void LEDBrickScheduler::load_preset(const std::string &preset_name) {
  auto it = presets_.find(preset_name);
  if (it != presets_.end()) {
    schedule_points_ = it->second;
    sort_schedule_points();
    ESP_LOGI(TAG, "Loaded preset '%s' with %zu points", preset_name.c_str(), schedule_points_.size());
  } else {
    ESP_LOGW(TAG, "Preset '%s' not found", preset_name.c_str());
  }
}

void LEDBrickScheduler::save_preset(const std::string &preset_name) {
  presets_[preset_name] = schedule_points_;
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
  return interpolate_values(current_time);
}

InterpolationResult LEDBrickScheduler::interpolate_values(uint16_t current_time) const {
  InterpolationResult result;
  result.pwm_values.resize(num_channels_, 0.0f);
  result.current_values.resize(num_channels_, 0.0f);
  
  if (schedule_points_.empty()) {
    return result;
  }
  
  // Handle single point case
  if (schedule_points_.size() == 1) {
    result.pwm_values = schedule_points_[0].pwm_values;
    result.current_values = schedule_points_[0].current_values;
    return result;
  }
  
  // Find interpolation points
  const SchedulePoint *before = nullptr;
  const SchedulePoint *after = nullptr;
  
  // Check if we're before the first point
  if (current_time <= schedule_points_[0].time_minutes) {
    // Interpolate from midnight (0,0) to first point
    after = &schedule_points_[0];
    float ratio = after->time_minutes > 0 ? static_cast<float>(current_time) / after->time_minutes : 0.0f;
    
    for (size_t i = 0; i < num_channels_; i++) {
      result.pwm_values[i] = ratio * after->pwm_values[i];
      result.current_values[i] = ratio * after->current_values[i];
    }
    
    return result;
  }
  
  // Check if we're after the last point
  if (current_time >= schedule_points_.back().time_minutes) {
    // Interpolate from last point to midnight (0,0)
    before = &schedule_points_.back();
    float time_to_midnight = 1440 - before->time_minutes;
    float time_since_last = current_time - before->time_minutes;
    float ratio = time_to_midnight > 0 ? (time_to_midnight - time_since_last) / time_to_midnight : 0.0f;
    
    for (size_t i = 0; i < num_channels_; i++) {
      result.pwm_values[i] = ratio * before->pwm_values[i];
      result.current_values[i] = ratio * before->current_values[i];
    }
    
    return result;
  }
  
  // Find surrounding points
  for (size_t i = 0; i < schedule_points_.size() - 1; i++) {
    if (current_time >= schedule_points_[i].time_minutes && 
        current_time <= schedule_points_[i + 1].time_minutes) {
      before = &schedule_points_[i];
      after = &schedule_points_[i + 1];
      break;
    }
  }
  
  if (!before || !after) {
    ESP_LOGW(TAG, "Failed to find interpolation points for time %u", current_time);
    return result;
  }
  
  // Linear interpolation
  uint16_t time_diff = after->time_minutes - before->time_minutes;
  float ratio = time_diff > 0 ? static_cast<float>(current_time - before->time_minutes) / time_diff : 0.0f;
  
  for (size_t i = 0; i < num_channels_; i++) {
    result.pwm_values[i] = before->pwm_values[i] + ratio * (after->pwm_values[i] - before->pwm_values[i]);
    result.current_values[i] = before->current_values[i] + ratio * (after->current_values[i] - before->current_values[i]);
  }
  
  ESP_LOGVV(TAG, "Interpolated at %02u:%02u (ratio=%.3f): PWM[0]=%.1f%%, Current[0]=%.2fA", 
            current_time / 60, current_time % 60, ratio, 
            result.pwm_values[0], result.current_values[0]);
  
  return result;
}

void LEDBrickScheduler::apply_values(const InterpolationResult &values) {
  static std::vector<float> last_pwm_values_(num_channels_, -1.0f);
  static std::vector<float> last_current_values_(num_channels_, -1.0f);
  
  for (uint8_t channel = 0; channel < num_channels_; channel++) {
    // Apply PWM to light entity
    auto light_it = lights_.find(channel);
    if (light_it != lights_.end() && light_it->second) {
      float brightness = values.pwm_values[channel] / 100.0f; // Convert percentage to 0-1
      
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
        
        ESP_LOGV(TAG, "Updated light %u brightness to %.3f (%.1f%%)", 
                 channel, brightness, values.pwm_values[channel]);
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

void LEDBrickScheduler::sort_schedule_points() {
  std::sort(schedule_points_.begin(), schedule_points_.end(),
           [](const SchedulePoint &a, const SchedulePoint &b) {
             return a.time_minutes < b.time_minutes;
           });
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


void LEDBrickScheduler::initialize_presets() {
  presets_["sunrise_sunset"] = create_sunrise_sunset_preset();
  presets_["full_spectrum"] = create_full_spectrum_preset();
  presets_["simple"] = create_simple_preset();
  
  ESP_LOGD(TAG, "Initialized %zu built-in presets", presets_.size());
}

std::vector<SchedulePoint> LEDBrickScheduler::create_sunrise_sunset_preset() const {
  std::vector<SchedulePoint> points;
  
  // 6 AM - Dawn (gradually increasing)
  points.emplace_back(360, 
                     std::vector<float>{0, 0, 0, 20, 40, 60, 80, 0}, 
                     std::vector<float>{0, 0, 0, 0.3f, 0.6f, 1.0f, 1.2f, 0});
  
  // 12 PM - Noon (full spectrum)
  points.emplace_back(720,
                     std::vector<float>{60, 80, 100, 90, 80, 70, 60, 40},
                     std::vector<float>{1.0f, 1.5f, 2.0f, 1.8f, 1.5f, 1.2f, 1.0f, 0.6f});
  
  // 6 PM - Dusk (decreasing)
  points.emplace_back(1080,
                     std::vector<float>{20, 40, 60, 80, 60, 40, 20, 10},
                     std::vector<float>{0.3f, 0.6f, 1.0f, 1.2f, 1.0f, 0.6f, 0.3f, 0.1f});
  
  // 12 AM - Night (off)
  points.emplace_back(1440,
                     std::vector<float>(num_channels_, 0.0f),
                     std::vector<float>(num_channels_, 0.0f));
  
  return points;
}

std::vector<SchedulePoint> LEDBrickScheduler::create_full_spectrum_preset() const {
  std::vector<SchedulePoint> points;
  
  // 8 AM - Morning
  points.emplace_back(480,
                     std::vector<float>{40, 60, 80, 100, 80, 60, 40, 20},
                     std::vector<float>{0.6f, 1.0f, 1.5f, 2.0f, 1.5f, 1.0f, 0.6f, 0.3f});
  
  // 12 PM - Midday (maximum)
  points.emplace_back(720,
                     std::vector<float>{80, 100, 100, 100, 100, 100, 80, 60},
                     std::vector<float>{1.5f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 1.5f, 1.0f});
  
  // 4 PM - Afternoon
  points.emplace_back(960,
                     std::vector<float>{60, 80, 100, 100, 80, 60, 40, 30},
                     std::vector<float>{1.0f, 1.5f, 2.0f, 2.0f, 1.5f, 1.0f, 0.6f, 0.4f});
  
  // 8 PM - Evening
  points.emplace_back(1200,
                     std::vector<float>{20, 30, 40, 60, 40, 30, 20, 10},
                     std::vector<float>{0.3f, 0.4f, 0.6f, 1.0f, 0.6f, 0.4f, 0.3f, 0.1f});
  
  return points;
}

std::vector<SchedulePoint> LEDBrickScheduler::create_simple_preset() const {
  std::vector<SchedulePoint> points;
  
  // 8 AM - On
  points.emplace_back(480,
                     std::vector<float>(num_channels_, 70.0f),
                     std::vector<float>(num_channels_, 1.2f));
  
  // 8 PM - Off
  points.emplace_back(1200,
                     std::vector<float>(num_channels_, 0.0f),
                     std::vector<float>(num_channels_, 0.0f));
  
  return points;
}

void LEDBrickScheduler::save_schedule_to_flash() {
  ScheduleStorage storage;
  memset(&storage, 0, sizeof(storage));
  
  storage.num_points = std::min((size_t)50, schedule_points_.size()); // Limit to 50 points
  
  size_t data_pos = 0;
  for (size_t i = 0; i < storage.num_points && data_pos < sizeof(storage.data) - 100; i++) {
    const auto &point = schedule_points_[i];
    
    // Write time_minutes (2 bytes)
    if (data_pos + 2 >= sizeof(storage.data)) break;
    storage.data[data_pos++] = point.time_minutes & 0xFF;
    storage.data[data_pos++] = (point.time_minutes >> 8) & 0xFF;
    
    // Write PWM count and values
    if (data_pos + 1 >= sizeof(storage.data)) break;
    uint8_t pwm_count = std::min((size_t)num_channels_, point.pwm_values.size());
    storage.data[data_pos++] = pwm_count;
    
    for (uint8_t j = 0; j < pwm_count && data_pos + 4 < sizeof(storage.data); j++) {
      uint32_t float_bits = *reinterpret_cast<const uint32_t*>(&point.pwm_values[j]);
      storage.data[data_pos++] = (float_bits >> 0) & 0xFF;
      storage.data[data_pos++] = (float_bits >> 8) & 0xFF;
      storage.data[data_pos++] = (float_bits >> 16) & 0xFF;
      storage.data[data_pos++] = (float_bits >> 24) & 0xFF;
    }
    
    // Write current count and values
    if (data_pos + 1 >= sizeof(storage.data)) break;
    uint8_t current_count = std::min((size_t)num_channels_, point.current_values.size());
    storage.data[data_pos++] = current_count;
    
    for (uint8_t j = 0; j < current_count && data_pos + 4 < sizeof(storage.data); j++) {
      uint32_t float_bits = *reinterpret_cast<const uint32_t*>(&point.current_values[j]);
      storage.data[data_pos++] = (float_bits >> 0) & 0xFF;
      storage.data[data_pos++] = (float_bits >> 8) & 0xFF;
      storage.data[data_pos++] = (float_bits >> 16) & 0xFF;
      storage.data[data_pos++] = (float_bits >> 24) & 0xFF;
    }
  }
  
  if (schedule_pref_.save(&storage)) {
    ESP_LOGD(TAG, "Saved schedule to flash (%u points, %zu bytes)", storage.num_points, data_pos);
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
  
  if (storage.num_points > 100) {  // Sanity check
    ESP_LOGW(TAG, "Invalid schedule data: too many points (%u)", storage.num_points);
    return;
  }
  
  schedule_points_.clear();
  
  size_t data_pos = 0;
  
  // Read each schedule point
  for (uint16_t i = 0; i < storage.num_points && data_pos < sizeof(storage.data) - 10; i++) {
    SchedulePoint point;
    
    // Read time (2 bytes)
    if (data_pos + 2 > sizeof(storage.data)) break;
    point.time_minutes = storage.data[data_pos] | (storage.data[data_pos + 1] << 8);
    data_pos += 2;
    
    // Read PWM values
    if (data_pos + 1 > sizeof(storage.data)) break;
    uint8_t pwm_count = storage.data[data_pos++];
    if (pwm_count > 16) {
      ESP_LOGW(TAG, "Invalid PWM count: %u", pwm_count);
      break;
    }
    
    for (uint8_t j = 0; j < pwm_count && data_pos + 4 <= sizeof(storage.data); j++) {
      uint32_t float_bits = storage.data[data_pos] | (storage.data[data_pos + 1] << 8) | 
                           (storage.data[data_pos + 2] << 16) | (storage.data[data_pos + 3] << 24);
      float val = *reinterpret_cast<const float*>(&float_bits);
      point.pwm_values.push_back(val);
      data_pos += 4;
    }
    
    // Read current values
    if (data_pos + 1 > sizeof(storage.data)) break;
    uint8_t current_count = storage.data[data_pos++];
    if (current_count > 16) {
      ESP_LOGW(TAG, "Invalid current count: %u", current_count);
      break;
    }
    
    for (uint8_t j = 0; j < current_count && data_pos + 4 <= sizeof(storage.data); j++) {
      uint32_t float_bits = storage.data[data_pos] | (storage.data[data_pos + 1] << 8) | 
                           (storage.data[data_pos + 2] << 16) | (storage.data[data_pos + 3] << 24);
      float val = *reinterpret_cast<const float*>(&float_bits);
      point.current_values.push_back(val);
      data_pos += 4;
    }
    
    schedule_points_.push_back(point);
  }
  
  sort_schedule_points();
  ESP_LOGI(TAG, "Loaded %zu schedule points from flash", schedule_points_.size());
}

void LEDBrickScheduler::export_schedule_json(std::string &json_output) const {
  uint16_t current_time = get_current_time_minutes();
  auto current_values = get_current_values();
  
  json_output = "{";
  json_output += "\"timezone\":\"" + timezone_ + "\",";
  json_output += "\"current_time_minutes\":" + std::to_string(current_time) + ",";
  json_output += "\"current_time_display\":\"";
  
  // Add human-readable time
  char time_str[10];
  sprintf(time_str, "%02d:%02d", current_time / 60, current_time % 60);
  json_output += std::string(time_str) + "\",";
  
  json_output += "\"enabled\":" + std::string(enabled_ ? "true" : "false") + ",";
  json_output += "\"channels\":" + std::to_string(num_channels_) + ",";
  
  // Current interpolated values
  json_output += "\"current_pwm_values\":[";
  for (size_t i = 0; i < current_values.pwm_values.size(); i++) {
    if (i > 0) json_output += ",";
    json_output += std::to_string(current_values.pwm_values[i]);
  }
  json_output += "],\"current_current_values\":[";
  for (size_t i = 0; i < current_values.current_values.size(); i++) {
    if (i > 0) json_output += ",";
    json_output += std::to_string(current_values.current_values[i]);
  }
  json_output += "],";
  
  // Schedule points
  json_output += "\"schedule_points\":[";
  for (size_t i = 0; i < schedule_points_.size(); i++) {
    const auto &point = schedule_points_[i];
    
    if (i > 0) json_output += ",";
    json_output += "{\"time_minutes\":" + std::to_string(point.time_minutes);
    
    // Add human-readable time for each point
    sprintf(time_str, "%02d:%02d", point.time_minutes / 60, point.time_minutes % 60);
    json_output += ",\"time_display\":\"" + std::string(time_str) + "\"";
    
    json_output += ",\"pwm_values\":[";
    
    for (size_t j = 0; j < point.pwm_values.size(); j++) {
      if (j > 0) json_output += ",";
      json_output += std::to_string(point.pwm_values[j]);
    }
    
    json_output += "],\"current_values\":[";
    
    for (size_t j = 0; j < point.current_values.size(); j++) {
      if (j > 0) json_output += ",";
      json_output += std::to_string(point.current_values[j]);
    }
    
    json_output += "]}";
  }
  
  json_output += "]}";
}

bool LEDBrickScheduler::import_schedule_json(const std::string &json_input) {
  ESP_LOGI(TAG, "Importing schedule from JSON (%zu chars)", json_input.length());
  
  // Simple JSON parsing - look for key patterns
  // This is basic parsing suitable for the JSON format we export
  
  // Clear existing schedule
  schedule_points_.clear();
  
  // Extract enabled state
  size_t enabled_pos = json_input.find("\"enabled\":");
  if (enabled_pos != std::string::npos) {
    size_t value_start = json_input.find_first_not_of(" \t", enabled_pos + 10);
    if (value_start != std::string::npos) {
      enabled_ = json_input.substr(value_start, 4) == "true";
    }
  }
  
  // Find schedule_points array
  size_t points_start = json_input.find("\"schedule_points\":[");
  if (points_start == std::string::npos) {
    ESP_LOGW(TAG, "No schedule_points found in JSON");
    return false;
  }
  
  points_start += 18; // Skip "schedule_points":[
  size_t points_end = json_input.find("]}", points_start);
  if (points_end == std::string::npos) {
    points_end = json_input.find("]", points_start);
  }
  
  if (points_end == std::string::npos) {
    ESP_LOGW(TAG, "Invalid schedule_points array format");
    return false;
  }
  
  std::string points_str = json_input.substr(points_start, points_end - points_start);
  ESP_LOGD(TAG, "Parsing schedule points: %s", points_str.c_str());
  
  // Parse individual points (basic implementation)
  size_t pos = 0;
  int points_parsed = 0;
  
  while (pos < points_str.length() && points_parsed < 50) { // Limit to 50 points
    // Find start of next point object
    size_t point_start = points_str.find("{", pos);
    if (point_start == std::string::npos) break;
    
    size_t point_end = points_str.find("}", point_start);
    if (point_end == std::string::npos) break;
    
    std::string point_str = points_str.substr(point_start, point_end - point_start + 1);
    
    // Parse this point
    SchedulePoint point;
    bool valid_point = false;
    
    // Extract time_minutes
    size_t time_pos = point_str.find("\"time_minutes\":");
    if (time_pos != std::string::npos) {
      size_t num_start = time_pos + 15;
      size_t num_end = point_str.find_first_of(",}", num_start);
      if (num_end != std::string::npos) {
        std::string time_str = point_str.substr(num_start, num_end - num_start);
        // Use stdlib atoi for reliable integer parsing
        int time_val = atoi(time_str.c_str());
        
        if (time_val >= 0 && time_val < 1440) {
          point.time_minutes = static_cast<uint16_t>(time_val);
          valid_point = true;
        }
      }
    }
    
    if (!valid_point) {
      pos = point_end + 1;
      continue;
    }
    
    // Extract PWM values array
    size_t pwm_start = point_str.find("\"pwm_values\":[");
    if (pwm_start != std::string::npos) {
      pwm_start += 14;
      size_t pwm_end = point_str.find("]", pwm_start);
      if (pwm_end != std::string::npos) {
        std::string pwm_str = point_str.substr(pwm_start, pwm_end - pwm_start);
        parse_float_array(pwm_str, point.pwm_values);
      }
    }
    
    // Extract current values array
    size_t current_start = point_str.find("\"current_values\":[");
    if (current_start != std::string::npos) {
      current_start += 18;
      size_t current_end = point_str.find("]", current_start);
      if (current_end != std::string::npos) {
        std::string current_str = point_str.substr(current_start, current_end - current_start);
        parse_float_array(current_str, point.current_values);
      }
    }
    
    // Validate and add point
    if (point.time_minutes < 1440 && 
        point.pwm_values.size() <= num_channels_ && 
        point.current_values.size() <= num_channels_) {
      
      // Resize to match channel count
      point.pwm_values.resize(num_channels_, 0.0f);
      point.current_values.resize(num_channels_, 0.0f);
      
      schedule_points_.push_back(point);
      points_parsed++;
      
      ESP_LOGD(TAG, "Imported point: %02u:%02u with %zu PWM/%zu current values",
              point.time_minutes / 60, point.time_minutes % 60,
              point.pwm_values.size(), point.current_values.size());
    } else {
      ESP_LOGW(TAG, "Skipping invalid schedule point");
    }
    
    pos = point_end + 1;
  }
  
  // Sort points and save to flash
  sort_schedule_points();
  save_schedule_to_flash();
  
  ESP_LOGI(TAG, "Successfully imported %d schedule points, enabled=%s", 
           points_parsed, enabled_ ? "true" : "false");
  
  return points_parsed > 0;
}

void LEDBrickScheduler::parse_float_array(const std::string &array_str, std::vector<float> &values) const {
  values.clear();
  
  size_t pos = 0;
  while (pos < array_str.length()) {
    // Skip whitespace and commas
    while (pos < array_str.length() && (array_str[pos] == ' ' || array_str[pos] == '\t' || array_str[pos] == ',')) {
      pos++;
    }
    
    if (pos >= array_str.length()) break;
    
    // Find end of number
    size_t end_pos = pos;
    while (end_pos < array_str.length() && 
           array_str[end_pos] != ',' && 
           array_str[end_pos] != ']' && 
           array_str[end_pos] != '}') {
      end_pos++;
    }
    
    if (end_pos > pos) {
      std::string num_str = array_str.substr(pos, end_pos - pos);
      
      // Use stdlib atof for reliable float parsing
      float value = static_cast<float>(atof(num_str.c_str()));
      values.push_back(value);
    }
    
    pos = end_pos;
  }
}

float LEDBrickScheduler::get_moon_phase() const {
  if (!time_source_) {
    ESP_LOGW(TAG, "No time source available for moon phase calculation");
    return 0.0f;
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    ESP_LOGW(TAG, "Invalid time for moon phase calculation");
    return 0.0f;
  }
  
  // Calculate Julian Day Number
  int year = time.year;
  int month = time.month;
  int day = time.day_of_month;
  
  // Adjust for January/February
  if (month <= 2) {
    year--;
    month += 12;
  }
  
  // Julian Day calculation (Gregorian calendar)
  int a = year / 100;
  int b = 2 - a + (a / 4);
  double jd = static_cast<double>(static_cast<int>(365.25 * (year + 4716)) + 
                                  static_cast<int>(30.6001 * (month + 1)) + 
                                  day + b - 1524.5);
  
  // Add time of day
  jd += (time.hour + time.minute / 60.0 + time.second / 3600.0) / 24.0;
  
  // Moon phase calculation using simplified lunar algorithm
  // Reference: Astronomical Algorithms by Jean Meeus
  
  // Days since J2000.0 (January 1, 2000, 12:00 TT)
  double days_since_j2000 = jd - 2451545.0;
  
  // Mean longitude of the Moon
  double moon_longitude = fmod(218.316 + 13.176396 * days_since_j2000, 360.0);
  if (moon_longitude < 0) moon_longitude += 360.0;
  
  // Mean longitude of the Sun  
  double sun_longitude = fmod(280.459 + 0.98564736 * days_since_j2000, 360.0);
  if (sun_longitude < 0) sun_longitude += 360.0;
  
  // Moon's elongation from the Sun
  double elongation = moon_longitude - sun_longitude;
  if (elongation < 0) elongation += 360.0;
  
  // Convert to phase (0.0 = new moon, 0.5 = full moon, 1.0 = new moon again)
  double phase = elongation / 360.0;
  
  ESP_LOGV(TAG, "Moon phase calculation: JD=%.2f, elongation=%.1f°, phase=%.3f", 
           jd, elongation, phase);
  
  return static_cast<float>(phase);
}

LEDBrickScheduler::MoonTimes LEDBrickScheduler::get_moon_rise_set_times() const {
  MoonTimes result;
  
  if (!time_source_) {
    ESP_LOGW(TAG, "No time source available for moon rise/set calculation");
    return result;
  }
  
  auto current_time = time_source_->now();
  if (!current_time.is_valid()) {
    ESP_LOGW(TAG, "Invalid time for moon rise/set calculation");
    return result;
  }
  
  // Calculate Julian Day for start of today
  int year = current_time.year;
  int month = current_time.month;
  int day = current_time.day_of_month;
  
  if (month <= 2) {
    year--;
    month += 12;
  }
  
  int a = year / 100;
  int b = 2 - a + (a / 4);
  double jd_base = static_cast<double>(static_cast<int>(365.25 * (year + 4716)) + 
                                       static_cast<int>(30.6001 * (month + 1)) + 
                                       day + b - 1524.5);
  
  double prev_altitude = -90.0;
  bool found_rise = false;
  bool found_set = false;
  
  // Check every 15 minutes throughout the day using the new helper function
  for (int minutes = 0; minutes < 1440 && (!found_rise || !found_set); minutes += 15) {
    int hours = minutes / 60;
    int mins = minutes % 60;
    
    // Calculate Julian Day for this specific time
    double jd = jd_base + (hours + mins / 60.0) / 24.0;
    
    // Use the dedicated position calculator
    auto pos = calculate_moon_position_at_time(jd);
    double altitude = pos.altitude;
    
    // Check for horizon crossings (0 degrees altitude)
    if (prev_altitude != -90.0) {  // Skip first iteration
      if (prev_altitude < 0.0 && altitude >= 0.0 && !found_rise) {
        // Moon rise detected
        result.rise_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
        if (result.rise_minutes < 0) result.rise_minutes += 1440;
        result.rise_valid = true;
        found_rise = true;
        ESP_LOGV(TAG, "Moon rise detected at %02d:%02d (altitude %.1f°)", 
                 result.rise_minutes / 60, result.rise_minutes % 60, altitude);
      }
      else if (prev_altitude >= 0.0 && altitude < 0.0 && !found_set) {
        // Moon set detected  
        result.set_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
        if (result.set_minutes < 0) result.set_minutes += 1440;
        result.set_valid = true;
        found_set = true;
        ESP_LOGV(TAG, "Moon set detected at %02d:%02d (altitude %.1f°)", 
                 result.set_minutes / 60, result.set_minutes % 60, altitude);
      }
    }
    
    prev_altitude = altitude;
  }
  
  ESP_LOGD(TAG, "Moon times for %.4f°N, %.4f°W: Rise=%s, Set=%s", 
           latitude_, -longitude_,
           result.rise_valid ? "valid" : "none",
           result.set_valid ? "valid" : "none");
  
  return result;
}

LEDBrickScheduler::CelestialPosition LEDBrickScheduler::calculate_moon_position() const {
  if (!time_source_) {
    return {-90.0, 0.0};  // Default below horizon
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    return {-90.0, 0.0};
  }
  
  // Calculate Julian Day Number with time of day
  int year = time.year;
  int month = time.month;
  int day = time.day_of_month;
  
  if (month <= 2) {
    year--;
    month += 12;
  }
  
  int a = year / 100;
  int b = 2 - a + (a / 4);
  double jd = static_cast<double>(static_cast<int>(365.25 * (year + 4716)) + 
                                  static_cast<int>(30.6001 * (month + 1)) + 
                                  day + b - 1524.5);
  
  // Add time of day
  jd += (time.hour + time.minute / 60.0 + time.second / 3600.0) / 24.0;
  
  return calculate_moon_position_at_time(jd);
}

LEDBrickScheduler::CelestialPosition LEDBrickScheduler::calculate_moon_position_at_time(double julian_day) const {
  CelestialPosition pos = {-90.0, 0.0};  // Default below horizon
  
  // Days since J2000.0
  double d = julian_day - 2451545.0;
  
  // Calculate Moon's position
  double L = fmod(218.316 + 13.176396 * d, 360.0);
  if (L < 0) L += 360.0;
  
  double M = fmod(134.963 + 13.064993 * d, 360.0);
  if (M < 0) M += 360.0;
  M = M * M_PI / 180.0;
  
  // Moon's longitude with perturbation
  double moon_lon = L + 6.289 * sin(M);
  moon_lon = fmod(moon_lon, 360.0);
  if (moon_lon < 0) moon_lon += 360.0;
  moon_lon = moon_lon * M_PI / 180.0;
  
  // Moon's latitude
  double moon_lat = 5.128 * sin(fmod(93.272 + 13.229350 * d, 360.0) * M_PI / 180.0);
  moon_lat = moon_lat * M_PI / 180.0;
  
  // Convert to equatorial coordinates
  double ecliptic_obliquity = 23.439 * M_PI / 180.0;
  
  double alpha = atan2(sin(moon_lon) * cos(ecliptic_obliquity) - tan(moon_lat) * sin(ecliptic_obliquity), 
                      cos(moon_lon));
  double delta = asin(sin(moon_lat) * cos(ecliptic_obliquity) + 
                     cos(moon_lat) * sin(ecliptic_obliquity) * sin(moon_lon));
  
  // Greenwich Sidereal Time
  double gst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  if (gst < 0) gst += 360.0;
  gst = gst * M_PI / 180.0;
  
  // Convert to radians
  double lat_rad = latitude_ * M_PI / 180.0;
  double lon_rad = longitude_ * M_PI / 180.0;
  
  // Local Hour Angle
  double H = gst + lon_rad - alpha;
  
  // Calculate altitude and azimuth
  pos.altitude = asin(sin(lat_rad) * sin(delta) + cos(lat_rad) * cos(delta) * cos(H));
  pos.altitude = pos.altitude * 180.0 / M_PI;
  
  double azimuth = atan2(sin(H), cos(H) * sin(lat_rad) - tan(delta) * cos(lat_rad));
  pos.azimuth = fmod(azimuth * 180.0 / M_PI + 180.0, 360.0);
  
  return pos;
}

LEDBrickScheduler::CelestialPosition LEDBrickScheduler::calculate_sun_position() const {
  if (!time_source_) {
    return {-90.0, 0.0};  // Default below horizon
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    return {-90.0, 0.0};
  }
  
  // Calculate Julian Day Number with time of day
  int year = time.year;
  int month = time.month;
  int day = time.day_of_month;
  
  if (month <= 2) {
    year--;
    month += 12;
  }
  
  int a = year / 100;
  int b = 2 - a + (a / 4);
  double jd = static_cast<double>(static_cast<int>(365.25 * (year + 4716)) + 
                                  static_cast<int>(30.6001 * (month + 1)) + 
                                  day + b - 1524.5);
  
  // Add time of day
  jd += (time.hour + time.minute / 60.0 + time.second / 3600.0) / 24.0;
  
  return calculate_sun_position_at_time(jd);
}

LEDBrickScheduler::CelestialPosition LEDBrickScheduler::calculate_sun_position_at_time(double julian_day) const {
  CelestialPosition pos = {-90.0, 0.0};  // Default below horizon
  
  // Days since J2000.0
  double n = julian_day - 2451545.0;
  
  // Mean longitude of Sun
  double L = fmod(280.460 + 0.98564736 * n, 360.0);
  if (L < 0) L += 360.0;
  
  // Mean anomaly of Sun
  double g = fmod(357.528 + 0.98560028 * n, 360.0);
  if (g < 0) g += 360.0;
  g = g * M_PI / 180.0;
  
  // Ecliptic longitude of Sun
  double lambda = L + 1.915 * sin(g) + 0.020 * sin(2.0 * g);
  lambda = fmod(lambda, 360.0);
  if (lambda < 0) lambda += 360.0;
  lambda = lambda * M_PI / 180.0;
  
  // Obliquity of ecliptic
  double epsilon = 23.439 * M_PI / 180.0;
  
  // Right ascension and declination
  double alpha = atan2(cos(epsilon) * sin(lambda), cos(lambda));
  double delta = asin(sin(epsilon) * sin(lambda));
  
  // Greenwich Sidereal Time
  double gst = fmod(280.46061837 + 360.98564736629 * n, 360.0);
  if (gst < 0) gst += 360.0;
  gst = gst * M_PI / 180.0;
  
  // Convert to radians
  double lat_rad = latitude_ * M_PI / 180.0;
  double lon_rad = longitude_ * M_PI / 180.0;
  
  // Local Hour Angle
  double H = gst + lon_rad - alpha;
  
  // Calculate altitude and azimuth
  pos.altitude = asin(sin(lat_rad) * sin(delta) + cos(lat_rad) * cos(delta) * cos(H));
  pos.altitude = pos.altitude * 180.0 / M_PI;
  
  double azimuth = atan2(sin(H), cos(H) * sin(lat_rad) - tan(delta) * cos(lat_rad));
  pos.azimuth = fmod(azimuth * 180.0 / M_PI + 180.0, 360.0);
  
  return pos;
}

float LEDBrickScheduler::get_moon_intensity() const {
  auto pos = calculate_moon_position();
  
  // If below horizon, return 0
  if (pos.altitude <= 0.0) {
    return 0.0f;
  }
  
  // Scale altitude from 0-90 degrees to 0.0-1.0 intensity
  // Use sine curve for more natural intensity scaling
  double altitude_rad = pos.altitude * M_PI / 180.0;
  float base_intensity = static_cast<float>(sin(altitude_rad));
  
  // Factor in moon phase - new moon is dim, full moon is bright
  float phase = get_moon_phase();
  float phase_brightness = 0.1f + 0.9f * (1.0f - abs(phase - 0.5f) * 2.0f);  // Peak at 0.5 (full moon)
  
  float intensity = base_intensity * phase_brightness;
  
  ESP_LOGVV(TAG, "Moon intensity: altitude=%.1f°, phase=%.3f, brightness=%.3f, final=%.3f", 
            pos.altitude, phase, phase_brightness, intensity);
  
  return intensity;
}

float LEDBrickScheduler::get_sun_intensity() const {
  auto pos = calculate_sun_position();
  
  // Handle different sun positions with atmospheric effects
  if (pos.altitude <= -6.0) {
    // Below civil twilight - complete darkness
    return 0.0f;
  } else if (pos.altitude <= 0.0) {
    // Twilight period - gradual transition from 0 to 0.1
    float twilight_factor = (pos.altitude + 6.0) / 6.0;  // 0.0 to 1.0
    return 0.1f * twilight_factor;
  } else if (pos.altitude <= 6.0) {
    // Dawn/dusk transition - from 0.1 to full intensity
    float dawn_factor = pos.altitude / 6.0;  // 0.0 to 1.0
    double altitude_rad = pos.altitude * M_PI / 180.0;
    float base_intensity = static_cast<float>(sin(altitude_rad));
    return 0.1f + (base_intensity - 0.1f) * dawn_factor;
  } else {
    // Above 6 degrees - full daylight calculation
    double altitude_rad = pos.altitude * M_PI / 180.0;
    float intensity = static_cast<float>(sin(altitude_rad));
    
    // Add slight atmospheric dimming for very low sun
    if (pos.altitude < 30.0) {
      float atm_factor = 0.7f + 0.3f * (pos.altitude / 30.0f);
      intensity *= atm_factor;
    }
    
    return intensity;
  }
}

double LEDBrickScheduler::get_projected_julian_day() const {
  if (!time_source_) {
    return 0.0;
  }
  
  auto time = time_source_->now();
  if (!time.is_valid()) {
    return 0.0;
  }
  
  // Calculate current Julian Day
  int year = time.year;
  int month = time.month;
  int day = time.day_of_month;
  
  if (month <= 2) {
    year--;
    month += 12;
  }
  
  int a = year / 100;
  int b = 2 - a + (a / 4);
  double jd = static_cast<double>(static_cast<int>(365.25 * (year + 4716)) + 
                                  static_cast<int>(30.6001 * (month + 1)) + 
                                  day + b - 1524.5);
  
  // Add current time of day
  jd += (time.hour + time.minute / 60.0 + time.second / 3600.0) / 24.0;
  
  if (!astronomical_projection_) {
    // No projection - return current time
    return jd;
  }
  
  // Apply time shift
  double shift_hours = time_shift_hours_ + time_shift_minutes_ / 60.0;
  jd += shift_hours / 24.0;  // Convert hours to days
  
  // For projection mode, we calculate what time it would be at the remote location
  // to get the same local solar time. This involves timezone offset calculation.
  
  // Calculate timezone offset between our local time and the target location
  // Rough approximation: 15 degrees longitude = 1 hour time difference
  double longitude_offset_hours = longitude_ / 15.0;  // Target location offset from GMT
  
  // We want remote location's solar time to map to our local clock time
  // So if it's 6:15 AM sunrise there, we want 6:15 AM here (in our timezone)
  // This means we calculate for what the sun position would be at the remote location
  // when their local solar time matches our current local clock time.
  
  // Apply the longitude-based time offset to synchronize local solar times
  jd -= longitude_offset_hours / 24.0;
  
  return jd;
}

float LEDBrickScheduler::get_projected_sun_intensity() const {
  if (!astronomical_projection_) {
    // Not using projection - return regular sun intensity
    return get_sun_intensity();
  }
  
  double projected_jd = get_projected_julian_day();
  if (projected_jd == 0.0) {
    return 0.0f;
  }
  
  auto pos = calculate_sun_position_at_time(projected_jd);
  
  // Same intensity calculation logic as regular sun intensity
  if (pos.altitude <= -6.0) {
    return 0.0f;
  } else if (pos.altitude <= 0.0) {
    float twilight_factor = (pos.altitude + 6.0) / 6.0;
    return 0.1f * twilight_factor;
  } else if (pos.altitude <= 6.0) {
    float dawn_factor = pos.altitude / 6.0;
    double altitude_rad = pos.altitude * M_PI / 180.0;
    float base_intensity = static_cast<float>(sin(altitude_rad));
    return 0.1f + (base_intensity - 0.1f) * dawn_factor;
  } else {
    double altitude_rad = pos.altitude * M_PI / 180.0;
    float intensity = static_cast<float>(sin(altitude_rad));
    
    if (pos.altitude < 30.0) {
      float atm_factor = 0.7f + 0.3f * (pos.altitude / 30.0f);
      intensity *= atm_factor;
    }
    
    ESP_LOGVV(TAG, "Projected sun intensity: altitude=%.1f°, shift=%dh%dm, intensity=%.3f", 
              pos.altitude, time_shift_hours_, time_shift_minutes_, intensity);
    
    return intensity;
  }
}

float LEDBrickScheduler::get_projected_moon_intensity() const {
  if (!astronomical_projection_) {
    // Not using projection - return regular moon intensity
    return get_moon_intensity();
  }
  
  double projected_jd = get_projected_julian_day();
  if (projected_jd == 0.0) {
    return 0.0f;
  }
  
  auto pos = calculate_moon_position_at_time(projected_jd);
  
  if (pos.altitude <= 0.0) {
    return 0.0f;
  }
  
  // Scale altitude from 0-90 degrees to 0.0-1.0 intensity
  double altitude_rad = pos.altitude * M_PI / 180.0;
  float base_intensity = static_cast<float>(sin(altitude_rad));
  
  // Factor in moon phase - we still use current phase, not projected phase
  float phase = get_moon_phase();
  float phase_brightness = 0.1f + 0.9f * (1.0f - abs(phase - 0.5f) * 2.0f);
  
  float intensity = base_intensity * phase_brightness;
  
  ESP_LOGVV(TAG, "Projected moon intensity: altitude=%.1f°, phase=%.3f, shift=%dh%dm, intensity=%.3f", 
            pos.altitude, phase, time_shift_hours_, time_shift_minutes_, intensity);
  
  return intensity;
}

} // namespace ledbrick_scheduler
} // namespace esphome