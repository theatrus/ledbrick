#pragma once

#include <vector>
#include <functional>
#include <cstdint>
#include <string>
#include "pid_controller.h"

namespace ledbrick {

// Temperature sensor interface
struct TemperatureSensor {
    std::string name;
    float temperature_c;
    bool valid;
    uint32_t last_update_ms;
};

// Using standalone PIDController from pid_controller.h

// Temperature control configuration
struct TemperatureControlConfig {
    // PID tuning parameters
    float target_temp_c = 45.0f;          // Target temperature in Celsius
    float kp = 2.0f;                      // Proportional gain
    float ki = 0.1f;                      // Integral gain  
    float kd = 0.5f;                      // Derivative gain
    
    // Fan control parameters
    float min_fan_pwm = 0.0f;             // Minimum fan PWM (0-100%)
    float max_fan_pwm = 100.0f;           // Maximum fan PWM (0-100%)
    uint32_t fan_update_interval_ms = 1000; // Fan update frequency
    
    // Emergency shutdown parameters
    float emergency_temp_c = 60.0f;       // Emergency shutdown temperature
    float recovery_temp_c = 55.0f;        // Temperature to recover from emergency
    uint32_t emergency_delay_ms = 5000;   // Delay before emergency action
    
    // Sensor parameters
    uint32_t sensor_timeout_ms = 10000;   // Max time before sensor is invalid
    float temp_filter_alpha = 0.8f;      // Low-pass filter coefficient (0-1)
};

// Temperature control status for monitoring
struct TemperatureControlStatus {
    bool enabled = false;
    bool thermal_emergency = false;
    bool fan_enabled = false;
    float current_temp_c = 0.0f;
    float target_temp_c = 0.0f;
    float fan_pwm_percent = 0.0f;
    float fan_rpm = 0.0f;
    float pid_error = 0.0f;
    float pid_output = 0.0f;
    uint32_t emergency_start_ms = 0;
    uint32_t sensors_valid_count = 0;
    uint32_t sensors_total_count = 0;
};

// Main temperature control class
class TemperatureControl {
public:
    TemperatureControl();
    ~TemperatureControl() = default;
    
    // Configuration
    void set_config(const TemperatureControlConfig& config);
    const TemperatureControlConfig& get_config() const { return config_; }
    
    // Sensor management
    void add_temperature_sensor(const std::string& name);
    void update_temperature_sensor(const std::string& name, float temp_c, uint32_t timestamp_ms);
    std::vector<TemperatureSensor> get_sensors() const;
    
    // Fan control callbacks
    void set_fan_pwm_callback(std::function<void(float)> callback) { fan_pwm_callback_ = callback; }
    void set_fan_enable_callback(std::function<void(bool)> callback) { fan_enable_callback_ = callback; }
    void update_fan_rpm(float rpm) { status_.fan_rpm = rpm; }
    
    // Emergency shutdown callbacks
    void set_emergency_callback(std::function<void(bool)> callback) { emergency_callback_ = callback; }
    
    // Control loop
    void enable(bool enabled);
    void update(uint32_t current_time_ms);
    
    // Status monitoring
    const TemperatureControlStatus& get_status() const { return status_; }
    bool is_thermal_emergency() const { return status_.thermal_emergency; }
    
    // Diagnostics
    void reset_pid();
    std::string get_diagnostics() const;
    
    // Configuration serialization
    std::string export_config_json() const;
    bool import_config_json(const std::string& json);
    
    // Fan curve data for visualization
    struct FanCurvePoint {
        float temperature;
        float fan_pwm;
    };
    std::vector<FanCurvePoint> get_fan_curve() const;

private:
    TemperatureControlConfig config_;
    TemperatureControlStatus status_;
    PIDController pid_controller_;
    
    std::vector<TemperatureSensor> sensors_;
    
    uint32_t last_update_ms_;
    uint32_t last_fan_update_ms_;
    uint32_t emergency_triggered_ms_;
    
    float filtered_temperature_;
    bool emergency_cooldown_;
    
    // Callbacks
    std::function<void(float)> fan_pwm_callback_;    // Set fan PWM (0-100%)
    std::function<void(bool)> fan_enable_callback_;  // Enable/disable fan
    std::function<void(bool)> emergency_callback_;   // Emergency state changed
    
    // Internal methods
    float get_average_temperature(uint32_t current_time_ms);
    void update_emergency_state(uint32_t current_time_ms);
    void update_fan_control(uint32_t current_time_ms);
    void apply_temperature_filter(float new_temp);
};

} // namespace ledbrick