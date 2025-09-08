#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/time.h"
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/number/number.h"
#include <vector>
#include <map>
#include <string>

namespace esphome {
namespace ledbrick_scheduler {

struct SchedulePoint {
  uint16_t time_minutes;  // 0-1439 (minutes from midnight)
  std::vector<float> pwm_values;     // PWM percentages (0-100) per channel
  std::vector<float> current_values; // Current values (0-5A) per channel
  
  SchedulePoint() : time_minutes(0) {}
  SchedulePoint(uint16_t time, std::vector<float> pwm, std::vector<float> current) 
    : time_minutes(time), pwm_values(std::move(pwm)), current_values(std::move(current)) {}
};

struct InterpolationResult {
  std::vector<float> pwm_values;
  std::vector<float> current_values;
};

class LEDBrickScheduler : public PollingComponent, public api::CustomAPIDevice {
 public:
  LEDBrickScheduler() : PollingComponent() {}
  explicit LEDBrickScheduler(uint32_t update_interval) : PollingComponent(update_interval) {}
  
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Configuration
  void set_num_channels(uint8_t channels) { num_channels_ = channels; }
  void set_update_interval(uint32_t interval_ms) { update_interval_ = interval_ms; }
  void set_time_source(time::RealTimeClock *time_source) { time_source_ = time_source; }

  // Schedule management
  void add_schedule_point(const SchedulePoint &point);
  void set_schedule_point(uint16_t time_minutes, const std::vector<float> &pwm_values, 
                         const std::vector<float> &current_values);
  void remove_schedule_point(uint16_t time_minutes);
  void clear_schedule();
  
  // Preset management
  void load_preset(const std::string &preset_name);
  void save_preset(const std::string &preset_name);
  
  // Control
  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool is_enabled() const { return enabled_; }
  
  // Current state
  uint16_t get_current_time_minutes() const;
  InterpolationResult get_current_values() const;
  
  // External output references
  void add_pwm_output(uint8_t channel, output::FloatOutput *output);
  void add_current_control(uint8_t channel, number::Number *control);
  void add_max_current_control(uint8_t channel, number::Number *control);

 protected:
  uint8_t num_channels_{8};
  uint32_t update_interval_{30000}; // 30 seconds
  bool enabled_{true};
  
  time::RealTimeClock *time_source_{nullptr};
  
  // Schedule storage
  std::vector<SchedulePoint> schedule_points_;
  mutable std::map<std::string, std::vector<SchedulePoint>> presets_;
  
  // Output references
  std::map<uint8_t, output::FloatOutput*> pwm_outputs_;
  std::map<uint8_t, number::Number*> current_controls_;
  std::map<uint8_t, number::Number*> max_current_controls_;
  
  // Internal methods
  InterpolationResult interpolate_values(uint16_t current_time) const;
  void apply_values(const InterpolationResult &values);
  void sort_schedule_points();
  void register_services();
  
  // Service handlers
  void on_set_schedule_point(int time_minutes, std::vector<float> pwm_values, std::vector<float> current_values);
  void on_load_preset(std::string preset_name);
  void on_set_enabled(bool enabled);
  void on_get_status();
  
  // Built-in presets
  void initialize_presets();
  std::vector<SchedulePoint> create_sunrise_sunset_preset() const;
  std::vector<SchedulePoint> create_full_spectrum_preset() const;
  std::vector<SchedulePoint> create_simple_preset() const;
};

// Automation actions
template<typename... Ts> class SetSchedulePointAction : public Action<Ts...>, public Parented<LEDBrickScheduler> {
 public:
  TEMPLATABLE_VALUE(uint16_t, timepoint)
  TEMPLATABLE_VALUE(std::vector<float>, pwm_values)
  TEMPLATABLE_VALUE(std::vector<float>, current_values)

  void play(Ts... x) override {
    auto timepoint = this->timepoint_.value(x...);
    auto pwm_values = this->pwm_values_.value(x...);
    auto current_values = this->current_values_.value(x...);
    
    this->parent_->set_schedule_point(timepoint, pwm_values, current_values);
  }
};

template<typename... Ts> class LoadPresetAction : public Action<Ts...>, public Parented<LEDBrickScheduler> {
 public:
  TEMPLATABLE_VALUE(std::string, preset_name)

  void play(Ts... x) override {
    auto preset_name = this->preset_name_.value(x...);
    this->parent_->load_preset(preset_name);
  }
};

template<typename... Ts> class SetEnabledAction : public Action<Ts...>, public Parented<LEDBrickScheduler> {
 public:
  TEMPLATABLE_VALUE(bool, enabled)

  void play(Ts... x) override {
    auto enabled = this->enabled_.value(x...);
    this->parent_->set_enabled(enabled);
  }
};

} // namespace ledbrick_scheduler
} // namespace esphome