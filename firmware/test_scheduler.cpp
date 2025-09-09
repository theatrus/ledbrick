#include "components/ledbrick_scheduler/scheduler.h"
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
    runner.assert_equals(50.0f, result.pwm_values[0], 0.01f, "PWM value at exact time (expected: 50.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(1.6f, result.current_values[3], 0.01f, "Current value at exact time (expected: 1.6, actual: " + std::to_string(result.current_values[3]) + ")");
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
    runner.assert_equals(50.0f, result.pwm_values[0], 1.0f, "PWM interpolated value channel 0 (expected: 50.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(60.0f, result.pwm_values[1], 1.0f, "PWM interpolated value channel 1 (expected: 60.0, actual: " + std::to_string(result.pwm_values[1]) + ")");
    runner.assert_equals(1.0f, result.current_values[0], 0.1f, "Current interpolated value channel 0 (expected: 1.0, actual: " + std::to_string(result.current_values[0]) + ")");
    runner.assert_equals(1.2f, result.current_values[1], 0.1f, "Current interpolated value channel 1 (expected: 1.2, actual: " + std::to_string(result.current_values[1]) + ")");
}

void test_presets(TestRunner& runner) {
    runner.start_suite("Preset Tests");
    
    LEDScheduler scheduler(4);
    
    // Test default preset
    scheduler.load_preset("default");
    runner.assert_false(scheduler.is_schedule_empty(), "Default preset loaded");
    runner.assert_true(scheduler.get_schedule_size() > 0, "Preset has schedule points");
    
    // Also test with legacy name
    scheduler.clear_schedule();
    scheduler.load_preset("sunrise_sunset");
    runner.assert_false(scheduler.is_schedule_empty(), "Legacy preset name still works");
    
    // Test that preset names only returns default
    auto preset_names = scheduler.get_preset_names();
    runner.assert_equals(1, static_cast<int>(preset_names.size()), "Only one preset available");
    runner.assert_true(preset_names[0] == "default", "Default preset name returned");
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
    runner.assert_equals(10.0f, result.pwm_values[0], 0.01f, "Deserialized PWM value (expected: 10.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(0.2f, result.current_values[0], 0.01f, "Deserialized current ch0 (expected: 0.2, actual: " + std::to_string(result.current_values[0]) + ")");
    runner.assert_equals(0.4f, result.current_values[1], 0.01f, "Deserialized current ch1 (expected: 0.4, actual: " + std::to_string(result.current_values[1]) + ")");
    runner.assert_equals(0.6f, result.current_values[2], 0.01f, "Deserialized current ch2 (expected: 0.6, actual: " + std::to_string(result.current_values[2]) + ")");
}

void test_json_export(TestRunner& runner) {
    runner.start_suite("JSON Export Tests");
    
    LEDScheduler scheduler(2);
    scheduler.set_schedule_point(720, {75.0f, 85.0f}, {1.5f, 1.7f}); // 12:00 PM
    
    std::string json = scheduler.export_json();
    runner.assert_true(json.length() > 50, "JSON export not empty");
    runner.assert_true(json.find("\"num_channels\":") != std::string::npos && json.find("2") != std::string::npos, "JSON contains channel count");
    runner.assert_true(json.find("\"time_minutes\":") != std::string::npos && json.find("720") != std::string::npos, "JSON contains time");
    runner.assert_true(json.find("75") != std::string::npos, "JSON contains PWM value");
    runner.assert_true(json.find("1.70") != std::string::npos, "JSON contains current value");
    
    std::cout << "Sample JSON export:\n" << json.substr(0, 200) << "..." << std::endl;
}

void test_json_import(TestRunner& runner) {
    runner.start_suite("JSON Import Tests");
    
    // Test 1: Import a simple fixed schedule
    LEDScheduler scheduler(2);
    std::string json_fixed = R"({
        "num_channels": 2,
        "schedule_points": [
            {
                "time_type": "fixed",
                "time_minutes": 360,
                "pwm_values": [50.0, 60.0],
                "current_values": [1.0, 1.2]
            },
            {
                "time_type": "fixed", 
                "time_minutes": 720,
                "pwm_values": [80.0, 90.0],
                "current_values": [1.6, 1.8]
            }
        ]
    })";
    
    bool success = scheduler.import_json(json_fixed);
    runner.assert_true(success, "Import fixed schedule succeeded");
    runner.assert_equals(2, static_cast<int>(scheduler.get_schedule_size()), "Imported 2 fixed points");
    
    // Verify imported values
    auto result = scheduler.get_values_at_time(360);
    runner.assert_equals(50.0f, result.pwm_values[0], 0.01f, "Imported PWM ch1 at 6 AM");
    runner.assert_equals(60.0f, result.pwm_values[1], 0.01f, "Imported PWM ch2 at 6 AM");
    runner.assert_equals(1.0f, result.current_values[0], 0.01f, "Imported current ch1 at 6 AM");
    
    // Test 2: Import dynamic schedule
    scheduler.clear_schedule();
    std::string json_dynamic = R"({
        "num_channels": 2,
        "schedule_points": [
            {
                "time_type": "sunrise_relative",
                "offset_minutes": -30,
                "time_minutes": 0,
                "pwm_values": [10.0, 15.0],
                "current_values": [0.2, 0.3]
            },
            {
                "time_type": "sunset_relative",
                "offset_minutes": 60,
                "time_minutes": 0,
                "pwm_values": [5.0, 7.0],
                "current_values": [0.1, 0.14]
            }
        ]
    })";
    
    success = scheduler.import_json(json_dynamic);
    runner.assert_true(success, "Import dynamic schedule succeeded");
    runner.assert_equals(2, static_cast<int>(scheduler.get_schedule_size()), "Imported 2 dynamic points");
    
    // Test 3: Import empty schedule
    scheduler.set_schedule_point(720, {50.0f, 50.0f}, {1.0f, 1.0f}); // Add a point first
    std::string json_empty = R"({
        "num_channels": 2,
        "schedule_points": []
    })";
    
    success = scheduler.import_json(json_empty);
    runner.assert_false(success, "Import empty schedule returns false");
    runner.assert_equals(0, static_cast<int>(scheduler.get_schedule_size()), "Schedule cleared on failed import");
    
    // Test 4: Import invalid JSON
    std::string json_invalid = "{ invalid json ]";
    success = scheduler.import_json(json_invalid);
    runner.assert_false(success, "Import invalid JSON returns false");
    
    // Test 5: Import with channel count change
    scheduler.set_num_channels(2);
    std::string json_channels = R"({
        "num_channels": 4,
        "schedule_points": [
            {
                "time_type": "fixed",
                "time_minutes": 600,
                "pwm_values": [30.0, 40.0, 50.0, 60.0],
                "current_values": [0.6, 0.8, 1.0, 1.2]
            }
        ]
    })";
    
    success = scheduler.import_json(json_channels);
    runner.assert_true(success, "Import with channel count change succeeded");
    runner.assert_equals(4, static_cast<int>(scheduler.get_num_channels()), "Channel count updated");
    
    auto ch_result = scheduler.get_values_at_time(600);
    runner.assert_equals(4, static_cast<int>(ch_result.pwm_values.size()), "Result has 4 channels");
    runner.assert_equals(50.0f, ch_result.pwm_values[2], 0.01f, "Channel 3 PWM value correct");
    
    // Test 6: Round-trip export/import
    LEDScheduler scheduler_export(3);
    scheduler_export.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SOLAR_NOON, 0,
        {70.0f, 80.0f, 90.0f}, {1.4f, 1.6f, 1.8f});
    scheduler_export.set_schedule_point(480, {20.0f, 25.0f, 30.0f}, {0.4f, 0.5f, 0.6f});
    
    std::string exported = scheduler_export.export_json();
    
    LEDScheduler scheduler_import(1); // Start with different channel count
    success = scheduler_import.import_json(exported);
    runner.assert_true(success, "Round-trip import succeeded");
    runner.assert_equals(3, static_cast<int>(scheduler_import.get_num_channels()), "Channel count restored");
    runner.assert_equals(2, static_cast<int>(scheduler_import.get_schedule_size()), "Schedule size preserved");
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
    runner.assert_equals(50.0f, result.pwm_values[0], 0.01f, "Original PWM preserved channel 0 (expected: 50.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(60.0f, result.pwm_values[1], 0.01f, "Original PWM preserved channel 1 (expected: 60.0, actual: " + std::to_string(result.pwm_values[1]) + ")");
    runner.assert_equals(0.0f, result.pwm_values[2], 0.01f, "New PWM defaulted to 0 channel 2 (expected: 0.0, actual: " + std::to_string(result.pwm_values[2]) + ")");
    runner.assert_equals(0.0f, result.pwm_values[3], 0.01f, "New PWM defaulted to 0 channel 3 (expected: 0.0, actual: " + std::to_string(result.pwm_values[3]) + ")");
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
    runner.assert_equals(55.0f, result.pwm_values[0], 0.01f, "Point updated correctly (expected: 55.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Remove point
    scheduler.remove_schedule_point(720);
    runner.assert_equals(2, static_cast<int>(scheduler.get_schedule_size()), "Point removed");
    
    // Clear schedule
    scheduler.clear_schedule();
    runner.assert_equals(0, static_cast<int>(scheduler.get_schedule_size()), "Schedule cleared");
    runner.assert_true(scheduler.is_schedule_empty(), "Schedule is empty after clear");
}

void test_dynamic_schedule_points(TestRunner& runner) {
    runner.start_suite("Dynamic Schedule Point Tests");
    
    LEDScheduler scheduler(2);
    
    // Test adding dynamic schedule points
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, -30,
        std::vector<float>{10.0f, 20.0f}, std::vector<float>{0.2f, 0.4f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SOLAR_NOON, 0,
        std::vector<float>{80.0f, 90.0f}, std::vector<float>{1.6f, 1.8f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 30,
        std::vector<float>{15.0f, 25.0f}, std::vector<float>{0.3f, 0.5f});
    
    runner.assert_equals(3, static_cast<int>(scheduler.get_schedule_size()), "Dynamic points added");
    
    // Test dynamic time calculation
    LEDScheduler::AstronomicalTimes astro_times;
    astro_times.sunrise_minutes = 420;  // 7:00 AM
    astro_times.sunset_minutes = 1080;  // 6:00 PM
    astro_times.solar_noon_minutes = 750; // 12:30 PM
    astro_times.valid = true;
    
    auto points = scheduler.get_schedule_points();
    
    // Test sunrise relative calculation
    uint16_t sunrise_time = scheduler.calculate_dynamic_time(points[0], astro_times);
    runner.assert_equals(390, static_cast<int>(sunrise_time), "Sunrise -30 minutes = 6:30 AM");
    
    // Test solar noon calculation
    uint16_t noon_time = scheduler.calculate_dynamic_time(points[1], astro_times);
    runner.assert_equals(750, static_cast<int>(noon_time), "Solar noon = 12:30 PM");
    
    // Test sunset relative calculation
    uint16_t sunset_time = scheduler.calculate_dynamic_time(points[2], astro_times);
    runner.assert_equals(1110, static_cast<int>(sunset_time), "Sunset +30 minutes = 6:30 PM");
    
    // Test interpolation with astronomical times
    auto result = scheduler.get_values_at_time_with_astro(750, astro_times);
    runner.assert_true(result.valid, "Dynamic interpolation valid");
    runner.assert_equals(80.0f, result.pwm_values[0], 1.0f, "Dynamic PWM value at solar noon (expected: 80.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test default preset (which is now dynamic)
    scheduler.load_preset("default");
    runner.assert_equals(5, static_cast<int>(scheduler.get_schedule_size()), "Default preset loaded");
    
    // Test JSON export includes dynamic info
    std::string json = scheduler.export_json();
    runner.assert_true(json.find("\"time_type\":") != std::string::npos && json.find("\"sunrise_relative\"") != std::string::npos, 
                      "JSON contains dynamic type info");
}

void test_dynamic_schedule_full_day(TestRunner& runner) {
    runner.start_suite("Dynamic Schedule Full Day Tests");
    
    LEDScheduler scheduler(4);  // 4 channels for easier testing
    
    // Set up astronomical times for a specific date
    // Let's use June 21 (summer solstice) for longer days
    // San Francisco location
    LEDScheduler::AstronomicalTimes astro_times;
    astro_times.astronomical_dawn_minutes = 270;   // 4:30 AM
    astro_times.nautical_dawn_minutes = 300;       // 5:00 AM  
    astro_times.civil_dawn_minutes = 330;          // 5:30 AM
    astro_times.sunrise_minutes = 360;             // 6:00 AM
    astro_times.solar_noon_minutes = 780;          // 1:00 PM
    astro_times.sunset_minutes = 1200;             // 8:00 PM
    astro_times.civil_dusk_minutes = 1230;         // 8:30 PM
    astro_times.nautical_dusk_minutes = 1260;      // 9:00 PM
    astro_times.astronomical_dusk_minutes = 1290;   // 9:30 PM
    astro_times.valid = true;
    
    // Create a realistic aquarium schedule with dynamic points
    // Night (before astronomical dawn)
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::ASTRONOMICAL_DAWN, -60,
        std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f},
        std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});
    
    // Start of astronomical dawn - very faint blue moonlight
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::ASTRONOMICAL_DAWN, 0,
        std::vector<float>{2.0f, 0.0f, 0.0f, 1.0f},   // Ch1: Blue, Ch4: Cool white
        std::vector<float>{0.04f, 0.0f, 0.0f, 0.02f});
    
    // Civil dawn - dawn begins
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::CIVIL_DAWN, 0,
        std::vector<float>{10.0f, 5.0f, 2.0f, 8.0f},
        std::vector<float>{0.2f, 0.1f, 0.04f, 0.16f});
    
    // Sunrise - morning light
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, 0,
        std::vector<float>{30.0f, 20.0f, 10.0f, 25.0f},
        std::vector<float>{0.6f, 0.4f, 0.2f, 0.5f});
    
    // Post sunrise (30 min after) - ramping up
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, 30,
        std::vector<float>{60.0f, 50.0f, 30.0f, 55.0f},
        std::vector<float>{1.2f, 1.0f, 0.6f, 1.1f});
    
    // Solar noon - peak intensity
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SOLAR_NOON, 0,
        std::vector<float>{90.0f, 85.0f, 60.0f, 88.0f},
        std::vector<float>{1.8f, 1.7f, 1.2f, 1.76f});
    
    // Pre-sunset (30 min before) - starting to ramp down
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, -30,
        std::vector<float>{60.0f, 50.0f, 30.0f, 55.0f},
        std::vector<float>{1.2f, 1.0f, 0.6f, 1.1f});
    
    // Sunset - evening light
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 0,
        std::vector<float>{25.0f, 15.0f, 5.0f, 20.0f},
        std::vector<float>{0.5f, 0.3f, 0.1f, 0.4f});
    
    // Civil dusk - fading light
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::CIVIL_DUSK, 0,
        std::vector<float>{10.0f, 5.0f, 2.0f, 8.0f},
        std::vector<float>{0.2f, 0.1f, 0.04f, 0.16f});
    
    // Astronomical dusk - back to moonlight
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::ASTRONOMICAL_DUSK, 0,
        std::vector<float>{2.0f, 0.0f, 0.0f, 1.0f},
        std::vector<float>{0.04f, 0.0f, 0.0f, 0.02f});
    
    // Late night (after astronomical dusk)
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::ASTRONOMICAL_DUSK, 60,
        std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f},
        std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});
    
    // Now test values at various times throughout the day
    
    // Test 1: Middle of the night (2:00 AM) - should be 0
    auto result = scheduler.get_values_at_time_with_astro(120, astro_times);
    runner.assert_true(result.valid, "2 AM result valid");
    runner.assert_equals(0.0f, result.pwm_values[0], 0.01f, "2 AM - Ch1 PWM is 0 (expected: 0.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, result.pwm_values[3], 0.01f, "2 AM - Ch4 PWM is 0 (expected: 0.0, actual: " + std::to_string(result.pwm_values[3]) + ")");
    
    // Test 2: Just before astronomical dawn (4:25 AM) - ramping from 0 to 2
    result = scheduler.get_values_at_time_with_astro(265, astro_times);
    runner.assert_true(result.pwm_values[0] > 0.0f && result.pwm_values[0] < 2.0f,
                      "4:25 AM - Ch1 ramping from night to moonlight (expected range: 0.0-2.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test 3: At astronomical dawn (4:30 AM) - moonlight starts
    result = scheduler.get_values_at_time_with_astro(270, astro_times);
    runner.assert_equals(2.0f, result.pwm_values[0], 0.01f, "4:30 AM - Ch1 moonlight (expected: 2.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(1.0f, result.pwm_values[3], 0.01f, "4:30 AM - Ch4 moonlight (expected: 1.0, actual: " + std::to_string(result.pwm_values[3]) + ")");
    
    // Test 4: Between astronomical and civil dawn (5:15 AM) - ramping up
    result = scheduler.get_values_at_time_with_astro(315, astro_times);
    runner.assert_true(result.pwm_values[0] > 2.0f && result.pwm_values[0] < 10.0f, 
                      "5:15 AM - Ch1 ramping up from moonlight (expected range: 2.0-10.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test 5: At sunrise (6:00 AM) - morning values
    result = scheduler.get_values_at_time_with_astro(360, astro_times);
    runner.assert_equals(30.0f, result.pwm_values[0], 0.01f, "6:00 AM - Ch1 at sunrise (expected: 30.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(25.0f, result.pwm_values[3], 0.01f, "6:00 AM - Ch4 at sunrise (expected: 25.0, actual: " + std::to_string(result.pwm_values[3]) + ")");
    
    // Test 6: Mid-morning (9:00 AM) - interpolating toward noon
    result = scheduler.get_values_at_time_with_astro(540, astro_times);
    runner.assert_true(result.pwm_values[0] > 60.0f && result.pwm_values[0] < 90.0f,
                      "9:00 AM - Ch1 interpolating to noon (expected range: 60.0-90.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test 7: Solar noon (1:00 PM) - peak intensity
    result = scheduler.get_values_at_time_with_astro(780, astro_times);
    runner.assert_equals(90.0f, result.pwm_values[0], 0.01f, "1:00 PM - Ch1 at peak (expected: 90.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(88.0f, result.pwm_values[3], 0.01f, "1:00 PM - Ch4 at peak (expected: 88.0, actual: " + std::to_string(result.pwm_values[3]) + ")");
    runner.assert_equals(1.8f, result.current_values[0], 0.01f, "1:00 PM - Ch1 current at peak (expected: 1.8, actual: " + std::to_string(result.current_values[0]) + ")");
    
    // Test 8: Late afternoon (5:00 PM) - starting to decrease
    result = scheduler.get_values_at_time_with_astro(1020, astro_times);
    runner.assert_true(result.pwm_values[0] > 60.0f && result.pwm_values[0] < 90.0f,
                      "5:00 PM - Ch1 decreasing from peak (expected range: 60.0-90.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test 9: At sunset (8:00 PM) - evening values
    result = scheduler.get_values_at_time_with_astro(1200, astro_times);
    runner.assert_equals(25.0f, result.pwm_values[0], 0.01f, "8:00 PM - Ch1 at sunset (expected: 25.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(0.5f, result.current_values[0], 0.01f, "8:00 PM - Ch1 current at sunset (expected: 0.5, actual: " + std::to_string(result.current_values[0]) + ")");
    
    // Test 10: During dusk (8:45 PM) - fading to moonlight
    result = scheduler.get_values_at_time_with_astro(1245, astro_times);
    runner.assert_true(result.pwm_values[0] > 2.0f && result.pwm_values[0] < 10.0f,
                      "8:45 PM - Ch1 fading to moonlight (expected range: 2.0-10.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test 11: Late night (11:00 PM) - back to 0
    result = scheduler.get_values_at_time_with_astro(1380, astro_times);
    runner.assert_equals(0.0f, result.pwm_values[0], 0.01f, "11:00 PM - Ch1 back to 0 (expected: 0.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, result.current_values[0], 0.01f, "11:00 PM - Ch1 current is 0 (expected: 0.0, actual: " + std::to_string(result.current_values[0]) + ")");
    
    // Test ramping characteristics
    runner.start_suite("Dynamic Schedule Ramping Tests");
    
    // Verify smooth ramping during sunrise period (5:30 AM to 6:30 AM)
    float prev_value = 0.0f;
    bool smooth_ramp_up = true;
    for (int minutes = 330; minutes <= 390; minutes += 5) {
        result = scheduler.get_values_at_time_with_astro(minutes, astro_times);
        if (minutes > 330 && result.pwm_values[0] <= prev_value) {
            smooth_ramp_up = false;
            std::cout << "Sunrise ramp failed at " << minutes << " minutes: prev=" << prev_value << ", current=" << result.pwm_values[0] << std::endl;
            break;
        }
        prev_value = result.pwm_values[0];
    }
    runner.assert_true(smooth_ramp_up, "Sunrise period shows smooth ramp up (values must increase monotonically)");
    
    // Verify smooth ramping during sunset period (7:30 PM to 8:30 PM)
    prev_value = 100.0f;
    bool smooth_ramp_down = true;
    for (int minutes = 1170; minutes <= 1230; minutes += 5) {
        result = scheduler.get_values_at_time_with_astro(minutes, astro_times);
        if (minutes > 1170 && result.pwm_values[0] >= prev_value) {
            smooth_ramp_down = false;
            std::cout << "Sunset ramp failed at " << minutes << " minutes: prev=" << prev_value << ", current=" << result.pwm_values[0] << std::endl;
            break;
        }
        prev_value = result.pwm_values[0];
    }
    runner.assert_true(smooth_ramp_down, "Sunset period shows smooth ramp down (values must decrease monotonically)");
    
    // Test edge case: Midnight wrap-around
    runner.start_suite("Dynamic Schedule Midnight Wrap Tests");
    
    // Test just before midnight
    result = scheduler.get_values_at_time_with_astro(1439, astro_times);
    runner.assert_equals(0.0f, result.pwm_values[0], 0.01f, "11:59 PM - Ch1 is 0 (expected: 0.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
    
    // Test just after midnight
    result = scheduler.get_values_at_time_with_astro(1, astro_times);
    runner.assert_equals(0.0f, result.pwm_values[0], 0.01f, "12:01 AM - Ch1 is 0 (expected: 0.0, actual: " + std::to_string(result.pwm_values[0]) + ")");
}

void test_dynamic_schedule_seasons(TestRunner& runner) {
    runner.start_suite("Dynamic Schedule Seasonal Tests");
    
    LEDScheduler scheduler(2);  // 2 channels for simpler testing
    
    // Create a simple dynamic schedule
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, -30,
        std::vector<float>{10.0f, 10.0f}, std::vector<float>{0.2f, 0.2f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, 0,
        std::vector<float>{50.0f, 50.0f}, std::vector<float>{1.0f, 1.0f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 0,
        std::vector<float>{50.0f, 50.0f}, std::vector<float>{1.0f, 1.0f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 30,
        std::vector<float>{10.0f, 10.0f}, std::vector<float>{0.2f, 0.2f});
    
    // Test with summer solstice times (long day)
    LEDScheduler::AstronomicalTimes summer_times;
    summer_times.sunrise_minutes = 360;   // 6:00 AM
    summer_times.sunset_minutes = 1200;   // 8:00 PM (14 hour day)
    summer_times.solar_noon_minutes = 780; // 1:00 PM
    summer_times.valid = true;
    
    // Test with winter solstice times (short day)
    LEDScheduler::AstronomicalTimes winter_times;
    winter_times.sunrise_minutes = 480;   // 8:00 AM
    winter_times.sunset_minutes = 1020;   // 5:00 PM (9 hour day)
    winter_times.solar_noon_minutes = 750; // 12:30 PM
    winter_times.valid = true;
    
    // Test summer sunrise time
    auto summer_sunrise = scheduler.get_values_at_time_with_astro(360, summer_times);
    runner.assert_equals(50.0f, summer_sunrise.pwm_values[0], 0.01f, "Summer sunrise at 6:00 AM (expected: 50.0, actual: " + std::to_string(summer_sunrise.pwm_values[0]) + ")");
    
    // Test winter sunrise time (should be different)
    auto winter_sunrise = scheduler.get_values_at_time_with_astro(480, winter_times);
    runner.assert_equals(50.0f, winter_sunrise.pwm_values[0], 0.01f, "Winter sunrise at 8:00 AM (expected: 50.0, actual: " + std::to_string(winter_sunrise.pwm_values[0]) + ")");
    
    // Test that at 6:00 AM in winter, lights are still off (before sunrise)
    auto winter_early = scheduler.get_values_at_time_with_astro(360, winter_times);
    runner.assert_true(winter_early.pwm_values[0] < 50.0f, "Winter 6:00 AM - before sunrise, lights low (expected: < 50.0, actual: " + std::to_string(winter_early.pwm_values[0]) + ")");
    
    // Test that at 8:00 PM in winter, lights are already off (after sunset)
    auto winter_late = scheduler.get_values_at_time_with_astro(1200, winter_times);
    runner.assert_true(winter_late.pwm_values[0] < 50.0f, "Winter 8:00 PM - after sunset, lights low (expected: < 50.0, actual: " + std::to_string(winter_late.pwm_values[0]) + ")");
    
    // Verify day length affects midday timing
    auto summer_noon = scheduler.get_values_at_time_with_astro(780, summer_times);
    auto winter_noon = scheduler.get_values_at_time_with_astro(750, winter_times);
    runner.assert_equals(50.0f, summer_noon.pwm_values[0], 0.01f, "Summer noon intensity (expected: 50.0, actual: " + std::to_string(summer_noon.pwm_values[0]) + ")");
    runner.assert_equals(50.0f, winter_noon.pwm_values[0], 0.01f, "Winter noon intensity (expected: 50.0, actual: " + std::to_string(winter_noon.pwm_values[0]) + ")");
}

void test_dynamic_midnight_crossing(TestRunner& runner) {
    runner.start_suite("Dynamic Schedule Midnight Crossing Tests");
    
    LEDScheduler scheduler(2);  // 2 channels for testing
    scheduler.clear_schedule();
    
    // Set up a dynamic schedule that spans across midnight
    // Sunset at 8 PM (1200), with lights extending past midnight
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, -120,  // 6 PM (2 hours before sunset)
        std::vector<float>{80.0f, 70.0f}, std::vector<float>{1.6f, 1.4f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 0,     // 8 PM (sunset)
        std::vector<float>{50.0f, 40.0f}, std::vector<float>{1.0f, 0.8f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 180,   // 11 PM (3 hours after sunset)
        std::vector<float>{20.0f, 15.0f}, std::vector<float>{0.4f, 0.3f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNSET_RELATIVE, 300,   // 1 AM next day (5 hours after sunset)
        std::vector<float>{0.0f, 0.0f}, std::vector<float>{0.0f, 0.0f});
    
    // Morning schedule
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, -60,   // 5 AM (1 hour before sunrise)
        std::vector<float>{10.0f, 8.0f}, std::vector<float>{0.2f, 0.16f});
    scheduler.add_dynamic_schedule_point(LEDScheduler::DynamicTimeType::SUNRISE_RELATIVE, 0,     // 6 AM (sunrise)
        std::vector<float>{40.0f, 35.0f}, std::vector<float>{0.8f, 0.7f});
    
    // Set up astronomical times
    LEDScheduler::AstronomicalTimes astro_times;
    astro_times.sunrise_minutes = 360;    // 6:00 AM
    astro_times.sunset_minutes = 1200;    // 8:00 PM
    astro_times.solar_noon_minutes = 780; // 1:00 PM
    astro_times.valid = true;
    
    // Test 1: Check values at 11:59 PM (just before midnight)
    auto before_midnight = scheduler.get_values_at_time_with_astro(1439, astro_times); // 23:59
    runner.assert_true(before_midnight.valid, "11:59 PM result should be valid");
    runner.assert_true(before_midnight.pwm_values[0] > 0.0f && before_midnight.pwm_values[0] < 20.0f, 
                      "11:59 PM - Ch1 should be dimming (expected: 0-20, actual: " + std::to_string(before_midnight.pwm_values[0]) + ")");
    
    // Test 2: Check values at 12:00 AM (midnight)
    auto at_midnight = scheduler.get_values_at_time_with_astro(0, astro_times); // 00:00
    runner.assert_true(at_midnight.valid, "Midnight result should be valid");
    runner.assert_true(at_midnight.pwm_values[0] > 0.0f && at_midnight.pwm_values[0] < 20.0f, 
                      "Midnight - Ch1 should be dimming (expected: 0-20, actual: " + std::to_string(at_midnight.pwm_values[0]) + ")");
    
    // Test 3: Check values at 12:30 AM (after midnight)
    auto after_midnight = scheduler.get_values_at_time_with_astro(30, astro_times); // 00:30
    runner.assert_true(after_midnight.valid, "12:30 AM result should be valid");
    runner.assert_true(after_midnight.pwm_values[0] >= 0.0f && after_midnight.pwm_values[0] < 20.0f, 
                      "12:30 AM - Ch1 should be dimming further (expected: 0-20, actual: " + std::to_string(after_midnight.pwm_values[0]) + ")");
    
    // Test 4: Check values at 1:00 AM (lights should be off)
    auto one_am = scheduler.get_values_at_time_with_astro(60, astro_times); // 01:00
    runner.assert_true(one_am.valid, "1:00 AM result should be valid");
    runner.assert_equals(0.0f, one_am.pwm_values[0], 0.01f, 
                        "1:00 AM - Ch1 should be off (expected: 0.0, actual: " + std::to_string(one_am.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, one_am.pwm_values[1], 0.01f, 
                        "1:00 AM - Ch2 should be off (expected: 0.0, actual: " + std::to_string(one_am.pwm_values[1]) + ")");
    
    // Test 5: Check values at 5:00 AM (lights starting to turn on)
    auto five_am = scheduler.get_values_at_time_with_astro(300, astro_times); // 05:00
    runner.assert_true(five_am.valid, "5:00 AM result should be valid");
    runner.assert_equals(10.0f, five_am.pwm_values[0], 0.01f, 
                        "5:00 AM - Ch1 morning start (expected: 10.0, actual: " + std::to_string(five_am.pwm_values[0]) + ")");
    
    // Test 6: Verify smooth transition across midnight boundary
    float pwm_2359 = before_midnight.pwm_values[0];
    float pwm_0000 = at_midnight.pwm_values[0];
    float pwm_0030 = after_midnight.pwm_values[0];
    runner.assert_true(pwm_2359 >= pwm_0000 || std::abs(pwm_2359 - pwm_0000) < 2.0f, 
                      "PWM should transition smoothly across midnight (11:59 PM: " + std::to_string(pwm_2359) + 
                      ", Midnight: " + std::to_string(pwm_0000) + ")");
    runner.assert_true(pwm_0000 >= pwm_0030, 
                      "PWM should continue decreasing after midnight (Midnight: " + std::to_string(pwm_0000) + 
                      ", 12:30 AM: " + std::to_string(pwm_0030) + ")");
    
    // Test 7: Test with different astronomical times (winter with earlier sunset)
    LEDScheduler::AstronomicalTimes winter_times;
    winter_times.sunrise_minutes = 420;   // 7:00 AM
    winter_times.sunset_minutes = 1020;   // 5:00 PM (earlier sunset)
    winter_times.solar_noon_minutes = 720; // 12:00 PM
    winter_times.valid = true;
    
    auto winter_midnight = scheduler.get_values_at_time_with_astro(0, winter_times);
    runner.assert_true(winter_midnight.valid, "Winter midnight result should be valid");
    // At midnight in winter (7 hours after 5 PM sunset), lights are still interpolating between sunset+5h (10 PM) and sunrise-1h (6 AM)
    // The dynamic schedule has sunset+5h (10 PM) = 0 and sunrise-1h (6 AM) = 10, so midnight should be interpolating
    float expected_midnight = 2.5f; // Interpolating between 0 at 10 PM and 10 at 6 AM
    runner.assert_equals(expected_midnight, winter_midnight.pwm_values[0], 0.01f, 
                        "Winter midnight - Ch1 interpolating (expected: 2.5, actual: " + std::to_string(winter_midnight.pwm_values[0]) + ")");
}

void test_pwm_scaling(TestRunner& runner) {
    runner.start_suite("PWM Scaling Tests");
    
    LEDScheduler scheduler(2);  // 2 channels for testing
    
    // Create a simple schedule
    scheduler.set_schedule_point(720, {80.0f, 60.0f}, {1.6f, 1.2f}); // 12:00 PM
    
    // Test 1: Normal values without scaling
    auto normal_result = scheduler.get_values_at_time(720);
    runner.assert_equals(80.0f, normal_result.pwm_values[0], 0.01f, 
                        "Normal Ch1 PWM (expected: 80.0, actual: " + std::to_string(normal_result.pwm_values[0]) + ")");
    runner.assert_equals(60.0f, normal_result.pwm_values[1], 0.01f, 
                        "Normal Ch2 PWM (expected: 60.0, actual: " + std::to_string(normal_result.pwm_values[1]) + ")");
    
    // Note: PWM scaling is applied in the ESPHome component, not in the scheduler itself
    // The scheduler always returns unscaled values
    // This test verifies the scheduler continues to work correctly
    
    // Test 2: Values remain consistent
    auto result2 = scheduler.get_values_at_time(720);
    runner.assert_equals(80.0f, result2.pwm_values[0], 0.01f, 
                        "Consistent Ch1 PWM (expected: 80.0, actual: " + std::to_string(result2.pwm_values[0]) + ")");
    runner.assert_equals(60.0f, result2.pwm_values[1], 0.01f, 
                        "Consistent Ch2 PWM (expected: 60.0, actual: " + std::to_string(result2.pwm_values[1]) + ")");
    
    // Test 3: Interpolation still works correctly
    scheduler.set_schedule_point(840, {40.0f, 30.0f}, {0.8f, 0.6f}); // 2:00 PM
    auto interp_result = scheduler.get_values_at_time(780); // 1:00 PM (midpoint)
    runner.assert_equals(60.0f, interp_result.pwm_values[0], 0.01f, 
                        "Interpolated Ch1 PWM (expected: 60.0, actual: " + std::to_string(interp_result.pwm_values[0]) + ")");
    runner.assert_equals(45.0f, interp_result.pwm_values[1], 0.01f, 
                        "Interpolated Ch2 PWM (expected: 45.0, actual: " + std::to_string(interp_result.pwm_values[1]) + ")");
}

void test_moon_simulation(TestRunner& runner) {
    runner.start_suite("Moon Simulation Tests");
    
    LEDScheduler scheduler(4);  // 4 channels for moon testing
    
    // Configure moon simulation
    LEDScheduler::MoonSimulation moon_config;
    moon_config.enabled = true;
    moon_config.base_intensity = {3.0f, 0.0f, 0.0f, 1.5f};  // Blue and white channels
    moon_config.phase_scaling = true;
    scheduler.set_moon_simulation(moon_config);
    
    // Verify moon config was set
    auto verify_config = scheduler.get_moon_simulation();
    runner.assert_true(verify_config.enabled, "Moon simulation should be enabled");
    runner.assert_equals(4, static_cast<int>(verify_config.base_intensity.size()), "Moon base intensity should have 4 values");
    
    // Create simple schedule - on during day, off at night
    scheduler.clear_schedule();
    scheduler.set_schedule_point(0, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f});       // Midnight - lights off
    scheduler.set_schedule_point(360, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f});     // 6 AM - lights still off
    scheduler.set_schedule_point(480, {50.0f, 50.0f, 50.0f, 50.0f}, {1.0f, 1.0f, 1.0f, 1.0f}); // 8 AM - lights on
    scheduler.set_schedule_point(720, {50.0f, 50.0f, 50.0f, 50.0f}, {1.0f, 1.0f, 1.0f, 1.0f}); // 12 PM - lights still on
    scheduler.set_schedule_point(1200, {50.0f, 50.0f, 50.0f, 50.0f}, {1.0f, 1.0f, 1.0f, 1.0f}); // 8 PM - lights still on
    scheduler.set_schedule_point(1260, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f});    // 9 PM - lights off
    
    // Set up astronomical times with moon data
    LEDScheduler::AstronomicalTimes astro_times;
    astro_times.sunrise_minutes = 360;
    astro_times.sunset_minutes = 1200;
    astro_times.moonrise_minutes = 1140;  // 7:00 PM
    astro_times.moonset_minutes = 420;    // 7:00 AM (next day - this crosses midnight)
    astro_times.moon_phase = 0.5f;        // Full moon
    astro_times.valid = true;
    
    // Test 1: During day with lights on - no moon
    auto day_result = scheduler.get_values_at_time_with_astro(720, astro_times);  // Noon
    runner.assert_equals(50.0f, day_result.pwm_values[0], 0.01f, 
                        "Noon Ch1 - regular light (expected: 50.0, actual: " + std::to_string(day_result.pwm_values[0]) + ")");
    runner.assert_equals(50.0f, day_result.pwm_values[3], 0.01f, 
                        "Noon Ch4 - regular light (expected: 50.0, actual: " + std::to_string(day_result.pwm_values[3]) + ")");
    
    // Test 2: Night with moon visible and lights off - moonlight active
    
    auto night_result = scheduler.get_values_at_time_with_astro(1320, astro_times);  // 10 PM
    // Debug: Check if result is valid
    runner.assert_true(night_result.valid, "Night result should be valid");
    runner.assert_equals(4, static_cast<int>(night_result.pwm_values.size()), "Night result should have 4 channels");
    
    runner.assert_equals(3.0f, night_result.pwm_values[0], 0.01f, 
                        "10 PM Ch1 - full moon blue (expected: 3.0, actual: " + std::to_string(night_result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, night_result.pwm_values[1], 0.01f, 
                        "10 PM Ch2 - no red moon (expected: 0.0, actual: " + std::to_string(night_result.pwm_values[1]) + ")");
    runner.assert_equals(1.5f, night_result.pwm_values[3], 0.01f, 
                        "10 PM Ch4 - full moon white (expected: 1.5, actual: " + std::to_string(night_result.pwm_values[3]) + ")");
    
    // Test 3: New moon phase - dimmer moonlight
    astro_times.moon_phase = 0.0f;  // New moon
    auto new_moon_result = scheduler.get_values_at_time_with_astro(1320, astro_times);  // 10 PM
    runner.assert_equals(0.0f, new_moon_result.pwm_values[0], 0.01f, 
                        "New moon Ch1 - no light (expected: 0.0, actual: " + std::to_string(new_moon_result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, new_moon_result.pwm_values[3], 0.01f, 
                        "New moon Ch4 - no light (expected: 0.0, actual: " + std::to_string(new_moon_result.pwm_values[3]) + ")");
    
    // Test 4: Quarter moon phase
    astro_times.moon_phase = 0.25f;  // Quarter moon
    auto quarter_moon_result = scheduler.get_values_at_time_with_astro(1320, astro_times);
    runner.assert_equals(1.5f, quarter_moon_result.pwm_values[0], 0.01f, 
                        "Quarter moon Ch1 - half intensity (expected: 1.5, actual: " + std::to_string(quarter_moon_result.pwm_values[0]) + ")");
    runner.assert_equals(0.75f, quarter_moon_result.pwm_values[3], 0.01f, 
                        "Quarter moon Ch4 - half intensity (expected: 0.75, actual: " + std::to_string(quarter_moon_result.pwm_values[3]) + ")");
    
    // Test 5: Moon below horizon at night - no moonlight  
    // Update moon times so moon is NOT visible at night (rises at 6 AM, sets at 6 PM)
    astro_times.moonrise_minutes = 360;   // 6:00 AM
    astro_times.moonset_minutes = 1080;   // 6:00 PM
    astro_times.moon_phase = 0.5f;        // Full moon
    auto no_moon_result = scheduler.get_values_at_time_with_astro(1320, astro_times);  // 10 PM (moon has set)
    runner.assert_equals(0.0f, no_moon_result.pwm_values[0], 0.01f, 
                        "Moon set Ch1 - no moonlight (expected: 0.0, actual: " + std::to_string(no_moon_result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, no_moon_result.pwm_values[3], 0.01f, 
                        "Moon set Ch4 - no moonlight (expected: 0.0, actual: " + std::to_string(no_moon_result.pwm_values[3]) + ")");
    
    // Restore original moon times for remaining tests
    astro_times.moonrise_minutes = 1140;  // 7:00 PM
    astro_times.moonset_minutes = 420;    // 7:00 AM (next day)
    
    // Test 6: Disable moon simulation
    scheduler.enable_moon_simulation(false);
    auto disabled_result = scheduler.get_values_at_time_with_astro(1320, astro_times);  // 10 PM
    runner.assert_equals(0.0f, disabled_result.pwm_values[0], 0.01f, 
                        "Moon disabled Ch1 - no light (expected: 0.0, actual: " + std::to_string(disabled_result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, disabled_result.pwm_values[3], 0.01f, 
                        "Moon disabled Ch4 - no light (expected: 0.0, actual: " + std::to_string(disabled_result.pwm_values[3]) + ")");
    
    // Test 7: Phase scaling disabled
    scheduler.enable_moon_simulation(true);
    moon_config.phase_scaling = false;
    scheduler.set_moon_simulation(moon_config);
    astro_times.moon_phase = 0.25f;  // Quarter moon
    auto no_scale_result = scheduler.get_values_at_time_with_astro(1320, astro_times);
    runner.assert_equals(3.0f, no_scale_result.pwm_values[0], 0.01f, 
                        "No phase scaling Ch1 - full intensity (expected: 3.0, actual: " + std::to_string(no_scale_result.pwm_values[0]) + ")");
    runner.assert_equals(1.5f, no_scale_result.pwm_values[3], 0.01f, 
                        "No phase scaling Ch4 - full intensity (expected: 1.5, actual: " + std::to_string(no_scale_result.pwm_values[3]) + ")");
    
    // Test 8: Moon rise/set crossing midnight
    astro_times.moonrise_minutes = 1380;  // 11:00 PM
    astro_times.moonset_minutes = 360;    // 6:00 AM (next day)
    astro_times.moon_phase = 0.5f;
    moon_config.phase_scaling = true;  // Re-enable phase scaling
    scheduler.set_moon_simulation(moon_config);
    auto midnight_moon_result = scheduler.get_values_at_time_with_astro(60, astro_times);  // 1:00 AM
    runner.assert_equals(3.0f, midnight_moon_result.pwm_values[0], 0.01f, 
                        "Midnight crossing Ch1 - moon visible (expected: 3.0, actual: " + std::to_string(midnight_moon_result.pwm_values[0]) + ")");
    
    // Test 9: Very low main light threshold
    scheduler.set_schedule_point(1320, {0.05f, 0.0f, 0.0f, 0.0f}, {0.001f, 0.0f, 0.0f, 0.0f});  // 10 PM
    auto threshold_result = scheduler.get_values_at_time_with_astro(1320, astro_times);
    runner.assert_equals(0.05f, threshold_result.pwm_values[0], 0.01f, 
                        "Below threshold Ch1 - main light preserved (expected: 0.05, actual: " + std::to_string(threshold_result.pwm_values[0]) + ")");
    runner.assert_equals(0.0f, threshold_result.pwm_values[3], 0.01f, 
                        "Below threshold Ch4 - no moon added (expected: 0.0, actual: " + std::to_string(threshold_result.pwm_values[3]) + ")");
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
    
    test_json_import(runner);
    results.add_suite_results(runner);
    
    test_edge_cases(runner);
    results.add_suite_results(runner);
    
    test_channel_management(runner);
    results.add_suite_results(runner);
    
    test_mutations(runner);
    results.add_suite_results(runner);
    
    test_dynamic_schedule_points(runner);
    results.add_suite_results(runner);
    
    test_dynamic_schedule_full_day(runner);
    results.add_suite_results(runner);
    
    test_dynamic_schedule_seasons(runner);
    results.add_suite_results(runner);
    
    test_dynamic_midnight_crossing(runner);
    results.add_suite_results(runner);
    
    test_pwm_scaling(runner);
    results.add_suite_results(runner);
    
    test_moon_simulation(runner);
    results.add_suite_results(runner);
    
    results.print_final_summary("LED Scheduler");
    
    return results.get_exit_code();
}