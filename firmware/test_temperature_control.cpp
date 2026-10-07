#include "components/ledbrick_scheduler/temperature_control.h"
#include "test_framework.h"
#include <cmath>
#include <vector>

using namespace ledbrick;

// Test helper to simulate the new architecture
class TemperatureControlTestHelper {
public:
    TemperatureControl controller;
    TemperatureHardwareManager hardware;
    
    TemperatureControlTestHelper() {
        // Don't enable by default - let tests control this
    }
    
    void update(uint32_t current_time_ms) {
        auto command = controller.compute_control_command(current_time_ms);
        hardware.apply_command(command, current_time_ms);
        controller.update_hardware_state(hardware.get_hardware_state());
    }
    
    const TemperatureControlStatus& get_status() {
        return controller.get_status();
    }
};

void test_sensor_management(TestRunner& runner) {
    runner.start_suite("Sensor Management Tests");
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    
    // Add sensors
    helper.controller.add_temperature_sensor("sensor1");
    helper.controller.add_temperature_sensor("sensor2");
    helper.controller.add_temperature_sensor("sensor3");
    
    // Verify sensor count through status
    helper.update(1000);
    auto status = helper.get_status();
    runner.assert_equals(3, static_cast<int>(status.sensors_total_count), "Total sensor count");
    
    // All sensors invalid initially
    runner.assert_equals(0, static_cast<int>(status.sensors_valid_count), "Valid sensor count (no data)");
}

void test_sensor_validity(TestRunner& runner) {
    runner.start_suite("Sensor Validity Tests");
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    TemperatureControlConfig config;
    config.sensor_timeout_ms = 5000; // 5 second timeout
    helper.controller.set_config(config);
    
    helper.controller.add_temperature_sensor("sensor1");
    helper.controller.add_temperature_sensor("sensor2");
    
    // Update sensors
    helper.controller.update_temperature_sensor("sensor1", 25.0f, 1000);
    helper.controller.update_temperature_sensor("sensor2", 26.0f, 1000);
    
    helper.update(1500); // 500ms later
    auto status = helper.get_status();
    runner.assert_equals(2, static_cast<int>(status.sensors_valid_count), "Both sensors valid");
    
    // Fast forward past timeout for sensor2
    helper.update(7000); // 6 seconds later
    status = helper.get_status();
    runner.assert_equals(0, static_cast<int>(status.sensors_valid_count), "Both sensors timed out");
    
    // Update one sensor
    helper.controller.update_temperature_sensor("sensor1", 25.5f, 7000);
    helper.update(7100);
    status = helper.get_status();
    runner.assert_equals(1, static_cast<int>(status.sensors_valid_count), "One sensor valid");
}

void test_temperature_averaging(TestRunner& runner) {
    runner.start_suite("Temperature Averaging Tests");
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    
    // Disable filtering for accurate averaging tests
    TemperatureControlConfig config;
    config.temp_filter_alpha = 1.0f; // No filtering
    helper.controller.set_config(config);
    
    helper.controller.add_temperature_sensor("sensor1");
    helper.controller.add_temperature_sensor("sensor2");
    helper.controller.add_temperature_sensor("sensor3");
    
    // Update with different temperatures
    helper.controller.update_temperature_sensor("sensor1", 25.0f, 1000);
    helper.controller.update_temperature_sensor("sensor2", 27.0f, 1000);
    helper.controller.update_temperature_sensor("sensor3", 26.0f, 1000);
    
    helper.update(1100);
    auto status = helper.get_status();
    
    // Average should be (25 + 27 + 26) / 3 = 26
    runner.assert_equals(26.0f, status.current_temp_c, 0.001f, "Average temperature");
    
    // Test with one invalid sensor
    // Wait for sensor3 to timeout (it was last updated at time 1000)
    helper.update(12000); // More than 10s after sensor3's last update
    
    // Update only sensor1 and sensor2
    helper.controller.update_temperature_sensor("sensor1", 30.0f, 12000);
    helper.controller.update_temperature_sensor("sensor2", 32.0f, 12000);
    // sensor3 remains timed out
    
    helper.update(12100);
    status = helper.get_status();
    
    // Should average only the two valid sensors: (30 + 32) / 2 = 31
    runner.assert_equals(31.0f, status.current_temp_c, 0.001f, "Average with invalid sensor");
}

void test_temperature_filtering(TestRunner& runner) {
    runner.start_suite("Temperature Filtering Tests");
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    TemperatureControlConfig config;
    config.temp_filter_alpha = 0.5f; // 50% new value, 50% old value
    helper.controller.set_config(config);
    
    helper.controller.add_temperature_sensor("sensor1");
    
    // First reading - no filtering
    helper.controller.update_temperature_sensor("sensor1", 25.0f, 1000);
    helper.update(1100);
    auto status = helper.get_status();
    runner.assert_equals(25.0f, status.current_temp_c, 0.001f, "First reading unfiltered");
    
    // Second reading - should be filtered
    helper.controller.update_temperature_sensor("sensor1", 30.0f, 2000);
    helper.update(2100);
    status = helper.get_status();
    // Filtered = 0.5 * 30 + 0.5 * 25 = 27.5
    runner.assert_equals(27.5f, status.current_temp_c, 0.001f, "Filtered temperature");
    
    // Third reading
    helper.controller.update_temperature_sensor("sensor1", 20.0f, 3000);
    helper.update(3100);
    status = helper.get_status();
    // Filtered = 0.5 * 20 + 0.5 * 27.5 = 23.75
    runner.assert_equals(23.75f, status.current_temp_c, 0.001f, "Further filtered");
}

void test_emergency_state_machine(TestRunner& runner) {
    runner.start_suite("Emergency State Machine Tests");
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    TemperatureControlConfig config;
    config.emergency_temp_c = 70.0f;
    config.recovery_temp_c = 65.0f;
    config.emergency_delay_ms = 0; // No delay for testing
    config.temp_filter_alpha = 1.0f; // Disable filtering for immediate response
    helper.controller.set_config(config);
    helper.controller.enable(true);
    
    helper.controller.add_temperature_sensor("sensor1");
    
    // Normal temperature
    helper.controller.update_temperature_sensor("sensor1", 50.0f, 1000);
    helper.update(1100);
    auto status = helper.get_status();
    runner.assert_false(status.hardware.thermal_emergency, "No emergency at normal temp");
    
    // Temperature exceeds emergency threshold
    helper.controller.update_temperature_sensor("sensor1", 71.0f, 2000);
    helper.update(2100); // First update starts countdown
    helper.update(2101); // Second update triggers emergency (delay is 0)
    status = helper.get_status();
    runner.assert_true(status.hardware.thermal_emergency, "Emergency triggered");
    
    // Temperature drops but not below recovery
    helper.controller.update_temperature_sensor("sensor1", 68.0f, 3000);
    helper.update(3100);
    status = helper.get_status();
    runner.assert_true(status.hardware.thermal_emergency, "Emergency maintained above recovery");
    
    // Temperature drops below recovery
    helper.controller.update_temperature_sensor("sensor1", 64.0f, 4000);
    helper.update(4100);
    status = helper.get_status();
    // Emergency should be cleared when temperature drops below recovery threshold
    runner.assert_false(status.hardware.thermal_emergency, "Emergency cleared");
}

void test_fan_curve_generation(TestRunner& runner) {
    runner.start_suite("Fan Curve Generation Tests");
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    TemperatureControlConfig config;
    config.target_temp_c = 45.0f;
    config.min_fan_pwm = 10.0f;
    config.max_fan_pwm = 100.0f;
    helper.controller.set_config(config);
    
    auto curve = helper.controller.get_fan_curve();
    
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
    
    TemperatureControlTestHelper helper;
    helper.controller.enable(true); // Enable to allow updates
    
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
    
    helper.controller.set_config(config);
    
    // Export to JSON
    std::string json = helper.controller.export_config_json();
    
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
    
    TemperatureControlTestHelper helper;
    
    // Track callbacks
    bool fan_enabled = false;
    float fan_pwm = -1.0f;
    bool emergency_state = false;
    
    helper.hardware.set_fan_enable_callback([&](bool state) {
        fan_enabled = state;
    });
    
    helper.hardware.set_fan_pwm_callback([&](float pwm) {
        fan_pwm = pwm;
    });
    
    helper.hardware.set_emergency_callback([&](bool state) {
        emergency_state = state;
    });
    
    // Initially disabled
    auto status = helper.get_status();
    runner.assert_false(status.enabled, "Initially disabled");
    
    // Enable
    helper.controller.enable(true);
    status = helper.get_status();
    runner.assert_true(status.enabled, "Enabled after call");
    
    // Add temperature sensor and trigger fan to turn on first  
    helper.controller.add_temperature_sensor("sensor1");
    helper.controller.update_temperature_sensor("sensor1", 50.0f, 1000); // High temp to trigger fan
    helper.update(1100); // This should trigger fan activity via safety mode
    
    // Disable - should turn off fan
    helper.controller.enable(false);
    helper.update(2100); // Trigger an update to apply the disable command
    runner.assert_false(fan_enabled, "Fan disabled");
    runner.assert_equals(0.0f, fan_pwm, 0.001f, "Fan PWM zero");
}

// Configuration used by the emergency regression tests
static TemperatureControlConfig emergency_test_config() {
    TemperatureControlConfig config;
    config.emergency_temp_c = 70.0f;
    config.recovery_temp_c = 65.0f;
    config.emergency_delay_ms = 0;
    config.temp_filter_alpha = 1.0f;
    return config;
}

static void trigger_emergency(TemperatureControlTestHelper& helper) {
    helper.controller.set_config(emergency_test_config());
    helper.controller.enable(true);
    helper.controller.add_temperature_sensor("sensor1");
    helper.controller.update_temperature_sensor("sensor1", 71.0f, 2000);
    helper.update(2100);
    helper.update(2101);
}

void test_emergency_latched_through_sensor_faults(TestRunner& runner) {
    runner.start_suite("Emergency Latch Tests");

    TemperatureControlTestHelper helper;
    int emergency_clears = 0;
    helper.hardware.set_emergency_callback([&](bool state) {
        if (!state) emergency_clears++;
    });

    trigger_emergency(helper);
    runner.assert_true(helper.get_status().hardware.thermal_emergency, "Emergency triggered");

    // Reading stamped 1 ms after the control tick (the integration stamps sensors
    // with a later millis() call) must not look stale
    helper.controller.update_temperature_sensor("sensor1", 68.0f, 3001);
    helper.update(3000);
    runner.assert_equals(1, static_cast<int>(helper.get_status().sensors_valid_count),
                         "Reading stamped after tick is valid");
    runner.assert_true(helper.get_status().hardware.thermal_emergency, "Emergency kept after timestamp skew");

    // All sensors time out: fan goes to full speed, emergency stays
    helper.update(30000);
    runner.assert_equals(0, static_cast<int>(helper.get_status().sensors_valid_count), "Sensor timed out");
    runner.assert_true(helper.get_status().hardware.thermal_emergency, "Emergency kept after sensor loss");
    runner.assert_equals(100.0f, helper.get_status().hardware.fan_pwm_percent, 0.001f, "Fan at full speed");

    // Sensor returns between recovery and trigger: still in emergency
    helper.controller.update_temperature_sensor("sensor1", 67.0f, 31000);
    helper.update(31100);
    runner.assert_true(helper.get_status().hardware.thermal_emergency, "Emergency kept above recovery");
    runner.assert_equals(0, emergency_clears, "No early clear");

    // Confirmed recovery clears it
    helper.controller.update_temperature_sensor("sensor1", 64.0f, 32000);
    helper.update(32100);
    runner.assert_false(helper.get_status().hardware.thermal_emergency, "Emergency cleared at recovery");
}

void test_disable_keeps_emergency(TestRunner& runner) {
    runner.start_suite("Disable During Emergency Tests");

    TemperatureControlTestHelper helper;
    trigger_emergency(helper);
    runner.assert_true(helper.get_status().hardware.thermal_emergency, "Emergency triggered");

    helper.controller.enable(false);
    helper.update(3000);
    runner.assert_true(helper.get_status().hardware.thermal_emergency, "Emergency kept while disabled");
    runner.assert_equals(100.0f, helper.get_status().hardware.fan_pwm_percent, 0.001f, "Fan at full speed while disabled");

    helper.controller.enable(true);
    helper.controller.update_temperature_sensor("sensor1", 60.0f, 4000);
    helper.update(4100);
    runner.assert_false(helper.get_status().hardware.thermal_emergency, "Emergency cleared after re-enable");
}

void test_no_windup_after_cool_period(TestRunner& runner) {
    runner.start_suite("PID Windup After Cool Period Tests");

    TemperatureControlConfig config;
    config.temp_filter_alpha = 1.0f;

    TemperatureControlTestHelper cooled;
    cooled.controller.set_config(config);
    cooled.controller.enable(true);
    cooled.controller.add_temperature_sensor("sensor1");

    // Eight hours at 25°C (target 45°C), one reading every 5 s
    uint32_t t = 1000;
    for (int i = 0; i < 8 * 3600 / 5; i++, t += 5000) {
        cooled.controller.update_temperature_sensor("sensor1", 25.0f, t);
        cooled.update(t);
    }

    TemperatureControlTestHelper fresh;
    fresh.controller.set_config(config);
    fresh.controller.enable(true);
    fresh.controller.add_temperature_sensor("sensor1");

    // One minute at 52°C for both controllers
    uint32_t t_fresh = 1000;
    for (int i = 0; i < 12; i++, t += 5000, t_fresh += 5000) {
        cooled.controller.update_temperature_sensor("sensor1", 52.0f, t);
        cooled.update(t);
        fresh.controller.update_temperature_sensor("sensor1", 52.0f, t_fresh);
        fresh.update(t_fresh);
    }

    float cooled_pwm = cooled.get_status().hardware.fan_pwm_percent;
    float fresh_pwm = fresh.get_status().hardware.fan_pwm_percent;
    runner.assert_true(cooled_pwm > 0.0f, "Fan runs after a cool night");
    // The fresh controller's first step uses the default 1 s interval rather than 5 s,
    // so the integrals differ slightly; before the windup fix the cooled fan stayed at 0%
    runner.assert_equals(fresh_pwm, cooled_pwm, 5.0f, "Fan output close to a fresh controller");
}

void test_config_import_validation(TestRunner& runner) {
    runner.start_suite("Config Import Validation Tests");

    TemperatureControl controller;
    TemperatureControlConfig defaults = controller.get_config();

    runner.assert_true(controller.import_config_json("{\"target_temp_c\":40}"), "Partial config accepted");
    auto config = controller.get_config();
    runner.assert_equals(40.0f, config.target_temp_c, 0.001f, "Target updated");
    runner.assert_equals(static_cast<int>(defaults.emergency_delay_ms), static_cast<int>(config.emergency_delay_ms),
                         "Missing emergency delay unchanged");
    runner.assert_equals(static_cast<int>(defaults.sensor_timeout_ms), static_cast<int>(config.sensor_timeout_ms),
                         "Missing sensor timeout unchanged");
    runner.assert_equals(static_cast<int>(defaults.fan_update_interval_ms), static_cast<int>(config.fan_update_interval_ms),
                         "Missing fan interval unchanged");

    std::string error;
    runner.assert_false(controller.import_config_json("{\"kp\":null}", &error), "Null value rejected");
    runner.assert_false(controller.import_config_json("{\"kp\":\"x\"}"), "String value rejected");
    runner.assert_false(controller.import_config_json("not json"), "Malformed JSON rejected");
    runner.assert_false(controller.import_config_json("{\"emergency_temp_c\":500}"), "Emergency temp above ceiling rejected");
    runner.assert_false(controller.import_config_json("{\"emergency_delay_ms\":-1}"), "Negative delay rejected");
    runner.assert_false(controller.import_config_json("{\"recovery_temp_c\":65,\"emergency_temp_c\":60}"),
                        "Recovery above emergency rejected");
    runner.assert_false(controller.import_config_json("{\"min_fan_pwm\":80,\"max_fan_pwm\":50}"),
                        "Min fan above max rejected");
    runner.assert_false(controller.import_config_json("{\"sensor_timeout_ms\":3000}"),
                        "Sensor timeout shorter than the sensor interval rejected");
    runner.assert_equals(40.0f, controller.get_config().target_temp_c, 0.001f, "Rejected imports change nothing");

    // Settings saved by older firmware are repaired on the safe side instead of dropped
    TemperatureControl loaded;
    std::string saved = "{\"emergency_temp_c\":45,\"recovery_temp_c\":50,\"target_temp_c\":40,"
                        "\"sensor_timeout_ms\":1000,\"kp\":\"x\",\"max_fan_pwm\":150}";
    runner.assert_true(loaded.import_config_json(saved, nullptr, true), "Repair import accepted");
    auto repaired = loaded.get_config();
    runner.assert_equals(45.0f, repaired.emergency_temp_c, 0.001f, "Lower emergency temp kept");
    runner.assert_equals(40.0f, repaired.recovery_temp_c, 0.001f, "Recovery moved below emergency");
    runner.assert_equals(static_cast<int>(10000), static_cast<int>(repaired.sensor_timeout_ms), "Sensor timeout raised to floor");
    runner.assert_equals(defaults.kp, repaired.kp, 0.001f, "Unparseable kp keeps current value");
    runner.assert_equals(100.0f, repaired.max_fan_pwm, 0.001f, "Max fan clamped");
    runner.assert_false(loaded.import_config_json("not json", nullptr, true), "Repair still rejects malformed JSON");
    runner.assert_equals(defaults.emergency_temp_c, controller.get_config().emergency_temp_c, 0.001f,
                         "Emergency temp unchanged after rejection");
}

void test_fan_control_across_millis_wrap(TestRunner& runner) {
    runner.start_suite("Millis Wrap Tests");

    TemperatureControlConfig config;
    config.temp_filter_alpha = 1.0f;

    TemperatureControlTestHelper helper;
    helper.controller.set_config(config);
    helper.controller.enable(true);
    helper.controller.add_temperature_sensor("sensor1");

    uint32_t t = 0xFFFFFFFFu - 10000u;
    for (int i = 0; i < 10; i++, t += 1000) {
        helper.controller.update_temperature_sensor("sensor1", 50.0f, t);
        helper.update(t);
    }
    float pwm_before_wrap = helper.get_status().hardware.fan_pwm_percent;

    // t has wrapped past zero; a hotter reading must still move the fan
    for (int i = 0; i < 10; i++, t += 1000) {
        helper.controller.update_temperature_sensor("sensor1", 54.0f, t);
        helper.update(t);
    }
    runner.assert_true(t < 20000u, "Clock wrapped");
    runner.assert_true(helper.get_status().hardware.fan_pwm_percent > pwm_before_wrap + 1.0f,
                       "Fan output follows temperature after wrap");
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

    test_emergency_latched_through_sensor_faults(runner);
    results.add_suite_results(runner);

    test_disable_keeps_emergency(runner);
    results.add_suite_results(runner);

    test_no_windup_after_cool_period(runner);
    results.add_suite_results(runner);

    test_config_import_validation(runner);
    results.add_suite_results(runner);

    test_fan_control_across_millis_wrap(runner);
    results.add_suite_results(runner);
    
    results.print_final_summary("Temperature Control");
    
    return results.get_exit_code();
}