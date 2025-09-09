#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/time.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "astronomical_calculator.h"  // Include our standalone calculator
#include "scheduler.h"  // Include standalone scheduler
#include <vector>
#include <map>
#include <string>

namespace esphome {
namespace ledbrick_scheduler {

// Use the standalone scheduler types
using SchedulePoint = LEDScheduler::SchedulePoint;
using InterpolationResult = LEDScheduler::InterpolationResult;

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
  void set_location(double latitude, double longitude);
  double get_latitude() const { return latitude_; }
  double get_longitude() const { return longitude_; }
  void set_astronomical_projection(bool enabled);
  bool is_astronomical_projection_enabled() const { return astronomical_projection_; }
  void set_time_shift(int hours, int minutes);
  int get_time_shift_hours() const { return time_shift_hours_; }
  int get_time_shift_minutes() const { return time_shift_minutes_; }
  void set_timezone_offset_hours(double hours) { timezone_offset_hours_ = hours; }

  // Schedule management (delegates to standalone scheduler)
  void add_schedule_point(const SchedulePoint &point);
  void set_schedule_point(uint16_t time_minutes, const std::vector<float> &pwm_values, 
                         const std::vector<float> &current_values);
  void add_dynamic_schedule_point(LEDScheduler::DynamicTimeType type, int16_t offset_minutes,
                                 const std::vector<float> &pwm_values,
                                 const std::vector<float> &current_values);
  void remove_schedule_point(uint16_t time_minutes);
  void remove_dynamic_schedule_point(LEDScheduler::DynamicTimeType type, int16_t offset_minutes);
  void clear_schedule();
  
  // Preset management (delegates to standalone scheduler)
  void load_preset(const std::string &preset_name);
  void save_preset(const std::string &preset_name);
  
  // Persistent storage (ESPHome-specific)
  void save_schedule_to_flash();
  void load_schedule_from_flash();
  void export_schedule_json(std::string &json_output) const;
  bool import_schedule_json(const std::string &json_input);
  
  // Control
  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool is_enabled() const { return enabled_; }
  size_t get_schedule_size() const { return scheduler_.get_schedule_size(); }
  
  // PWM scaling
  void set_pwm_scale(float scale);
  float get_pwm_scale() const { return pwm_scale_; }
  
  // Moon simulation - with auto-save
  void enable_moon_simulation(bool enabled);
  void set_moon_base_intensity(const std::vector<float>& intensity);
  void set_moon_simulation(const LEDScheduler::MoonSimulation& config);
  bool is_moon_simulation_enabled() const { return scheduler_.get_moon_simulation().enabled; }
  LEDScheduler::MoonSimulation get_moon_simulation() const { return scheduler_.get_moon_simulation(); }
  
  // Channel configuration
  void set_channel_config(uint8_t channel, const LEDScheduler::ChannelConfig& config) { scheduler_.set_channel_config(channel, config); }
  LEDScheduler::ChannelConfig get_channel_config(uint8_t channel) const { return scheduler_.get_channel_config(channel); }
  void set_channel_color(uint8_t channel, const std::string& rgb_hex) { scheduler_.set_channel_color(channel, rgb_hex); }
  void set_channel_max_current(uint8_t channel, float max_current);
  std::string get_channel_color(uint8_t channel) const { return scheduler_.get_channel_color(channel); }
  float get_channel_max_current(uint8_t channel) const { return scheduler_.get_channel_max_current(channel); }
  
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
  float pwm_scale_{1.0f};  // Global PWM scaling factor (0.0-1.0)
  
  // Geographic location for astronomical calculations
  double latitude_{37.7749};   // Default: San Francisco
  double longitude_{-122.4194};
  
  // Astronomical time projection settings
  bool astronomical_projection_{false};  // Enable time projection mode
  int time_shift_hours_{0};              // Additional time shift in hours
  int time_shift_minutes_{0};            // Additional time shift in minutes
  double timezone_offset_hours_{0.0};    // Timezone offset from UTC in hours
  
  // Standalone components
  mutable AstronomicalCalculator astro_calc_;  // Astronomical calculations
  LEDScheduler scheduler_;  // Core scheduling logic
  
  time::RealTimeClock *time_source_{nullptr};
  
  // Persistent storage
  ESPPreferenceObject schedule_pref_;
  static constexpr uint32_t SCHEDULE_HASH = 0x12345678;  // Storage identifier
  
  struct ScheduleStorage {
    uint16_t num_points;
    uint8_t data[3800];  // Flexible storage for points
  };
  
  // Entity references
  std::map<uint8_t, light::LightState*> lights_;
  std::map<uint8_t, number::Number*> current_controls_;
  std::map<uint8_t, number::Number*> max_current_controls_;
  
  // Internal methods
  void apply_values(const InterpolationResult &values);
  
  // Helper to convert ESPHome time to AstronomicalCalculator::DateTime
  AstronomicalCalculator::DateTime esphome_time_to_datetime() const;
  
  // Update the astronomical calculator settings when location/projection changes
  void update_astro_calculator_settings() const;
  void update_astronomical_times_for_scheduler();
  
  // Built-in presets (use astronomical data for sunrise/sunset)
  void create_sunrise_sunset_preset_with_astro_data() const;
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