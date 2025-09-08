#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/time.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "astronomical_calculator.h"  // Include our standalone calculator
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

class LEDBrickScheduler : public PollingComponent {
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
  void set_timezone(const std::string &timezone) { timezone_ = timezone; }
  void set_location(double latitude, double longitude) { 
    latitude_ = latitude; 
    longitude_ = longitude; 
  }
  void set_astronomical_projection(bool enabled) { astronomical_projection_ = enabled; }
  void set_time_shift(int hours, int minutes) { 
    time_shift_hours_ = hours; 
    time_shift_minutes_ = minutes; 
  }
  void set_timezone_offset_hours(double hours) { timezone_offset_hours_ = hours; }

  // Schedule management
  void add_schedule_point(const SchedulePoint &point);
  void set_schedule_point(uint16_t time_minutes, const std::vector<float> &pwm_values, 
                         const std::vector<float> &current_values);
  void remove_schedule_point(uint16_t time_minutes);
  void clear_schedule();
  
  // Preset management
  void load_preset(const std::string &preset_name);
  void save_preset(const std::string &preset_name);
  
  // Persistent storage
  void save_schedule_to_flash();
  void load_schedule_from_flash();
  void export_schedule_json(std::string &json_output) const;
  bool import_schedule_json(const std::string &json_input);
  
  // Control
  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool is_enabled() const { return enabled_; }
  
  // Current state
  uint16_t get_current_time_minutes() const;
  InterpolationResult get_current_values() const;
  
  // Astronomical functions (delegated to standalone calculator)
  float get_moon_phase() const;  // Returns 0.0-1.0 (0=new moon, 0.5=full moon)
  float get_moon_intensity() const;  // Returns 0.0-1.0 based on altitude (0=below horizon, 1=overhead)
  float get_sun_intensity() const;   // Returns 0.0-1.0 based on altitude (0=below horizon, 1=overhead)
  
  // Projected astronomical functions (with time shift mapping)
  float get_projected_sun_intensity() const;  // Sun intensity with optional time projection
  float get_projected_moon_intensity() const; // Moon intensity with optional time projection
  
  // Use the standalone calculator's MoonTimes and SunTimes structs
  AstronomicalCalculator::MoonTimes get_moon_rise_set_times() const;
  AstronomicalCalculator::SunTimes get_sun_rise_set_times() const;
  
  // Projected rise/set times (with time shift if projection enabled)
  AstronomicalCalculator::SunTimes get_projected_sun_rise_set_times() const;
  
  // External entity references
  void add_light(uint8_t channel, light::LightState *light);
  void add_current_control(uint8_t channel, number::Number *control);
  void add_max_current_control(uint8_t channel, number::Number *control);

 protected:
  uint8_t num_channels_{8};
  uint32_t update_interval_{1000}; // 1 second for smooth transitions
  bool enabled_{true};
  std::string timezone_{"UTC"};
  
  // Geographic location for astronomical calculations
  double latitude_{37.7749};   // Default: San Francisco
  double longitude_{-122.4194};
  
  // Astronomical time projection settings
  bool astronomical_projection_{false};  // Enable time projection mode
  int time_shift_hours_{0};              // Additional time shift in hours
  int time_shift_minutes_{0};            // Additional time shift in minutes
  double timezone_offset_hours_{0.0};    // Timezone offset from UTC in hours
  
  // Standalone astronomical calculator instance
  mutable AstronomicalCalculator astro_calc_;  // Mutable to allow updates from const methods
  
  time::RealTimeClock *time_source_{nullptr};
  
  // Persistent storage
  ESPPreferenceObject schedule_pref_;
  static constexpr uint32_t SCHEDULE_HASH = 0x12345678;  // Storage identifier
  
  struct ScheduleStorage {
    uint16_t num_points;
    uint8_t data[3800];  // Flexible storage for points
  };
  
  // Schedule storage
  std::vector<SchedulePoint> schedule_points_;
  mutable std::map<std::string, std::vector<SchedulePoint>> presets_;
  
  // Entity references
  std::map<uint8_t, light::LightState*> lights_;
  std::map<uint8_t, number::Number*> current_controls_;
  std::map<uint8_t, number::Number*> max_current_controls_;
  
  // Internal methods
  InterpolationResult interpolate_values(uint16_t current_time) const;
  void apply_values(const InterpolationResult &values);
  void sort_schedule_points();
  void parse_float_array(const std::string &array_str, std::vector<float> &values) const;
  
  // Helper to convert ESPHome time to AstronomicalCalculator::DateTime
  AstronomicalCalculator::DateTime esphome_time_to_datetime() const;
  
  // Update the astronomical calculator settings when location/projection changes
  void update_astro_calculator_settings() const;
  
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