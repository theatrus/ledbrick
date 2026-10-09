#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/time.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "astronomical_calculator.h"  // Include our standalone calculator
#include "scheduler.h"  // Include standalone scheduler
#include "temperature_control.h"  // Include temperature control
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/switch/switch.h"
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
  // Temperature sensor mapping struct
  struct TempSensorMapping {
    std::string name;
    sensor::Sensor *sensor;
  };

  LEDBrickScheduler() : PollingComponent() {}
  explicit LEDBrickScheduler(uint32_t update_interval) : PollingComponent(update_interval) {}
  
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Configuration
  void set_num_channels(uint8_t channels) { num_channels_ = channels; }
  void set_time_source(time::RealTimeClock *time_source) { time_source_ = time_source; }
  void set_timezone(const std::string &timezone) { 
    timezone_ = timezone; 
    // Convert timezone string to numeric offset for astronomical calculations
    if (timezone == "America/Los_Angeles" || timezone == "America/Vancouver" || timezone == "US/Pacific") {
      // Pacific Time: PST (UTC-8) in winter, PDT (UTC-7) in summer
      // For simplicity, use PST (UTC-8) - this should ideally check DST
      timezone_offset_hours_ = -8.0;
    } else if (timezone == "America/Denver" || timezone == "US/Mountain") {
      timezone_offset_hours_ = -7.0; // MST
    } else if (timezone == "America/Chicago" || timezone == "US/Central") {
      timezone_offset_hours_ = -6.0; // CST
    } else if (timezone == "America/New_York" || timezone == "US/Eastern") {
      timezone_offset_hours_ = -5.0; // EST
    } else {
      // Default to UTC if timezone not recognized
      timezone_offset_hours_ = 0.0;
    }
  }
  void set_location(double latitude, double longitude);
  double get_latitude() const { return latitude_; }
  double get_longitude() const { return longitude_; }
  void set_astronomical_projection(bool enabled);
  bool is_astronomical_projection_enabled() const { return astronomical_projection_; }
  void set_time_shift(int hours, int minutes);
  int get_time_shift_hours() const { return time_shift_hours_; }
  int get_time_shift_minutes() const { return time_shift_minutes_; }
  void set_timezone_offset_hours(double hours) { timezone_offset_hours_ = hours; }
  double get_timezone_offset_hours() const { return timezone_offset_hours_; }
  const std::string& get_timezone() const { return timezone_; }
  time::RealTimeClock* get_time_source() const { return time_source_; }

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
  // Returns false, leaving the schedule unchanged, for an unknown preset. Saves to flash
  // unless save is false, for a caller that saves itself and reports a failed save.
  bool load_preset(const std::string &preset_name, bool save = true);
  void save_preset(const std::string &preset_name);
  
  // Persistent storage (ESPHome-specific)
  // Returns false when the schedule is too large to store; the previous copy is kept
  bool save_schedule_to_flash();
  void load_schedule_from_flash();
  // Custom LED models have their own record: they change rarely, and the schedule's
  // record and API stay small without them
  bool save_led_models_to_flash();
  void load_led_models_from_flash();
  // for_storage writes compact JSON without volatile fields such as the current time
  void export_schedule_json(std::string &json_output, bool for_storage = false) const;
  // Nothing changes unless the whole document is valid. from_flash skips invalid
  // settings fields (with a warning) instead of rejecting the saved schedule.
  bool import_schedule_json(const std::string &json_input, std::string *error = nullptr, bool from_flash = false);
  
  // Control
  void set_enabled(bool enabled);
  bool is_enabled() const { return enabled_; }
  
  // Thermal emergency control
  void set_thermal_emergency(bool emergency);
  // Clears a latched emergency by hand, e.g. after a sensor failed during one. Refused
  // (returns false) while a working sensor reads above the recovery temperature.
  bool reset_thermal_emergency();
  bool is_thermal_emergency() const { return temp_hardware_.get_hardware_state().thermal_emergency; }
  const ledbrick::TemperatureHardwareState& get_temperature_hardware_state() const { return temp_hardware_.get_hardware_state(); }
  void force_channel_output(uint8_t channel, float pwm, float current);
  size_t get_schedule_size() const { return scheduler_.get_schedule_size(); }
  
  // PWM scaling. Returns false when the new scale applies but could not be saved
  bool set_pwm_scale(float scale);
  float get_pwm_scale() const { return pwm_scale_; }
  
  // Moon simulation - with auto-save
  void enable_moon_simulation(bool enabled);
  void set_moon_base_intensity(const std::vector<float>& intensity);
  void set_moon_base_current(const std::vector<float>& current);
  void set_moon_simulation(const LEDScheduler::MoonSimulation& config);
  bool is_moon_simulation_enabled() const { return scheduler_.get_moon_simulation().enabled; }
  LEDScheduler::MoonSimulation get_moon_simulation() const { return scheduler_.get_moon_simulation(); }
  
  // Channel configuration
  void set_channel_config(uint8_t channel, const LEDScheduler::ChannelConfig& config);
  LEDScheduler::ChannelConfig get_channel_config(uint8_t channel) const { return scheduler_.get_channel_config(channel); }
  void set_channel_color(uint8_t channel, const std::string& rgb_hex);
  void set_channel_max_current(uint8_t channel, float max_current);
  std::string get_channel_color(uint8_t channel) const { return scheduler_.get_channel_color(channel); }
  
  // Color sensor management
  void add_color_text_sensor(uint8_t channel, text_sensor::TextSensor *sensor);
  void update_color_sensors();
  float get_channel_max_current(uint8_t channel) const { return scheduler_.get_channel_max_current(channel); }
  uint8_t get_num_channels() const { return num_channels_; }
  
  // Temperature control integration
  void add_temperature_sensor(const std::string &name, sensor::Sensor *sensor);
  void set_fan(fan::Fan *fan) { fan_ = fan; }
  void set_fan_power_switch(switch_::Switch *fan_switch) { fan_power_switch_ = fan_switch; }
  void set_fan_speed_sensor(sensor::Sensor *sensor) { fan_speed_sensor_ = sensor; }
  void enable_temperature_control(bool enabled);
  const ledbrick::TemperatureControlStatus& get_temperature_status() const { return temp_control_.get_status(); }
  std::string get_temperature_config_json() const { return temp_control_.export_config_json(); }
  bool set_temperature_config_json(const std::string& json, std::string *error = nullptr);
  std::vector<ledbrick::TemperatureControl::FanCurvePoint> get_fan_curve() const { return temp_control_.get_fan_curve(); }
  const std::vector<TempSensorMapping>& get_temperature_sensors() const { return temp_sensors_; }
  bool is_temperature_control_initialized() const { return temp_control_initialized_; }
  
  // Current state
  bool has_valid_time() const;
  uint16_t get_current_time_minutes() const;  // 0 when the time is not valid
  uint32_t get_current_time_seconds() const;  // Second of the day; 0 when the time is not valid
  InterpolationResult get_current_values() const;
  InterpolationResult get_actual_channel_values() const;
  
  // Manual channel control (only works when scheduler is disabled and there is no thermal emergency)
  bool set_channel_manual_control(uint8_t channel, float pwm, float current);
  // Manual control of a curve-mode channel by level (0-1), through its LED curves
  bool set_channel_manual_level(uint8_t channel, float level);

  // Dimming (led_dimming.h). Changing the mode converts the channel's schedule; the caller saves.
  bool set_channel_dimming(uint8_t channel, ledbrick::DimMode mode, ledbrick::DimPriority priority,
                           float floor_current, const std::vector<ledbrick::LedGroup> &leds, std::string *error);
  bool is_curve_channel(uint8_t channel) const { return scheduler_.is_curve_channel(channel); }
  // Replaces the custom LED models from {"led_models":[...]} and saves them. False with
  // save_failed clear: rejected, nothing changed. With save_failed set: applied, not saved.
  bool set_led_models_json(const std::string &json, std::string *error, bool *save_failed);
  // Every model with its curves, or with custom_only the custom ones as {"led_models":[...]}
  std::string get_led_models_json(bool custom_only = false) const {
    return scheduler_.export_led_models_json(custom_only);
  }
  size_t get_custom_led_model_count() const { return scheduler_.get_custom_led_models().size(); }
  // Level (0-1) a curve-mode channel was last driven at; negative for manual channels
  float get_channel_level(uint8_t channel) const;
  // Temperature used for the LED curves: the board sensors' filtered average, or the
  // 25 C reference (no compensation) without a working sensor
  float get_led_temperature_c() const;
  
  // Update timezone offset from time source
  void update_timezone_from_time_source();
  
  // Astronomical functions (delegated to standalone calculator)
  float get_moon_phase() const;  // Position in the lunar cycle, 0.0-1.0 (0=new moon, 0.5=full moon)
  float get_moon_illumination() const;  // Share of the moon that is lit, 0.0-1.0
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
  
  // Debug helpers for the web API
  AstronomicalCalculator::SunTimes get_astronomical_times() const { return get_sun_rise_set_times(); }
  AstronomicalCalculator::SunTimes get_projected_astronomical_times() const { return get_projected_sun_rise_set_times(); }
  
  // Get info about a specific schedule point
  struct SchedulePointInfo {
    std::string time_type;
    int offset_minutes;
    int time_minutes;
  };
  SchedulePointInfo get_schedule_point_info(size_t index) const;
  
  // External entity references
  void add_light(uint8_t channel, light::LightState *light);
  void add_current_control(uint8_t channel, number::Number *control);
  void add_max_current_control(uint8_t channel, number::Number *control);
  
  // Temperature control sensors
  void set_current_temp_sensor(sensor::Sensor *sensor) { current_temp_sensor_ = sensor; }
  void set_target_temp_sensor(sensor::Sensor *sensor) { target_temp_sensor_ = sensor; }
  void set_fan_pwm_sensor(sensor::Sensor *sensor) { fan_pwm_sensor_ = sensor; }
  void set_pid_error_sensor(sensor::Sensor *sensor) { pid_error_sensor_ = sensor; }
  void set_pid_output_sensor(sensor::Sensor *sensor) { pid_output_sensor_ = sensor; }
  void set_thermal_emergency_sensor(binary_sensor::BinarySensor *sensor) { thermal_emergency_sensor_ = sensor; }
  void set_fan_enabled_sensor(binary_sensor::BinarySensor *sensor) { fan_enabled_sensor_ = sensor; }
  void set_target_temp_number(number::Number *number) { target_temp_number_ = number; }
  void set_enable_switch(switch_::Switch *sw) { temp_enable_switch_ = sw; }

 protected:
  uint8_t num_channels_{8};
  bool enabled_{true};
  // thermal_emergency_ removed - now tracked by temp_hardware_.get_hardware_state().thermal_emergency
  bool force_next_update_{false};   // Force update after emergency recovery
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
  mutable AstronomicalCalculator astro_calc_{0.0, 0.0, 0.0};  // Astronomical calculations (lat, lon, tz)
  LEDScheduler scheduler_;  // Core scheduling logic
  ledbrick::TemperatureControl temp_control_;  // Temperature control system
  ledbrick::TemperatureHardwareManager temp_hardware_;  // Hardware state manager
  
  time::RealTimeClock *time_source_{nullptr};
  
  // Persistent storage
  ESPPreferenceObject schedule_pref_;
  ESPPreferenceObject emergency_pref_;
  static constexpr uint32_t EMERGENCY_HASH = 0x4C54454D;  // 'LTEM'
  bool temp_control_enable_requested_{false};  // The enable switch restored a setting before setup()
  // Use more unique hash based on component name
  static constexpr uint32_t SCHEDULE_HASH = 0x4C454453;  // 'LEDS' in hex
  
  // Use a larger storage for JSON format to include all settings
  struct ScheduleStorage {
    uint32_t version;  // Storage format version
    uint32_t json_length;  // Length of JSON data
    char json_data[8192];  // JSON storage (8KB should be enough)
  };

  ESPPreferenceObject led_models_pref_;
  static constexpr uint32_t LED_MODELS_HASH = 0x4C45444D;  // 'LEDM'
  // The custom models as {"led_models":[...]}. Written only when they change.
  struct LedModelsStorage {
    uint32_t version;
    uint32_t json_length;
    char json_data[12288];
  };
  
  
  // Entity references
  std::map<uint8_t, light::LightState*> lights_;
  std::map<uint8_t, number::Number*> current_controls_;
  std::map<uint8_t, number::Number*> max_current_controls_;
  std::map<uint8_t, text_sensor::TextSensor*> color_text_sensors_;
  
  // Temperature control entities
  std::vector<TempSensorMapping> temp_sensors_;
  fan::Fan *fan_{nullptr};
  switch_::Switch *fan_power_switch_{nullptr};
  sensor::Sensor *fan_speed_sensor_{nullptr};
  
  // Temperature monitoring sensors
  sensor::Sensor *current_temp_sensor_{nullptr};
  sensor::Sensor *target_temp_sensor_{nullptr};
  sensor::Sensor *fan_pwm_sensor_{nullptr};
  sensor::Sensor *pid_error_sensor_{nullptr};
  sensor::Sensor *pid_output_sensor_{nullptr};
  binary_sensor::BinarySensor *thermal_emergency_sensor_{nullptr};
  binary_sensor::BinarySensor *fan_enabled_sensor_{nullptr};
  number::Number *target_temp_number_{nullptr};
  switch_::Switch *temp_enable_switch_{nullptr};
  
  // Temperature control state
  bool temp_control_initialized_{false};
  uint32_t last_temp_update_{0};
  uint32_t last_sensor_publish_{0};
  ledbrick::TemperatureControlConfig temp_config_;
  
  // Boot state management to prevent flash save race conditions
  bool boot_complete_{false};
  bool save_pending_{false};
  bool flash_load_failed_{false};  // A saved schedule exists but could not be loaded
  uint32_t last_time_warning_ms_{0};
  
  // Timers and last-seen values for periodic work and logging
  uint32_t last_log_time_ms_{0};
  bool was_emergency_{false};
  uint32_t recovery_time_ms_{0};
  uint32_t last_tz_update_ms_{0};
  uint32_t last_astro_update_ms_{0};
  uint32_t last_discovery_check_ms_{0};
  std::vector<float> last_pwm_values_;
  std::vector<float> last_current_values_;
  std::vector<float> last_levels_;  // curve channels: level last driven (0-1); -1 for manual
  std::map<uint8_t, std::string> last_colors_;
  
  // Internal methods
  void apply_values(const InterpolationResult &values);
  
  // Rise and set times depend only on the date and settings, so compute them once per day
  struct DailyAstroTimes {
    bool valid{false};
    int year{0}, month{0}, day{0};
    double latitude{0.0}, longitude{0.0}, timezone_offset{0.0};
    bool projection{false};
    int shift_hours{0}, shift_minutes{0};
    AstronomicalCalculator::SunTimes sun, projected_sun;
    AstronomicalCalculator::MoonTimes moon, projected_moon;
  };
  mutable DailyAstroTimes daily_astro_;
  const DailyAstroTimes &get_daily_astro_times_() const;
  
  // Helper to convert ESPHome time to AstronomicalCalculator::DateTime
  AstronomicalCalculator::DateTime esphome_time_to_datetime() const;
  
  // Update the astronomical calculator settings when location/projection changes
  void update_astro_calculator_settings() const;
  void update_astronomical_times_for_scheduler(bool force = false);
  
  // Built-in presets (use astronomical data for sunrise/sunset)
  void create_sunrise_sunset_preset_with_astro_data() const;
  
  // Temperature control methods
  void register_temperature_sensor_(const std::string &name, sensor::Sensor *sensor);
  void on_temperature_reading_(const std::string &name, float temp);
  void update_temperature_sensors();
  void update_fan_speed();
  void publish_temp_sensor_values();
  void on_fan_pwm_change(float pwm);
  void on_fan_enable_change(bool enabled);
  void on_emergency_change(bool emergency);
};

// Automation actions
template<typename... Ts> class SetSchedulePointAction : public Action<Ts...>, public Parented<LEDBrickScheduler> {
 public:
  TEMPLATABLE_VALUE(uint16_t, timepoint)
  TEMPLATABLE_VALUE(std::vector<float>, pwm_values)
  TEMPLATABLE_VALUE(std::vector<float>, current_values)

  void play(const Ts &...x) override {
    auto timepoint = this->timepoint_.value(x...);
    auto pwm_values = this->pwm_values_.value(x...);
    auto current_values = this->current_values_.value(x...);
    
    this->parent_->set_schedule_point(timepoint, pwm_values, current_values);
  }
};

template<typename... Ts> class LoadPresetAction : public Action<Ts...>, public Parented<LEDBrickScheduler> {
 public:
  TEMPLATABLE_VALUE(std::string, preset_name)

  void play(const Ts &...x) override {
    auto preset_name = this->preset_name_.value(x...);
    this->parent_->load_preset(preset_name);
  }
};

template<typename... Ts> class SetEnabledAction : public Action<Ts...>, public Parented<LEDBrickScheduler> {
 public:
  TEMPLATABLE_VALUE(bool, enabled)

  void play(const Ts &...x) override {
    auto enabled = this->enabled_.value(x...);
    this->parent_->set_enabled(enabled);
  }
};

} // namespace ledbrick_scheduler
} // namespace esphome