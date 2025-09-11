#include "components/ledbrick_scheduler/temperature_control.h"
#include "test_framework.h"
#include <cmath>
#include <vector>

using namespace ledbrick;

void test_sensor_management(TestRunner& runner) {
    runner.start_suite("Sensor Management Tests");
    
    TemperatureControl temp_control;
    temp_control.enable(true); // Enable to allow updates
    
    // Add sensors
    temp_control.add_temperature_sensor("sensor1");
    temp_control.add_temperature_sensor("sensor2");
    temp_control.add_temperature_sensor("sensor3");
    
    // Verify sensor count through status
    temp_control.update(1000);
    auto status = temp_control.get_status();
    runner.assert_equals(3, static_cast<int>(status.sensors_total_count), "Total sensor count");
    
    // All sensors invalid initially
    runner.assert_equals(0, static_cast<int>(status.sensors_valid_count), "Valid sensor count (no data)");
}

void test_sensor_validity(TestRunner& runner) {
    runner.start_suite("Sensor Validity Tests");
    
    TemperatureControl temp_control;
    temp_control.enable(true); // Enable to allow updates
    TemperatureControlConfig config;
    config.sensor_timeout_ms = 5000; // 5 second timeout
    temp_control.set_config(config);
    
    temp_control.add_temperature_sensor("sensor1");
    temp_control.add_temperature_sensor("sensor2");
    
    // Update sensors
    temp_control.update_temperature_sensor("sensor1", 25.0f, 1000);
    temp_control.update_temperature_sensor("sensor2", 26.0f, 1000);
    
    temp_control.update(1500); // 500ms later
    auto status = temp_control.get_status();
    runner.assert_equals(2, static_cast<int>(status.sensors_valid_count), "Both sensors valid");
    
    // Fast forward past timeout for sensor2
    temp_control.update(7000); // 6 seconds later
    status = temp_control.get_status();
    runner.assert_equals(0, static_cast<int>(status.sensors_valid_count), "Both sensors timed out");
    
    // Update one sensor
    temp_control.update_temperature_sensor("sensor1", 25.5f, 7000);
    temp_control.update(7100);
    status = temp_control.get_status();
    runner.assert_equals(1, static_cast<int>(status.sensors_valid_count), "One sensor valid");
}

void test_temperature_averaging(TestRunner& runner) {
    runner.start_suite("Temperature Averaging Tests");
    
    TemperatureControl temp_control;
    temp_control.enable(true); // Enable to allow updates
    
    // Disable filtering for accurate averaging tests
    TemperatureControlConfig config;
    config.temp_filter_alpha = 1.0f; // No filtering
    temp_control.set_config(config);
    
    temp_control.add_temperature_sensor("sensor1");
    temp_control.add_temperature_sensor("sensor2");
    temp_control.add_temperature_sensor("sensor3");
    
    // Update with different temperatures
    temp_control.update_temperature_sensor("sensor1", 25.0f, 1000);
    temp_control.update_temperature_sensor("sensor2", 27.0f, 1000);
    temp_control.update_temperature_sensor("sensor3", 26.0f, 1000);
    
    temp_control.update(1100);
    auto status = temp_control.get_status();
    
    // Average should be (25 + 27 + 26) / 3 = 26
    runner.assert_equals(26.0f, status.current_temp_c, 0.001f, "Average temperature");
    
    // Test with one invalid sensor
    // Wait for sensor3 to timeout (it was last updated at time 1000)
    temp_control.update(12000); // More than 10s after sensor3's last update
    
    // Update only sensor1 and sensor2
    temp_control.update_temperature_sensor("sensor1", 30.0f, 12000);
    temp_control.update_temperature_sensor("sensor2", 32.0f, 12000);
    // sensor3 remains timed out
    
    temp_control.update(12100);
    status = temp_control.get_status();
    
    // Should average only the two valid sensors: (30 + 32) / 2 = 31
    runner.assert_equals(31.0f, status.current_temp_c, 0.001f, "Average with invalid sensor");
}

void test_temperature_filtering(TestRunner& runner) {
    runner.start_suite("Temperature Filtering Tests");
    
    TemperatureControl temp_control;
    temp_control.enable(true); // Enable to allow updates
    TemperatureControlConfig config;
    config.temp_filter_alpha = 0.5f; // 50% new value, 50% old value
    temp_control.set_config(config);
    
    temp_control.add_temperature_sensor("sensor1");
    
    // First reading - no filtering
    temp_control.update_temperature_sensor("sensor1", 25.0f, 1000);
    temp_control.update(1100);
    auto status = temp_control.get_status();
    runner.assert_equals(25.0f, status.current_temp_c, 0.001f, "First reading unfiltered");
    
    // Second reading - should be filtered
    temp_control.update_temperature_sensor("sensor1", 30.0f, 2000);
    temp_control.update(2100);
    status = temp_control.get_status();
    // Filtered = 0.5 * 30 + 0.5 * 25 = 27.5
    runner.assert_equals(27.5f, status.current_temp_c, 0.001f, "Filtered temperature");
    
    // Third reading
    temp_control.update_temperature_sensor("sensor1", 20.0f, 3000);
    temp_control.update(3100);
    status = temp_control.get_status();
    // Filtered = 0.5 * 20 + 0.5 * 27.5 = 23.75
    runner.assert_equals(23.75f, status.current_temp_c, 0.001f, "Further filtered");
}

void test_emergency_state_machine(TestRunner& runner) {
    runner.start_suite("Emergency State Machine Tests");
    
    TemperatureControl temp_control;
    TemperatureControlConfig config;
    config.emergency_temp_c = 70.0f;
    config.recovery_temp_c = 65.0f;
    config.emergency_delay_ms = 0; // No delay for testing
    config.temp_filter_alpha = 1.0f; // Disable filtering for immediate response
    temp_control.set_config(config);
    temp_control.enable(true);
    
    temp_control.add_temperature_sensor("sensor1");
    
    // Normal temperature
    temp_control.update_temperature_sensor("sensor1", 50.0f, 1000);
    temp_control.update(1100);
    auto status = temp_control.get_status();
    runner.assert_false(status.thermal_emergency, "No emergency at normal temp");
    
    // Temperature exceeds emergency threshold
    temp_control.update_temperature_sensor("sensor1", 71.0f, 2000);
    temp_control.update(2100); // First update starts countdown
    temp_control.update(2101); // Second update triggers emergency (delay is 0)
    status = temp_control.get_status();
    runner.assert_true(status.thermal_emergency, "Emergency triggered");
    
    // Temperature drops but not below recovery
    temp_control.update_temperature_sensor("sensor1", 68.0f, 3000);
    temp_control.update(3100);
    status = temp_control.get_status();
    runner.assert_true(status.thermal_emergency, "Emergency maintained above recovery");
    
    // Temperature drops below recovery
    temp_control.update_temperature_sensor("sensor1", 64.0f, 4000);
    temp_control.update(4100);
    status = temp_control.get_status();
    // Emergency should be cleared when temperature drops below recovery threshold
    runner.assert_false(status.thermal_emergency, "Emergency cleared");
}

void test_fan_curve_generation(TestRunner& runner) {
    runner.start_suite("Fan Curve Generation Tests");
    
    TemperatureControl temp_control;
    TemperatureControlConfig config;
    config.target_temp_c = 45.0f;
    config.min_fan_pwm = 10.0f;
    config.max_fan_pwm = 100.0f;
    temp_control.set_config(config);
    
    auto curve = temp_control.get_fan_curve();
    
    // Should have 7 points
    runner.assert_equals(7, static_cast<int>(curve.size()), "Fan curve point count");
    
    // Check key points - curve is based on target_temp - 10
    runner.assert_equals(35.0f, curve[0].temperature, 0.001f, "First temp point (target-10)");
    runner.assert_equals(10.0f, curve[0].fan_pwm, 0.001f, "Min fan at low temp");
    
    runner.assert_equals(45.0f, curve[2].temperature, 0.001f, "Target temp point");
    runner.assert_equals(30.0f, curve[2].fan_pwm, 0.001f, "Fan PWM at target");
    
    // Last point should be emergency + 5
    runner.assert_equals(65.0f, curve[6].temperature, 0.001f, "Last temp point");
    runner.assert_equals(100.0f, curve[6].fan_pwm, 0.001f, "Max fan at high temp");
    
    // Check monotonic increase
    for (size_t i = 1; i < curve.size(); i++) {
        runner.assert_true(curve[i].temperature > curve[i-1].temperature, 
                          "Temperature increases");
        runner.assert_true(curve[i].fan_pwm >= curve[i-1].fan_pwm, 
                          "Fan PWM increases");
    }
}

void test_configuration_persistence(TestRunner& runner) {
    runner.start_suite("Configuration Persistence Tests");
    
    TemperatureControl temp_control;
    
    // Set configuration
    TemperatureControlConfig config;
    config.target_temp_c = 47.5f;
    config.kp = 3.0f;
    config.ki = 0.2f;
    config.kd = 0.5f;
    config.min_fan_pwm = 15.0f;
    config.max_fan_pwm = 95.0f;
    config.emergency_temp_c = 75.0f;
    config.recovery_temp_c = 70.0f;
    config.emergency_delay_ms = 3000;
    config.sensor_timeout_ms = 10000;
    config.temp_filter_alpha = 0.3f;
    
    temp_control.set_config(config);
    
    // Export to JSON
    std::string json = temp_control.export_config_json();
    
    // Create new instance and import
    TemperatureControl temp_control2;
    bool success = temp_control2.import_config_json(json);
    runner.assert_true(success, "Config import successful");
    
    // Verify all values
    auto config2 = temp_control2.get_config();
    runner.assert_equals(config.target_temp_c, config2.target_temp_c, 0.001f, "Target temp");
    runner.assert_equals(config.kp, config2.kp, 0.001f, "Kp");
    runner.assert_equals(config.ki, config2.ki, 0.001f, "Ki");
    runner.assert_equals(config.kd, config2.kd, 0.001f, "Kd");
    runner.assert_equals(config.min_fan_pwm, config2.min_fan_pwm, 0.001f, "Min fan");
    runner.assert_equals(config.max_fan_pwm, config2.max_fan_pwm, 0.001f, "Max fan");
    runner.assert_equals(config.emergency_temp_c, config2.emergency_temp_c, 0.001f, "Emergency temp");
    runner.assert_equals(config.recovery_temp_c, config2.recovery_temp_c, 0.001f, "Recovery temp");
    runner.assert_equals(static_cast<int>(config.emergency_delay_ms), static_cast<int>(config2.emergency_delay_ms), "Emergency delay");
    runner.assert_equals(static_cast<int>(config.sensor_timeout_ms), static_cast<int>(config2.sensor_timeout_ms), "Sensor timeout");
    runner.assert_equals(config.temp_filter_alpha, config2.temp_filter_alpha, 0.001f, "Filter alpha");
}

void test_enable_disable(TestRunner& runner) {
    runner.start_suite("Enable Disable Tests");
    
    TemperatureControl temp_control;
    
    // Track callbacks
    bool fan_enabled = false;
    float fan_pwm = -1.0f;
    bool emergency_state = false;
    
    temp_control.set_fan_enable_callback([&](bool state) {
        fan_enabled = state;
    });
    
    temp_control.set_fan_pwm_callback([&](float pwm) {
        fan_pwm = pwm;
    });
    
    temp_control.set_emergency_callback([&](bool state) {
        emergency_state = state;
    });
    
    // Initially disabled
    auto status = temp_control.get_status();
    runner.assert_false(status.enabled, "Initially disabled");
    
    // Enable
    temp_control.enable(true);
    status = temp_control.get_status();
    runner.assert_true(status.enabled, "Enabled after call");
    
    // Disable - should turn off fan
    temp_control.enable(false);
    runner.assert_false(fan_enabled, "Fan disabled");
    runner.assert_equals(0.0f, fan_pwm, 0.001f, "Fan PWM zero");
}

// Main test runner
int main() {
    TestResults results;
    TestRunner runner;
    
    std::cout << "=== TEMPERATURE CONTROL UNIT TESTS ===" << std::endl;
    
    test_sensor_management(runner);
    results.add_suite_results(runner);
    
    test_sensor_validity(runner);
    results.add_suite_results(runner);
    
    test_temperature_averaging(runner);
    results.add_suite_results(runner);
    
    test_temperature_filtering(runner);
    results.add_suite_results(runner);
    
    test_emergency_state_machine(runner);
    results.add_suite_results(runner);
    
    test_fan_curve_generation(runner);
    results.add_suite_results(runner);
    
    test_configuration_persistence(runner);
    results.add_suite_results(runner);
    
    test_enable_disable(runner);
    results.add_suite_results(runner);
    
    results.print_final_summary("Temperature Control");
    
    return results.get_exit_code();
}