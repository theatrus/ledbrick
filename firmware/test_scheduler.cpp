#include "scheduler.h"
#include "test_framework.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>

void test_basic_functionality(TestRunner& runner) {
    runner.start_suite("Basic Functionality Tests");
    
    LEDScheduler scheduler(4);
    
    // Test initial state
    runner.assert_equals(4, static_cast<int>(scheduler.get_num_channels()), "Initial channel count");
    runner.assert_true(scheduler.is_schedule_empty(), "Initial schedule is empty");
    runner.assert_equals(0, static_cast<int>(scheduler.get_schedule_size()), "Initial schedule size");
    
    // Test adding a schedule point
    std::vector<float> pwm_values = {50.0f, 60.0f, 70.0f, 80.0f};
    std::vector<float> current_values = {1.0f, 1.2f, 1.4f, 1.6f};
    scheduler.set_schedule_point(720, pwm_values, current_values); // 12:00 PM
    
    runner.assert_false(scheduler.is_schedule_empty(), "Schedule not empty after adding point");
    runner.assert_equals(1, static_cast<int>(scheduler.get_schedule_size()), "Schedule size after adding point");
    
    // Test getting values at the exact time
    auto result = scheduler.get_values_at_time(720);
    runner.assert_true(result.valid, "Result valid at exact time");
    runner.assert_equals(50.0f, result.pwm_values[0], 0.01f, "PWM value at exact time");
    runner.assert_equals(1.6f, result.current_values[3], 0.01f, "Current value at exact time");
}

void test_interpolation(TestRunner& runner) {
    runner.start_suite("Interpolation Tests");
    
    LEDScheduler scheduler(2);
    
    // Add two schedule points
    scheduler.set_schedule_point(480, {20.0f, 30.0f}, {0.4f, 0.6f}); // 8:00 AM
    scheduler.set_schedule_point(1200, {80.0f, 90.0f}, {1.6f, 1.8f}); // 8:00 PM
    
    // Test interpolation at midpoint
    auto result = scheduler.get_values_at_time(840); // 2:00 PM (midpoint)
    runner.assert_true(result.valid, "Interpolation result valid");
    runner.assert_equals(50.0f, result.pwm_values[0], 1.0f, "PWM interpolated value");
    runner.assert_equals(60.0f, result.pwm_values[1], 1.0f, "PWM interpolated value");
    runner.assert_equals(1.0f, result.current_values[0], 0.1f, "Current interpolated value");
    runner.assert_equals(1.2f, result.current_values[1], 0.1f, "Current interpolated value");
}

void test_presets(TestRunner& runner) {
    runner.start_suite("Preset Tests");
    
    LEDScheduler scheduler(4);
    
    // Test sunrise/sunset preset
    scheduler.load_preset("sunrise_sunset");
    runner.assert_false(scheduler.is_schedule_empty(), "Sunrise/sunset preset loaded");
    runner.assert_true(scheduler.get_schedule_size() > 0, "Preset has schedule points");
    
    // Test full spectrum preset
    scheduler.load_preset("full_spectrum");
    runner.assert_false(scheduler.is_schedule_empty(), "Full spectrum preset loaded");
    
    // Test simple preset
    scheduler.load_preset("simple");
    runner.assert_false(scheduler.is_schedule_empty(), "Simple preset loaded");
    
    // Test custom preset
    scheduler.clear_schedule();
    scheduler.set_schedule_point(600, {25.0f, 35.0f, 45.0f, 55.0f}, {0.5f, 0.7f, 0.9f, 1.1f});
    scheduler.save_preset("custom_test");
    
    auto preset_names = scheduler.get_preset_names();
    bool found_custom = false;
    for (const auto& name : preset_names) {
        if (name == "custom_test") {
            found_custom = true;
            break;
        }
    }
    runner.assert_true(found_custom, "Custom preset saved and found");
}

void test_serialization(TestRunner& runner) {
    runner.start_suite("Serialization Tests");
    
    LEDScheduler scheduler1(3);
    scheduler1.set_schedule_point(360, {10.0f, 20.0f, 30.0f}, {0.2f, 0.4f, 0.6f}); // 6:00 AM
    scheduler1.set_schedule_point(1080, {40.0f, 50.0f, 60.0f}, {0.8f, 1.0f, 1.2f}); // 6:00 PM
    
    // Serialize
    auto serialized = scheduler1.serialize();
    runner.assert_equals(2, static_cast<int>(serialized.num_points), "Serialized point count");
    runner.assert_equals(3, static_cast<int>(serialized.num_channels), "Serialized channel count");
    runner.assert_true(serialized.data.size() > 0, "Serialized data not empty");
    
    // Deserialize to new scheduler
    LEDScheduler scheduler2(1); // Different initial channel count
    bool deserialized = scheduler2.deserialize(serialized);
    runner.assert_true(deserialized, "Deserialization successful");
    runner.assert_equals(3, static_cast<int>(scheduler2.get_num_channels()), "Deserialized channel count");
    runner.assert_equals(2, static_cast<int>(scheduler2.get_schedule_size()), "Deserialized schedule size");
    
    // Verify values
    auto result = scheduler2.get_values_at_time(360);
    runner.assert_true(result.valid, "Deserialized values valid");
    runner.assert_equals(10.0f, result.pwm_values[0], 0.01f, "Deserialized PWM value");
    runner.assert_equals(0.6f, result.current_values[2], 0.01f, "Deserialized current value");
}

void test_json_export(TestRunner& runner) {
    runner.start_suite("JSON Export Tests");
    
    LEDScheduler scheduler(2);
    scheduler.set_schedule_point(720, {75.0f, 85.0f}, {1.5f, 1.7f}); // 12:00 PM
    
    std::string json = scheduler.export_json();
    runner.assert_true(json.length() > 50, "JSON export not empty");
    runner.assert_true(json.find("\"num_channels\": 2") != std::string::npos, "JSON contains channel count");
    runner.assert_true(json.find("\"time_minutes\": 720") != std::string::npos, "JSON contains time");
    runner.assert_true(json.find("75.00") != std::string::npos, "JSON contains PWM value");
    runner.assert_true(json.find("1.70") != std::string::npos, "JSON contains current value");
    
    std::cout << "Sample JSON export:\n" << json.substr(0, 200) << "..." << std::endl;
}

void test_edge_cases(TestRunner& runner) {
    runner.start_suite("Edge Case Tests");
    
    LEDScheduler scheduler(2);
    
    // Test invalid time values
    auto result = scheduler.get_values_at_time(1440); // Invalid: >= 1440
    runner.assert_false(result.valid, "Invalid time rejected");
    
    result = scheduler.get_values_at_time(1500); // Invalid: > 1440
    runner.assert_false(result.valid, "Invalid time rejected");
    
    // Test empty schedule
    result = scheduler.get_values_at_time(720);
    runner.assert_false(result.valid, "Empty schedule returns invalid");
    
    // Test single point schedule
    scheduler.set_schedule_point(600, {50.0f, 60.0f}, {1.0f, 1.2f});
    result = scheduler.get_values_at_time(300); // Before point
    runner.assert_true(result.valid, "Single point interpolation valid");
    
    result = scheduler.get_values_at_time(900); // After point
    runner.assert_true(result.valid, "Single point interpolation valid");
    
    // Test boundary conditions
    result = scheduler.get_values_at_time(0); // Midnight
    runner.assert_true(result.valid, "Midnight interpolation valid");
    
    result = scheduler.get_values_at_time(1439); // 23:59
    runner.assert_true(result.valid, "End of day interpolation valid");
}

void test_channel_management(TestRunner& runner) {
    runner.start_suite("Channel Management Tests");
    
    LEDScheduler scheduler(2);
    scheduler.set_schedule_point(720, {50.0f, 60.0f}, {1.0f, 1.2f});
    
    // Test channel count change
    scheduler.set_num_channels(4);
    runner.assert_equals(4, static_cast<int>(scheduler.get_num_channels()), "Channel count updated");
    
    auto result = scheduler.get_values_at_time(720);
    runner.assert_true(result.valid, "Schedule valid after channel change");
    runner.assert_equals(4, static_cast<int>(result.pwm_values.size()), "PWM values resized");
    runner.assert_equals(4, static_cast<int>(result.current_values.size()), "Current values resized");
    
    // Original values should be preserved
    runner.assert_equals(50.0f, result.pwm_values[0], 0.01f, "Original PWM preserved");
    runner.assert_equals(60.0f, result.pwm_values[1], 0.01f, "Original PWM preserved");
    runner.assert_equals(0.0f, result.pwm_values[2], 0.01f, "New PWM defaulted to 0");
    runner.assert_equals(0.0f, result.pwm_values[3], 0.01f, "New PWM defaulted to 0");
}

void test_mutations(TestRunner& runner) {
    runner.start_suite("Schedule Mutation Tests");
    
    LEDScheduler scheduler(2);
    
    // Add multiple points
    scheduler.set_schedule_point(480, {20.0f, 30.0f}, {0.4f, 0.6f}); // 8:00 AM
    scheduler.set_schedule_point(720, {50.0f, 60.0f}, {1.0f, 1.2f}); // 12:00 PM
    scheduler.set_schedule_point(1200, {80.0f, 90.0f}, {1.6f, 1.8f}); // 8:00 PM
    
    runner.assert_equals(3, static_cast<int>(scheduler.get_schedule_size()), "Three points added");
    
    // Update existing point (same time)
    scheduler.set_schedule_point(720, {55.0f, 65.0f}, {1.1f, 1.3f}); // Updated 12:00 PM
    runner.assert_equals(3, static_cast<int>(scheduler.get_schedule_size()), "Size unchanged after update");
    
    auto result = scheduler.get_values_at_time(720);
    runner.assert_equals(55.0f, result.pwm_values[0], 0.01f, "Point updated correctly");
    
    // Remove point
    scheduler.remove_schedule_point(720);
    runner.assert_equals(2, static_cast<int>(scheduler.get_schedule_size()), "Point removed");
    
    // Clear schedule
    scheduler.clear_schedule();
    runner.assert_equals(0, static_cast<int>(scheduler.get_schedule_size()), "Schedule cleared");
    runner.assert_true(scheduler.is_schedule_empty(), "Schedule is empty after clear");
}

int main() {
    TestResults results;
    TestRunner runner;
    
    std::cout << "=== LEDBrick LED Scheduler Unit Tests ===" << std::endl;
    
    test_basic_functionality(runner);
    results.add_suite_results(runner);
    
    test_interpolation(runner);
    results.add_suite_results(runner);
    
    test_presets(runner);
    results.add_suite_results(runner);
    
    test_serialization(runner);
    results.add_suite_results(runner);
    
    test_json_export(runner);
    results.add_suite_results(runner);
    
    test_edge_cases(runner);
    results.add_suite_results(runner);
    
    test_channel_management(runner);
    results.add_suite_results(runner);
    
    test_mutations(runner);
    results.add_suite_results(runner);
    
    results.print_final_summary("LED Scheduler");
    
    return results.get_exit_code();
}