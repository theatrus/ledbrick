#include "ledbrick_scheduler.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/number/number.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace ledbrick_scheduler {

static const char *const TAG = "ledbrick_scheduler";

void LEDBrickScheduler::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LEDBrick Scheduler...");
  
  // Initialize presets
  initialize_presets();
  
  // Load default sunrise/sunset schedule if no points exist
  if (schedule_points_.empty()) {
    load_preset("sunrise_sunset");
  }
  
  // Register Home Assistant services
  register_services();
  
  ESP_LOGCONFIG(TAG, "LEDBrick Scheduler setup complete");
}

void LEDBrickScheduler::update() {
  if (!enabled_) {
    return;
  }
  
  // Get current values and apply them
  auto values = get_current_values();
  apply_values(values);
}

void LEDBrickScheduler::dump_config() {
  ESP_LOGCONFIG(TAG, "LEDBrick Scheduler:");
  ESP_LOGCONFIG(TAG, "  Channels: %u", num_channels_);
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", update_interval_);
  ESP_LOGCONFIG(TAG, "  Enabled: %s", enabled_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Schedule Points: %zu", schedule_points_.size());
  ESP_LOGCONFIG(TAG, "  Time Source: %s", time_source_ ? "CONFIGURED" : "NOT SET");
  
  if (time_source_) {
    uint16_t current_time = get_current_time_minutes();
    ESP_LOGCONFIG(TAG, "  Current Time: %02u:%02u (%u minutes)", 
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
  for (uint8_t channel = 0; channel < num_channels_; channel++) {
    // Apply PWM output
    auto pwm_it = pwm_outputs_.find(channel);
    if (pwm_it != pwm_outputs_.end() && pwm_it->second) {
      float pwm_level = values.pwm_values[channel] / 100.0f; // Convert percentage to 0-1
      pwm_it->second->set_level(pwm_level);
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
      
      // Set the current control value
      current_it->second->publish_state(target_current);
    }
  }
}

void LEDBrickScheduler::sort_schedule_points() {
  std::sort(schedule_points_.begin(), schedule_points_.end(),
           [](const SchedulePoint &a, const SchedulePoint &b) {
             return a.time_minutes < b.time_minutes;
           });
}

void LEDBrickScheduler::add_pwm_output(uint8_t channel, output::FloatOutput *output) {
  pwm_outputs_[channel] = output;
  ESP_LOGD(TAG, "Added PWM output for channel %u", channel);
}

void LEDBrickScheduler::add_current_control(uint8_t channel, number::Number *control) {
  current_controls_[channel] = control;
  ESP_LOGD(TAG, "Added current control for channel %u", channel);
}

void LEDBrickScheduler::add_max_current_control(uint8_t channel, number::Number *control) {
  max_current_controls_[channel] = control;
  ESP_LOGD(TAG, "Added max current control for channel %u", channel);
}

void LEDBrickScheduler::register_services() {
  // Register Home Assistant services
  register_service(&LEDBrickScheduler::on_set_schedule_point, "set_schedule_point",
                  {"time_minutes", "pwm_values", "current_values"});
  
  register_service(&LEDBrickScheduler::on_load_preset, "load_preset", {"preset_name"});
  
  register_service(&LEDBrickScheduler::on_set_enabled, "set_enabled", {"enabled"});
  
  register_service(&LEDBrickScheduler::on_get_status, "get_status");
}

void LEDBrickScheduler::on_set_schedule_point(int time_minutes, std::vector<float> pwm_values, 
                                             std::vector<float> current_values) {
  if (time_minutes < 0 || time_minutes >= 1440) {
    ESP_LOGW(TAG, "Invalid time_minutes: %d", time_minutes);
    return;
  }
  
  // Resize vectors to match channel count
  pwm_values.resize(num_channels_, 0.0f);
  current_values.resize(num_channels_, 0.0f);
  
  set_schedule_point(static_cast<uint16_t>(time_minutes), pwm_values, current_values);
}

void LEDBrickScheduler::on_load_preset(std::string preset_name) {
  load_preset(preset_name);
}

void LEDBrickScheduler::on_set_enabled(bool enabled) {
  set_enabled(enabled);
  ESP_LOGI(TAG, "Scheduler %s", enabled ? "enabled" : "disabled");
}

void LEDBrickScheduler::on_get_status() {
  uint16_t current_time = get_current_time_minutes();
  auto values = get_current_values();
  
  // Send status back via Home Assistant event  
  std::map<std::string, std::string> data;
  data["enabled"] = enabled_ ? "true" : "false";
  data["current_time"] = std::to_string(current_time);
  data["schedule_points"] = std::to_string(schedule_points_.size());
  data["channels"] = std::to_string(num_channels_);
  
  // Note: fire_homeassistant_event may need different implementation
  ESP_LOGI(TAG, "Status: enabled=%s, time=%u, points=%zu, channels=%u", 
           data["enabled"].c_str(), current_time, schedule_points_.size(), num_channels_);
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

} // namespace ledbrick_scheduler
} // namespace esphome