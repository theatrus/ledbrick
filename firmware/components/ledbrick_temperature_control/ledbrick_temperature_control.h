#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "../ledbrick_scheduler/temperature_control.h"
#include "../ledbrick_scheduler/ledbrick_scheduler.h"

namespace esphome {
namespace ledbrick_temperature_control {

// ESPHome Temperature Control Component
class LEDBrickTemperatureControl : public Component {
public:
    LEDBrickTemperatureControl() = default;
    
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
    
    // Configuration
    void set_scheduler(ledbrick_scheduler::LEDBrickScheduler *scheduler) { scheduler_ = scheduler; }
    void set_update_interval(uint32_t interval) { update_interval_ = interval; }
    
    // Temperature control configuration
    void set_target_temperature(float temp) { config_.target_temp_c = temp; }
    void set_pid_parameters(float kp, float ki, float kd) {
        config_.kp = kp;
        config_.ki = ki; 
        config_.kd = kd;
    }
    void set_fan_pwm_range(float min_pwm, float max_pwm) {
        config_.min_fan_pwm = min_pwm;
        config_.max_fan_pwm = max_pwm;
    }
    void set_emergency_temperatures(float emergency_temp, float recovery_temp) {
        config_.emergency_temp_c = emergency_temp;
        config_.recovery_temp_c = recovery_temp;
    }
    void set_emergency_delay(uint32_t delay_ms) { config_.emergency_delay_ms = delay_ms; }
    
    // Hardware connections
    void set_fan(fan::Fan *fan) { fan_ = fan; }
    void set_fan_power_switch(switch_::Switch *power_switch) { fan_power_switch_ = power_switch; }
    void set_fan_speed_sensor(sensor::Sensor *sensor) { fan_speed_sensor_ = sensor; }
    
    // Temperature sensor management
    void add_temperature_sensor(const std::string &name, sensor::Sensor *sensor);
    
    // Monitoring sensors for Home Assistant
    void set_current_temp_sensor(sensor::Sensor *sensor) { current_temp_sensor_ = sensor; }
    void set_target_temp_sensor(sensor::Sensor *sensor) { target_temp_sensor_ = sensor; }
    void set_fan_pwm_sensor(sensor::Sensor *sensor) { fan_pwm_sensor_ = sensor; }
    void set_pid_error_sensor(sensor::Sensor *sensor) { pid_error_sensor_ = sensor; }
    void set_pid_output_sensor(sensor::Sensor *sensor) { pid_output_sensor_ = sensor; }
    void set_thermal_emergency_sensor(binary_sensor::BinarySensor *sensor) { thermal_emergency_sensor_ = sensor; }
    void set_fan_enabled_sensor(binary_sensor::BinarySensor *sensor) { fan_enabled_sensor_ = sensor; }
    
    // Manual control
    void set_enable_switch(switch_::Switch *enable_switch) { enable_switch_ = enable_switch; }
    void set_target_temp_number(number::Number *target_temp_number) { target_temp_number_ = target_temp_number; }
    
    // Control methods
    void enable_temperature_control(bool enabled);
    void set_manual_target_temperature(float temp);
    void reset_pid();
    
    // Status access
    bool is_thermal_emergency() const { return temp_control_.is_thermal_emergency(); }
    ledbrick::TemperatureControlStatus get_status() const { return temp_control_.get_status(); }
    
    // Configuration access
    ledbrick::TemperatureControlConfig get_config() const { return temp_control_.get_config(); }
    std::string export_config_json() const { return temp_control_.export_config_json(); }
    bool import_config_json(const std::string& json);
    std::vector<ledbrick::TemperatureControl::FanCurvePoint> get_fan_curve() const { 
        return temp_control_.get_fan_curve(); 
    }

protected:
    ledbrick::TemperatureControl temp_control_;
    ledbrick::TemperatureControlConfig config_;
    ledbrick_scheduler::LEDBrickScheduler *scheduler_{nullptr};
    
    uint32_t update_interval_{1000}; // 1 second default
    uint32_t last_update_{0};
    uint32_t last_sensor_publish_{0};
    
    // Hardware components
    fan::Fan *fan_{nullptr};
    switch_::Switch *fan_power_switch_{nullptr};
    sensor::Sensor *fan_speed_sensor_{nullptr};
    switch_::Switch *enable_switch_{nullptr};
    number::Number *target_temp_number_{nullptr};
    
    // Temperature sensors
    struct TempSensorMapping {
        std::string name;
        sensor::Sensor *sensor;
    };
    std::vector<TempSensorMapping> temp_sensors_;
    
    // Monitoring sensors
    sensor::Sensor *current_temp_sensor_{nullptr};
    sensor::Sensor *target_temp_sensor_{nullptr};
    sensor::Sensor *fan_pwm_sensor_{nullptr};
    sensor::Sensor *pid_error_sensor_{nullptr};
    sensor::Sensor *pid_output_sensor_{nullptr};
    binary_sensor::BinarySensor *thermal_emergency_sensor_{nullptr};
    binary_sensor::BinarySensor *fan_enabled_sensor_{nullptr};
    
    // Callbacks for temperature control
    void on_fan_pwm_change_(float pwm);
    void on_fan_enable_change_(bool enabled);
    void on_emergency_change_(bool emergency);
    
    // Sensor value updates
    void update_temperature_sensors_();
    void update_fan_speed_();
    void publish_sensor_values_();
    
    // Persistence
    void save_config_();
    void load_config_();
    ESPPreferenceObject preferences_;
    uint32_t config_hash_{0};
    
    bool initialized_{false};
};

// Switch for enabling/disabling temperature control
class TemperatureControlEnableSwitch : public switch_::Switch {
public:
    void set_temperature_control(LEDBrickTemperatureControl *temp_control) {
        temp_control_ = temp_control;
    }
    
protected:
    void write_state(bool state) override {
        if (temp_control_) {
            temp_control_->enable_temperature_control(state);
        }
        this->publish_state(state);
    }
    
    LEDBrickTemperatureControl *temp_control_{nullptr};
};

// Number component for target temperature setting
class TargetTemperatureNumber : public number::Number {
public:
    void set_temperature_control(LEDBrickTemperatureControl *temp_control) {
        temp_control_ = temp_control;
    }
    
protected:
    void control(float value) override {
        if (temp_control_) {
            temp_control_->set_manual_target_temperature(value);
        }
        this->publish_state(value);
    }
    
    LEDBrickTemperatureControl *temp_control_{nullptr};
};

} // namespace ledbrick_temperature_control
} // namespace esphome