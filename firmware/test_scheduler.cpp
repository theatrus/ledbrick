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

    // An unknown preset is reported and leaves the schedule alone
    size_t loaded_size = scheduler.get_schedule_size();
    runner.assert_true(scheduler.load_preset("default"), "Known preset reports success");
    runner.assert_false(scheduler.load_preset("no_such_preset"), "Unknown preset reports failure");
    runner.assert_equals(static_cast<int>(loaded_size), static_cast<int>(scheduler.get_schedule_size()),
                         "Unknown preset leaves schedule unchanged");

    // A preset kept with save_preset loads back
    scheduler.clear_schedule();
    scheduler.add_schedule_point(LEDScheduler::SchedulePoint(600, {10.0f, 20.0f, 30.0f, 40.0f}, {0.5f, 0.5f, 0.5f, 0.5f}));
    scheduler.save_preset("mine");
    scheduler.load_preset("default");
    runner.assert_true(scheduler.load_preset("mine"), "Saved preset loads");
    runner.assert_equals(1, static_cast<int>(scheduler.get_schedule_size()), "Saved preset restores its points");
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
    runner.assert_true(json.find("1.7") != std::string::npos, "JSON contains current value");
    runner.assert_true(json.find("1.7000000476837158") == std::string::npos, "JSON numbers are rounded");
    
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
    size_t size_before_empty_import = scheduler.get_schedule_size();
    std::string json_empty = R"({
        "num_channels": 2,
        "schedule_points": []
    })";
    
    success = scheduler.import_json(json_empty);
    runner.assert_false(success, "Import empty schedule returns false");
    runner.assert_equals(static_cast<int>(size_before_empty_import), static_cast<int>(scheduler.get_schedule_size()),
                         "Schedule unchanged on failed import");
    
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
    moon_config.phase_scaling_pwm = true;
    moon_config.phase_scaling_current = true;
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
    moon_config.phase_scaling_pwm = false;
    moon_config.phase_scaling_current = false;
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
    moon_config.phase_scaling_pwm = true;  // Re-enable phase scaling
    moon_config.phase_scaling_current = true;
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

void test_json_import_validation(TestRunner& runner) {
    runner.start_suite("JSON Import Validation Tests");

    // Out-of-range values are clamped instead of dropped or accepted as-is
    LEDScheduler scheduler(2);
    std::string json_out_of_range = R"({
        "num_channels": 2,
        "channel_configs": [
            {"rgb_hex": "#FFFFFF", "max_current": 50.0, "name": "A"},
            {"rgb_hex": "#0000FF", "max_current": 1.0, "name": "B"}
        ],
        "schedule_points": [
            {"time_type": "fixed", "time_minutes": 600,
             "pwm_values": [500.0, -5.0], "current_values": [-3.0, 1.5]},
            {"time_type": "sunrise_relative", "offset_minutes": 0, "time_minutes": 0,
             "pwm_values": [150.0, 20.0], "current_values": [9.0, 0.5]}
        ],
        "moon_simulation": {"enabled": true, "min_current_threshold": 7.0,
                            "base_intensity": [300.0, 5.0], "base_current": [4.0, -1.0]}
    })";
    runner.assert_true(scheduler.import_json(json_out_of_range), "Out-of-range import accepted");
    runner.assert_equals(LEDScheduler::MAX_CHANNEL_CURRENT, scheduler.get_channel_max_current(0), 0.001f,
                         "Channel max current clamped to hardware limit");
    auto points = scheduler.get_schedule_points();
    runner.assert_equals(2, static_cast<int>(points.size()), "Both points kept");
    for (const auto& point : points) {
        bool in_range = true;
        for (float pwm : point.pwm_values) in_range = in_range && pwm >= 0.0f && pwm <= 100.0f;
        for (size_t i = 0; i < point.current_values.size(); i++) {
            in_range = in_range && point.current_values[i] >= 0.0f &&
                       point.current_values[i] <= scheduler.get_channel_max_current(i);
        }
        runner.assert_true(in_range, "Point values clamped");
    }
    auto moon = scheduler.get_moon_simulation();
    runner.assert_equals(100.0f, moon.base_intensity[0], 0.001f, "Moon intensity clamped");
    runner.assert_equals(0.0f, moon.base_current[1], 0.001f, "Negative moon current clamped");
    runner.assert_equals(LEDScheduler::MAX_CHANNEL_CURRENT, moon.min_current_threshold, 0.001f,
                         "Moon threshold clamped");

    // Current above a lowered channel limit keeps the point (it used to be dropped)
    LEDScheduler lowered(1);
    std::string json_lowered = R"({
        "num_channels": 1,
        "channel_configs": [{"rgb_hex": "#FFFFFF", "max_current": 0.5}],
        "schedule_points": [{"time_type": "fixed", "time_minutes": 60,
                             "pwm_values": [50.0], "current_values": [1.5]}]
    })";
    runner.assert_true(lowered.import_json(json_lowered), "Import with current above channel limit accepted");
    runner.assert_equals(1, static_cast<int>(lowered.get_schedule_size()), "Point kept");
    runner.assert_equals(0.5f, lowered.get_schedule_points()[0].current_values[0], 0.001f, "Current clamped to limit");

    // Structural errors fail the whole import and change nothing
    LEDScheduler unchanged(2);
    unchanged.set_schedule_point(480, {10.0f, 20.0f}, {0.5f, 0.5f});
    unchanged.set_channel_max_current(0, 1.25f);
    const char* bad_imports[] = {
        R"({"num_channels": 2, "schedule_points": [{"time_type": "fixed", "time_minutes": 5000,
            "pwm_values": [1, 1], "current_values": [1, 1]}]})",
        R"({"num_channels": 2, "schedule_points": [{"time_type": "moonrise_ish", "offset_minutes": 0,
            "pwm_values": [1, 1], "current_values": [1, 1]}]})",
        R"({"num_channels": 2, "schedule_points": [{"time_type": "sunset_relative", "offset_minutes": 9999,
            "pwm_values": [1, 1], "current_values": [1, 1]}]})",
        R"({"num_channels": 99, "schedule_points": []})",
        R"([1, 2, 3])",
    };
    for (const char* bad : bad_imports) {
        runner.assert_false(unchanged.import_json(bad), "Bad import rejected");
    }
    runner.assert_equals(1, static_cast<int>(unchanged.get_schedule_size()), "Schedule unchanged after bad imports");
    runner.assert_equals(2, static_cast<int>(unchanged.get_num_channels()), "Channel count unchanged after bad imports");
    runner.assert_equals(1.25f, unchanged.get_channel_max_current(0), 0.001f, "Channel config unchanged after bad imports");
}

void test_exact_point_time(TestRunner& runner) {
    runner.start_suite("Exact Point Time Tests");

    // On a point's exact minute the result must still get the current clamp and moonlight
    LEDScheduler scheduler(2);
    scheduler.set_schedule_point(600, {0.0f, 0.0f}, {2.0f, 2.0f});
    scheduler.set_schedule_point(700, {0.0f, 0.0f}, {2.0f, 2.0f});
    scheduler.set_channel_max_current(0, 0.5f);

    LEDScheduler::MoonSimulation moon;
    moon.enabled = true;
    moon.phase_scaling_pwm = false;
    moon.phase_scaling_current = false;
    moon.base_intensity = {5.0f, 5.0f};
    moon.base_current = {0.1f, 0.1f};
    scheduler.set_moon_simulation(moon);

    LEDScheduler::AstronomicalTimes astro;
    astro.moonrise_minutes = 1;
    astro.moonset_minutes = 1438;
    astro.moon_phase = 0.5f;
    astro.valid = true;
    scheduler.set_astronomical_times(astro);

    auto before = scheduler.get_values_at_time_with_astro(599, astro);
    auto exact = scheduler.get_values_at_time_with_astro(600, astro);
    auto after = scheduler.get_values_at_time_with_astro(601, astro);
    runner.assert_true(before.pwm_values[0] > 0.0f, "Moonlight on before the point");
    runner.assert_equals(before.pwm_values[0], exact.pwm_values[0], 0.001f, "Moonlight unchanged on the point's minute");
    runner.assert_equals(after.pwm_values[0], exact.pwm_values[0], 0.001f, "Moonlight unchanged after the point");

    // Without moonlight, the exact minute is still limited to the channel maximum
    scheduler.enable_moon_simulation(false);
    auto clamped = scheduler.get_values_at_time_with_astro(600, astro);
    runner.assert_equals(0.5f, clamped.current_values[0], 0.001f, "Current clamped on the point's minute");
}

void test_interpolation_to_the_second(TestRunner& runner) {
    runner.start_suite("Interpolation To The Second Tests");

    LEDScheduler scheduler(2);
    scheduler.set_schedule_point(600, {0.0f, 0.0f}, {0.0f, 0.0f});
    scheduler.set_schedule_point(660, {60.0f, 30.0f}, {1.0f, 0.5f});
    LEDScheduler::AstronomicalTimes astro;
    astro.valid = true;

    // Whole minutes match the minute API
    auto by_minute = scheduler.get_values_at_time_with_astro(630, astro);
    auto by_second = scheduler.get_values_at_seconds_with_astro(630 * 60, astro);
    runner.assert_equals(by_minute.pwm_values[0], by_second.pwm_values[0], 0.0001f, "Same value on a whole minute");
    runner.assert_equals(by_minute.current_values[1], by_second.current_values[1], 0.0001f, "Same current on a whole minute");

    // Between minutes the value moves instead of holding for the whole minute
    auto half = scheduler.get_values_at_seconds_with_astro(630 * 60 + 30, astro);
    runner.assert_equals(30.5f, half.pwm_values[0], 0.001f, "Half a minute later: half a minute's change");
    runner.assert_equals(0.5083f, half.current_values[0], 0.001f, "Current also moves within the minute");

    // Every second of the ramp is a little higher than the last
    bool rising = true;
    float last = -1.0f;
    for (uint32_t s = 600 * 60; s <= 660 * 60; s += 7) {
        float v = scheduler.get_values_at_seconds_with_astro(s, astro).pwm_values[0];
        if (s > 600 * 60 && !(v > last)) rising = false;
        last = v;
    }
    runner.assert_true(rising, "Ramp rises at every sample, not once a minute");

    // Out of range is invalid, wrap-around still works near midnight
    runner.assert_false(scheduler.get_values_at_seconds_with_astro(1440 * 60, astro).valid, "86400 s is out of range");
    runner.assert_true(scheduler.get_values_at_seconds_with_astro(1440 * 60 - 1, astro).valid, "Last second of the day is valid");
}

void test_channel_dimming(TestRunner& runner) {
    runner.start_suite("Channel Dimming Tests");
    using ledbrick::DimMode;
    using ledbrick::DimPriority;

    LEDScheduler scheduler(8);
    for (uint8_t c = 0; c < 8; c++) scheduler.set_channel_max_current(c, 1.0f);
    std::vector<float> pwm = {50, 50, 50, 50, 50, 50, 50, 50};
    std::vector<float> cur = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    scheduler.set_schedule_point(600, pwm, cur);
    runner.assert_false(scheduler.is_curve_channel(4), "Channels start in manual mode");

    // Manual to curve: the point becomes a level and the current is no longer used
    auto dimmer_before = scheduler.channel_dimmer(4);
    ledbrick::DriveLimits limits;
    limits.max_current_a = 1.0f;
    float expected_level = dimmer_before.level_for_drive(0.5f, 0.5f, limits) * 100.0f;
    std::string error;
    runner.assert_true(scheduler.set_channel_dimming(4, DimMode::CURVE, DimPriority::CURRENT_FIRST, 0.1f, {}, &error),
                       "Switch channel 5 to curve mode");
    runner.assert_true(scheduler.is_curve_channel(4), "Channel 5 in curve mode");
    auto point = scheduler.get_schedule_points()[0];
    runner.assert_equals(expected_level, point.pwm_values[4], 0.01f, "Point converted to the level");
    runner.assert_equals(0.0f, point.current_values[4], 1e-6f, "Current cleared in curve mode");
    runner.assert_equals(50.0f, point.pwm_values[3], 1e-6f, "Other channels untouched");

    // Curve back to manual gives the same light: delivered output matches
    runner.assert_true(scheduler.set_channel_dimming(4, DimMode::MANUAL, DimPriority::CURRENT_FIRST, 0.1f, {}, &error),
                       "Switch back to manual");
    point = scheduler.get_schedule_points()[0];
    // Light from a manual PWM allows for the light each pulse loses
    float light_before = limits.effective_pwm(0.5f, 0.5f) * dimmer_before.output(0.5f, 25.0f);
    float step = limits.current_step_a;
    float landed = std::floor(point.current_values[4] / step) * step;
    float light_after = limits.effective_pwm(point.pwm_values[4] / 100.0f, landed) * dimmer_before.output(landed, 25.0f);
    runner.assert_equals(light_before, light_after, 0.01f * light_before, "Round trip keeps the light");
    runner.assert_true(point.current_values[4] > 1.0f - 2 * step && point.current_values[4] <= 1.0f,
                       "Back in manual mode the current is the channel maximum");

    // A typical manual point (PWM at the maximum current) comes back as itself
    scheduler.set_schedule_point(700, std::vector<float>(8, 15.5f), std::vector<float>(8, 1.0f));
    scheduler.set_channel_dimming(4, DimMode::CURVE, DimPriority::CURRENT_FIRST, 0.1f, {}, &error);
    scheduler.set_channel_dimming(4, DimMode::MANUAL, DimPriority::CURRENT_FIRST, 0.1f, {}, &error);
    for (const auto& p : scheduler.get_schedule_points()) {
        if (p.time_minutes == 700) {
            runner.assert_equals(15.5f, p.pwm_values[4], 0.2f, "PWM restored after a round trip");
            runner.assert_equals(1.0f, p.current_values[4], 2 * step, "Current restored after a round trip");
        }
    }

    // Bad settings change nothing
    runner.assert_false(scheduler.set_channel_dimming(4, DimMode::CURVE, DimPriority::CURRENT_FIRST, 0.01f, {}, &error),
                        "Floor below 50 mA rejected");
    runner.assert_false(scheduler.set_channel_dimming(4, DimMode::CURVE, DimPriority::CURRENT_FIRST, 0.1f,
                                                      {{"no_such_led", 3}}, &error), "Unknown LED rejected");
    runner.assert_false(scheduler.is_curve_channel(4), "Still manual after rejected changes");
    LEDScheduler small(4);
    runner.assert_true(small.set_channel_dimming(0, DimMode::CURVE, DimPriority::CURRENT_FIRST, 0.1f, {}, &error),
                       "A board without an LED map dims on the standard LED");
    runner.assert_true(small.set_channel_dimming(0, DimMode::CURVE, DimPriority::PWM_FIRST, 0.1f,
                                                 {{"luxeon_c_royal_blue", 6}}, &error), "Explicit LEDs work");
}

void test_channel_dimming_json(TestRunner& runner) {
    runner.start_suite("Channel Dimming JSON Tests");
    using ledbrick::DimMode;
    using ledbrick::DimPriority;

    LEDScheduler scheduler(8);
    scheduler.set_schedule_point(600, std::vector<float>(8, 40.0f), std::vector<float>(8, 0.5f));
    size_t manual_size = scheduler.export_json_minified().size();

    std::string error;
    scheduler.set_channel_dimming(1, DimMode::CURVE, DimPriority::PWM_FIRST, 0.2f, {}, &error);
    scheduler.set_channel_dimming(6, DimMode::CURVE, DimPriority::CURRENT_FIRST, 0.1f,
                                  {{"luxeon_c_white_5900k", 5}, {"luxeon_c_royal_blue", 4}}, &error);

    // Saved copy: only the changed channels carry dimming settings
    std::string saved = scheduler.export_json_minified();
    runner.assert_true(saved.find("\"dimming\"") != std::string::npos, "Saved copy has dimming settings");
    runner.assert_true(saved.size() < manual_size + 400, "Saved copy stays small");
    runner.assert_true(saved.find("leds_default") == std::string::npos, "Saved copy leaves out defaults");

    // Full export: every channel, with the LEDs in use
    std::string full = scheduler.export_json();
    size_t count = 0;
    for (size_t pos = full.find("\"leds_default\""); pos != std::string::npos; pos = full.find("\"leds_default\"", pos + 1)) count++;
    runner.assert_equals(8, static_cast<int>(count), "Full export describes every channel");

    // Both forms load back to the same settings
    for (const std::string& json : {saved, full}) {
        LEDScheduler loaded(8);
        runner.assert_true(loaded.import_json(json), "Export imports");
        auto ch2 = loaded.get_channel_config(1);
        auto ch7 = loaded.get_channel_config(6);
        runner.assert_true(ch2.dim_mode == DimMode::CURVE && ch2.dim_priority == DimPriority::PWM_FIRST, "Channel 2 settings kept");
        runner.assert_equals(0.2f, ch2.floor_current, 1e-4f, "Channel 2 floor kept");
        runner.assert_true(ch2.leds.empty(), "Channel 2 still on default LEDs");
        runner.assert_equals(2, static_cast<int>(ch7.leds.size()), "Channel 7 custom LEDs kept");
        runner.assert_equals(5, static_cast<int>(ch7.leds[0].count), "Channel 7 LED count kept");
        runner.assert_false(loaded.is_curve_channel(0), "Channel 1 still manual");
    }

    // A client that predates dimming leaves it out: settings are kept
    std::string old_client = "{\"num_channels\":8,\"channel_configs\":[{\"rgb_hex\":\"#112233\",\"max_current\":1,\"name\":\"a\"},"
                             "{\"rgb_hex\":\"#112233\",\"max_current\":1,\"name\":\"b\"}],"
                             "\"schedule_points\":[{\"time_minutes\":600,\"pwm_values\":[1,2,3,4,5,6,7,8],"
                             "\"current_values\":[0,0,0,0,0,0,0,0]}]}";
    runner.assert_true(scheduler.import_json(old_client), "Import without dimming");
    runner.assert_true(scheduler.is_curve_channel(1), "Curve mode kept when the field is missing");
    runner.assert_true(scheduler.get_channel_config(1).name == "b", "Other fields still imported");

    // Invalid settings reject the whole import
    LEDScheduler strict(8);
    strict.set_schedule_point(600, std::vector<float>(8, 10.0f), std::vector<float>(8, 0.5f));
    auto bad = [&](const std::string& dimming) {
        return "{\"num_channels\":8,\"channel_configs\":[{\"rgb_hex\":\"#fff\",\"max_current\":1,\"name\":\"a\","
               "\"dimming\":" + dimming + "}],\"schedule_points\":[]}";
    };
    runner.assert_false(strict.import_json(bad("{\"mode\":\"dimmer\"}")), "Unknown mode rejected");
    runner.assert_false(strict.import_json(bad("{\"mode\":\"curve\",\"priority\":\"fast\"}")), "Unknown priority rejected");
    runner.assert_false(strict.import_json(bad("{\"mode\":\"curve\",\"floor_current\":5}")), "Floor out of range rejected");
    runner.assert_false(strict.import_json(bad("{\"mode\":\"curve\",\"leds\":[{\"model\":\"x\",\"count\":2}]}")), "Unknown LED rejected");
    runner.assert_false(strict.import_json(bad("{\"mode\":\"curve\",\"leds\":[{\"model\":\"luxeon_c_blue\",\"count\":1.5}]}")), "Fractional count rejected");
    runner.assert_false(strict.import_json(bad("\"curve\"")), "Non-object rejected");
    runner.assert_equals(static_cast<size_t>(1), strict.get_schedule_points().size(), "Schedule untouched after rejects");
}

size_t count_of(const std::string& text, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) count++;
    return count;
}

void test_custom_led_models(TestRunner& runner) {
    runner.start_suite("Custom LED Model Tests");
    using ledbrick::DimMode;
    using ledbrick::DimPriority;

    const std::string red =
        R"({"id":"my_red","name":"My deep red","test_current":0.35,"max_current":0.7,)"
        R"("output_vs_current":[[0.1,0.29],[0.35,1.0],[0.7,1.94]],)"
        R"("output_vs_temp":[[25,1.1],[85,1.0]],"curve_temp":25,"rth":2.8,)"
        R"("vf_vs_current":[[0.1,1.9],[0.7,2.2]]})";
    const std::string red_list = R"({"led_models":[)" + red + "]}";
    const std::string points =
        R"("schedule_points":[{"time_minutes":600,"pwm_values":[10,10,10,10,10,10,10,10],)"
        R"("current_values":[0.5,0.5,0,0.5,0.5,0.5,0.5,0.5]}])";
    // Channel 3 runs on the custom model next to four built-in whites
    auto settings = [&](const std::string& extra, const std::string& ch3_leds) {
        return R"({"num_channels":8,)" + extra +
               R"("channel_configs":[{"rgb_hex":"#ffffff","max_current":1,"name":"a"},)"
               R"({"rgb_hex":"#ffffff","max_current":1,"name":"b"},)"
               R"({"rgb_hex":"#ff8800","max_current":2,"name":"WW","dimming":{"mode":"curve")" + ch3_leds + "}}]," +
               points + "}";
    };
    const std::string mixed_leds = R"(,"leds":[{"model":"my_red","count":4},{"model":"luxeon_c_white_3900k","count":4}])";

    LEDScheduler scheduler(8);
    std::string error;
    runner.assert_true(scheduler.import_led_models_json(red_list, &error), "Custom model posted " + error);
    runner.assert_true(scheduler.import_json(settings("", mixed_leds), &error), "A channel uses it " + error);
    runner.assert_equals(static_cast<size_t>(1), scheduler.get_custom_led_models().size(), "One custom model");
    const ledbrick::LedModel* model = scheduler.find_led_model("my_red");
    runner.assert_true(model != nullptr && model->name == "My deep red", "Custom model found by id");
    runner.assert_true(model != nullptr && model->vf_vs_current.size() == 2 && model->output_vs_temp.size() == 2,
                       "All curves read");
    runner.assert_equals(25.0f, model ? model->curve_temp_c : 0.0f, 1e-6f, "Curve temperature read");
    runner.assert_true(scheduler.is_curve_channel(2) && scheduler.channel_dimmer(2).valid(), "Channel 3 dims on it");
    ledbrick::DriveLimits limits;
    limits.max_current_a = 2.0f;
    runner.assert_equals(0.7f, scheduler.channel_dimmer(2).max_current(limits), 1e-6f,
                         "Custom model's max current caps the channel");

    // The schedule's exports leave the models out; they have their own document
    std::string saved = scheduler.export_json_minified();
    std::string full = scheduler.export_json();
    runner.assert_true(saved.find("led_models") == std::string::npos && saved.find("output_vs") == std::string::npos,
                       "Saved schedule has no models");
    runner.assert_true(full.find("led_models") == std::string::npos && full.find("output_vs") == std::string::npos,
                       "Full schedule export has no models");
    std::string models_saved = scheduler.export_led_models_json(true);
    runner.assert_true(models_saved.find("\"led_models\"") == 1 && models_saved.find("\"custom\"") == std::string::npos,
                       "Models document is the posted form");

    // Boot order: models, then the schedule
    for (const std::string& json : {saved, full}) {
        LEDScheduler loaded(8);
        runner.assert_true(loaded.import_led_models_json(models_saved, &error), "Saved models load " + error);
        runner.assert_true(loaded.import_json(json, &error), "Schedule loads after them " + error);
        const ledbrick::LedModel* again = loaded.find_led_model("my_red");
        bool same = again != nullptr && model != nullptr &&
                    again->output_vs_current.size() == model->output_vs_current.size();
        for (size_t i = 0; same && i < model->output_vs_current.size(); i++) {
            same = std::fabs(again->output_vs_current[i].x - model->output_vs_current[i].x) < 1e-4f &&
                   std::fabs(again->output_vs_current[i].y - model->output_vs_current[i].y) < 1e-4f;
        }
        runner.assert_true(same, "Current curve survives the round trip");
        runner.assert_true(again != nullptr && std::fabs(again->rth_c_per_w - 2.8f) < 1e-4f, "Rth survives");
        runner.assert_true(loaded.is_curve_channel(2) && loaded.channel_dimmer(2).valid(), "Channel 3 still on it");
    }

    // A schedule import ignores led_models
    LEDScheduler ignoring(8);
    runner.assert_true(ignoring.import_json(settings(R"("led_models":[)" + red + "],", ""), &error),
                       "Schedule with a led_models key imports " + error);
    runner.assert_true(ignoring.get_custom_led_models().empty(), "Its models are ignored");

    // Without the models, a schedule naming them is refused, unless it is the saved copy
    LEDScheduler missing(8);
    error.clear();
    runner.assert_false(missing.import_json(saved, &error), "Schedule naming an unknown model refused");
    runner.assert_true(error.find("channel 3") != std::string::npos, "Error names the channel: " + error);
    runner.assert_true(missing.import_json(saved, &error, true), "Saved copy loads without its models " + error);
    runner.assert_true(missing.is_curve_channel(2) && missing.get_channel_config(2).leds.size() == 2,
                       "Channel keeps its mode and LED names");
    runner.assert_false(missing.channels_have_models(&error), "Missing model reported");
    runner.assert_true(error.find("my_red") != std::string::npos, "Report names the model: " + error);
    runner.assert_true(missing.channel_dimmer(2).valid(), "Channel dims, the lost model as a standard LED");
    LEDScheduler lost(8);
    lost.import_json(settings("", R"(,"leds":[{"model":"my_red","count":4}])"), nullptr, true);
    runner.assert_true(lost.channel_dimmer(2).valid(), "With none known the channel uses the standard LED");
    ledbrick::Drive lost_drive = lost.channel_dimmer(2).drive_for_level(0.5f, limits, 25.0f);
    ledbrick::ChannelDimmer standard_dimmer({{ledbrick::STANDARD_LED_MODEL, 4}}, DimPriority::CURRENT_FIRST, 0.1f);
    ledbrick::Drive standard_drive = standard_dimmer.drive_for_level(0.5f, limits, 25.0f);
    runner.assert_true(lost_drive.current_a > 0.0f && std::fabs(lost_drive.current_a - standard_drive.current_a) < 1e-6f &&
                       std::fabs(lost_drive.pwm - standard_drive.pwm) < 1e-6f, "It lights as the standard LED would");
    runner.assert_true(missing.import_led_models_json(R"({"led_models":[]})", &error),
                       "Other model changes still work while a channel waits " + error);
    runner.assert_true(missing.import_led_models_json(red_list, &error), "Posting the model back " + error);
    runner.assert_true(missing.channels_have_models(&error), "Channel whole again");

    // Dropping a model a channel uses fails and changes nothing
    error.clear();
    runner.assert_false(scheduler.import_led_models_json(R"({"led_models":[]})", &error),
                        "Dropping an in-use model rejected");
    runner.assert_true(error.find("channel 3") != std::string::npos, "Error names the channel: " + error);
    runner.assert_false(scheduler.set_custom_led_models({}, &error), "set_custom_led_models refuses too");
    runner.assert_equals(static_cast<size_t>(1), scheduler.get_custom_led_models().size(), "Model still there");
    runner.assert_true(scheduler.import_json(settings("", ""), &error), "Channel back on its default LEDs " + error);
    runner.assert_true(scheduler.set_custom_led_models({}, &error), "Then the model can go " + error);

    // A custom model with a built-in's id replaces it, for channels on their default LEDs too
    float before = scheduler.channel_dimmer(2).output(0.35f, 25.0f);
    const ledbrick::LedModel* white = scheduler.find_led_model("luxeon_c_white_3900k");
    ledbrick::LedModel brighter = *white;
    for (auto& p : brighter.output_vs_current) p.y *= 2.0f;
    runner.assert_true(scheduler.set_custom_led_models({brighter}, &error), "Replace a built-in " + error);
    float after = scheduler.channel_dimmer(2).output(0.35f, 25.0f);
    runner.assert_true(after > before * 1.4f, "WW uses the replacement curve");
    std::string listing = scheduler.export_led_models_json();
    runner.assert_equals(static_cast<size_t>(1), count_of(listing, "\"id\":\"luxeon_c_white_3900k\""),
                         "Listing shows the replacement once");
    runner.assert_equals(static_cast<size_t>(1), count_of(listing, "\"custom\":true"), "Listing marks it custom");
    runner.assert_equals(ledbrick::builtin_led_models().size(), count_of(listing, "\"id\""),
                         "Listing has one entry per id");

    // The listing's entries can be posted back as custom models
    std::string listed_model = listing.substr(listing.find('{', 1));
    listed_model = listed_model.substr(0, listed_model.find("},{") + 1);
    LEDScheduler copy(8);
    runner.assert_true(copy.import_led_models_json(R"({"led_models":[)" + listed_model + "]}", &error),
                       "A listed model posts back " + error);

    // Bad model lists
    auto rejects = [&](const std::string& body, const std::string& expect, const std::string& what) {
        LEDScheduler target(8);
        std::string why;
        bool ok = target.import_led_models_json(body, &why);
        runner.assert_true(!ok && why.find(expect) != std::string::npos, what + " rejected: " + why);
    };
    auto with = [&](const std::string& from, const std::string& to) {
        std::string m = red;
        m.replace(m.find(from), from.size(), to);
        return R"({"led_models":[)" + m + "]}";
    };
    rejects("{nope", "invalid JSON", "Bad JSON");
    rejects("{}", "must be a list", "Missing list");
    rejects(R"({"led_models":[)" + red + "," + red + "]}", "twice", "Repeated id");
    std::string nine;
    for (int i = 0; i < 9; i++) {
        std::string m = red;
        m.replace(m.find("my_red"), 6, "red_" + std::to_string(i));
        nine += (i ? "," : "") + m;
    }
    rejects(R"({"led_models":[)" + nine + "]}", "at most 8", "Nine models");
    rejects(with(R"("id":"my_red")", R"("id":"My Red")"), "a-z", "Id with capitals");
    rejects(with(R"("max_current":0.7,)", ""), "max_current is required", "Missing max current");
    rejects(with(R"("max_current":0.7)", R"("max_current":"0.7")"), "must be a number", "Text max current");
    rejects(with(R"([0.35,1.0])", R"([0.35,1.0,2])"), "[x, y]", "Three-number point");
    rejects(with(R"([0.35,1.0])", R"([0.35,0.1])"), "must not fall", "Falling output");
    rejects(with(R"("curve_temp":25,)", ""), "curve_temp is required", "Temperature curve without its temperature");
    std::string many = "[";
    for (int k = 0; k < 33; k++) {
        many += (k ? ",[" : "[") + std::to_string(0.02 * (k + 1)) + "," + std::to_string(0.03 * (k + 1)) + "]";
    }
    many += "]";
    rejects(with(R"([[0.1,0.29],[0.35,1.0],[0.7,1.94]])", many), "over 32", "33-point curve");
    LEDScheduler unchanged(8);
    unchanged.import_led_models_json("{nope", nullptr);
    runner.assert_true(unchanged.get_custom_led_models().empty(), "Failed post changes nothing");

    // The size limit for the saved record is checked before anything changes
    LEDScheduler limited(8);
    error.clear();
    runner.assert_false(limited.import_led_models_json(red_list, &error, 100), "Set over the size limit refused");
    runner.assert_true(error.find("bytes to save") != std::string::npos && limited.get_custom_led_models().empty(),
                       "Refused before applying: " + error);
    runner.assert_true(limited.import_led_models_json(red_list, &error, 12287), "Under the limit accepted " + error);

    // Eight models with 24-point curves fit the 12 KB record
    std::vector<ledbrick::LedModel> eight;
    for (int i = 0; i < 8; i++) {
        ledbrick::LedModel m;
        m.id = "custom_part_" + std::to_string(i);
        m.name = "Custom part number " + std::to_string(i);
        m.test_current_a = 0.35f;
        m.max_current_a = 1.4f;
        m.curve_temp_c = 25.0f;
        m.rth_c_per_w = 2.5f;
        for (int k = 0; k < 24; k++) {
            m.output_vs_current.push_back({0.1f + 0.0567f * k, 0.3123f + 0.1717f * k});
            m.output_vs_temp.push_back({-20.0f + 7.33f * k, 1.123f - 0.0217f * k});
            m.vf_vs_current.push_back({0.1f + 0.0567f * k, 2.612f + 0.0213f * k});
        }
        eight.push_back(m);
    }
    LEDScheduler roomy(8);
    runner.assert_true(roomy.set_custom_led_models(eight, &error), "Eight 24-point models accepted " + error);
    size_t eight_size = roomy.export_led_models_json(true).size();
    runner.assert_true(eight_size < 12287, "They fit the 12 KB record: " + std::to_string(eight_size) + " bytes");
}

void test_json_export_size(TestRunner& runner) {
    runner.start_suite("JSON Export Size Tests");

    // The firmware stores the minified export in an 8 KB flash slot
    LEDScheduler scheduler(8);
    for (int p = 0; p < 30; p++) {
        std::vector<float> pwm, current;
        for (int c = 0; c < 8; c++) {
            pwm.push_back(0.65f * static_cast<float>(p + c + 1));
            current.push_back(0.137f * static_cast<float>((p + c) % 14));
        }
        scheduler.set_schedule_point(static_cast<uint16_t>(p * 45), pwm, current);
    }
    runner.assert_true(scheduler.export_json_minified().size() < 7000, "30 eight-channel points fit in under 7000 bytes");

    LEDScheduler round_trip(8);
    runner.assert_true(round_trip.import_json(scheduler.export_json_minified()), "Minified export imports");
    runner.assert_equals(30, static_cast<int>(round_trip.get_schedule_size()), "All points survive the round trip");
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

    test_json_import_validation(runner);
    results.add_suite_results(runner);

    test_json_export_size(runner);
    results.add_suite_results(runner);

    test_exact_point_time(runner);
    results.add_suite_results(runner);

    test_interpolation_to_the_second(runner);
    results.add_suite_results(runner);

    test_channel_dimming(runner);
    results.add_suite_results(runner);

    test_channel_dimming_json(runner);
    results.add_suite_results(runner);

    test_custom_led_models(runner);
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