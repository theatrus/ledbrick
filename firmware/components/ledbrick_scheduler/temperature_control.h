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

// Controller decision output - what the hardware should be set to
struct TemperatureControlCommand {
    bool fan_enabled = false;
    float fan_pwm_percent = 0.0f;
    bool emergency_state = false;
    bool override_normal_control = false;  // true for safety modes, emergency, etc.
    std::string reason;  // reason for override (for logging)
};

// Current hardware state (read-only status)
struct TemperatureHardwareState {
    bool fan_enabled = false;
    float fan_pwm_percent = 0.0f;
    float fan_rpm = 0.0f;
    bool thermal_emergency = false;
    uint32_t emergency_start_ms = 0;
};

// Temperature control status for monitoring (combines controller state + hardware state)
struct TemperatureControlStatus {
    bool enabled = false;
    float current_temp_c = 0.0f;
    float target_temp_c = 0.0f;
    float pid_error = 0.0f;
    float pid_output = 0.0f;
    uint32_t sensors_valid_count = 0;
    uint32_t sensors_total_count = 0;
    
    // Hardware state (actual current state)
    TemperatureHardwareState hardware;
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
    void update_fan_rpm(float rpm) { status_.hardware.fan_rpm = rpm; }
    
    // Emergency shutdown callbacks
    void set_emergency_callback(std::function<void(bool)> callback) { emergency_callback_ = callback; }
    
    // Control loop - returns command for hardware
    void enable(bool enabled);
    TemperatureControlCommand compute_control_command(uint32_t current_time_ms);
    
    // Hardware state management (called by outer layer)
    void update_hardware_state(const TemperatureHardwareState& hardware_state);
    
    // Safety evaluation (testable, independent function)
    static TemperatureControlCommand evaluate_safety_conditions(
        const TemperatureControlConfig& config,
        float current_temp_c,
        bool ever_had_valid_temp,
        uint32_t last_valid_temp_ms,
        uint32_t sensors_valid_count,
        uint32_t current_time_ms
    );
    
    // Status monitoring
    const TemperatureControlStatus& get_status() const { return status_; }
    bool is_thermal_emergency() const { return status_.hardware.thermal_emergency; }
    
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
    
    // Internal controller state (not hardware state)
    bool emergency_cooldown_;
    uint32_t emergency_triggered_ms_;
    
    std::vector<TemperatureSensor> sensors_;
    
    uint32_t last_update_ms_;
    uint32_t last_fan_update_ms_;
    uint32_t last_valid_temp_ms_;
    uint32_t last_pid_compute_temp_ms_;  // Timestamp of temp data used for last PID compute

    float filtered_temperature_;
    bool ever_had_valid_temp_;
    
    // Callbacks (deprecated - will be removed)
    std::function<void(float)> fan_pwm_callback_;    // Set fan PWM (0-100%)
    std::function<void(bool)> fan_enable_callback_;  // Enable/disable fan
    std::function<void(bool)> emergency_callback_;   // Emergency state changed
    
    // Internal methods
    float get_average_temperature(uint32_t current_time_ms);
    TemperatureControlCommand evaluate_emergency_state(uint32_t current_time_ms);
    TemperatureControlCommand compute_fan_control(uint32_t current_time_ms);
    void apply_temperature_filter(float new_temp);
};

// Hardware management interface - handles actual hardware state updates
class TemperatureHardwareManager {
public:
    TemperatureHardwareManager();
    ~TemperatureHardwareManager() = default;
    
    // Hardware control callbacks
    void set_fan_pwm_callback(std::function<void(float)> callback) { fan_pwm_callback_ = callback; }
    void set_fan_enable_callback(std::function<void(bool)> callback) { fan_enable_callback_ = callback; }
    void set_emergency_callback(std::function<void(bool)> callback) { emergency_callback_ = callback; }
    void update_fan_rpm(float rpm) { hardware_state_.fan_rpm = rpm; }
    
    // Apply controller command to hardware
    void apply_command(const TemperatureControlCommand& command);
    
    // Get current hardware state
    const TemperatureHardwareState& get_hardware_state() const { return hardware_state_; }

private:
    TemperatureHardwareState hardware_state_;
    
    std::function<void(float)> fan_pwm_callback_;    // Set fan PWM (0-100%)
    std::function<void(bool)> fan_enable_callback_;  // Enable/disable fan
    std::function<void(bool)> emergency_callback_;   // Emergency state changed
};

} // namespace ledbrick