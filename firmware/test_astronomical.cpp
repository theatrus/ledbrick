#include "components/ledbrick_scheduler/astronomical_calculator.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>

// Simple test framework
class TestRunner {
private:
    int tests_run = 0;
    int tests_passed = 0;
    
public:
    void assert_equals(double expected, double actual, double tolerance, const std::string& test_name) {
        tests_run++;
        if (std::abs(expected - actual) <= tolerance) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED: expected " << expected 
                      << ", got " << actual << " (diff: " << std::abs(expected - actual) << ")" << std::endl;
        }
    }
    
    void assert_true(bool condition, const std::string& test_name) {
        tests_run++;
        if (condition) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED" << std::endl;
        }
    }
    
    void print_summary() {
        std::cout << "\n=== TEST SUMMARY ===" << std::endl;
        std::cout << "Tests run: " << tests_run << std::endl;
        std::cout << "Tests passed: " << tests_passed << std::endl;
        std::cout << "Tests failed: " << (tests_run - tests_passed) << std::endl;
        std::cout << "Success rate: " << std::fixed << std::setprecision(1) 
                  << (100.0 * tests_passed / tests_run) << "%" << std::endl;
    }
};

void test_julian_day_calculation() {
    std::cout << "\n=== Julian Day Tests ===" << std::endl;
    TestRunner runner;
    AstronomicalCalculator calc;
    calc.set_timezone_offset(0.0);  // Use UTC for this test
    
    // Test known Julian Day values
    // January 1, 2000, 12:00 UTC should be JD 2451545.0
    AstronomicalCalculator::DateTime dt_j2000(2000, 1, 1, 12, 0, 0);
    double jd_j2000 = calc.calculate_julian_day(dt_j2000);
    runner.assert_equals(2451545.0, jd_j2000, 0.1, "J2000 epoch calculation");
    
    // January 8, 2025, 00:00 UTC
    AstronomicalCalculator::DateTime dt_today(2025, 1, 8, 0, 0, 0);
    double jd_today = calc.calculate_julian_day(dt_today);
    runner.assert_true(jd_today > 2460000 && jd_today < 2470000, "Current date JD range check");
    
    runner.print_summary();
}

void test_moon_phase() {
    std::cout << "\n=== Moon Phase Tests ===" << std::endl;
    TestRunner runner;
    AstronomicalCalculator calc;
    
    // Test moon phase calculation for various dates
    AstronomicalCalculator::DateTime dt1(2025, 1, 8, 12, 0, 0);
    float phase1 = calc.get_moon_phase(dt1);
    runner.assert_true(phase1 >= 0.0 && phase1 <= 1.0, "Moon phase in valid range");
    
    std::cout << "Moon phase on 2025-01-08: " << std::fixed << std::setprecision(3) << phase1 << std::endl;
    
    runner.print_summary();
}

void test_sun_position() {
    std::cout << "\n=== Sun Position Tests ===" << std::endl;
    TestRunner runner;
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    calc.set_timezone_offset(-8.0);  // PST = UTC-8
    
    // Test sun position at solar noon
    AstronomicalCalculator::DateTime noon(2025, 1, 8, 12, 0, 0);  // Local noon
    auto sun_pos = calc.calculate_sun_position(noon);
    
    std::cout << "Sun position at noon SF: altitude=" << std::fixed << std::setprecision(1) 
              << sun_pos.altitude << "°, azimuth=" << sun_pos.azimuth << "°" << std::endl;
    
    runner.assert_true(sun_pos.altitude > 0, "Sun above horizon at noon in January SF");
    runner.assert_true(sun_pos.azimuth > 150 && sun_pos.azimuth < 210, "Sun roughly south at noon");
    
    runner.print_summary();
}

void test_sun_intensity() {
    std::cout << "\n=== Sun Intensity Tests ===" << std::endl;
    TestRunner runner;
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    calc.set_timezone_offset(-8.0);  // PST = UTC-8
    
    // Test sun intensity at different times
    AstronomicalCalculator::DateTime midnight(2025, 1, 8, 0, 0, 0);
    AstronomicalCalculator::DateTime noon(2025, 1, 8, 12, 0, 0);
    AstronomicalCalculator::DateTime sunrise(2025, 1, 8, 7, 0, 0);  // Approximate
    
    float intensity_midnight = calc.get_sun_intensity(midnight);
    float intensity_noon = calc.get_sun_intensity(noon);
    float intensity_sunrise = calc.get_sun_intensity(sunrise);
    
    std::cout << "Sun intensity - Midnight: " << intensity_midnight 
              << ", Sunrise: " << intensity_sunrise 
              << ", Noon: " << intensity_noon << std::endl;
    
    runner.assert_equals(0.0, intensity_midnight, 0.1, "No sun intensity at midnight");
    runner.assert_true(intensity_noon >= 0.5, "High sun intensity at noon");
    runner.assert_true(intensity_sunrise > 0.0 && intensity_sunrise < intensity_noon, "Sunrise intensity between midnight and noon");
    
    runner.print_summary();
}

void test_time_projection() {
    std::cout << "\n=== Time Projection Tests ===" << std::endl;
    TestRunner runner;
    
    // Test Tahiti projection scenario
    AstronomicalCalculator calc_tahiti(-17.5, -149.4);  // Tahiti coordinates
    calc_tahiti.set_projection_settings(true, 0, 0);    // Enable projection, no shift
    
    AstronomicalCalculator::DateTime test_time(2025, 1, 8, 6, 15, 0);  // 6:15 AM
    
    float normal_intensity = calc_tahiti.get_sun_intensity(test_time);
    float projected_intensity = calc_tahiti.get_projected_sun_intensity(test_time);
    
    std::cout << "Tahiti sun intensity - Normal: " << normal_intensity 
              << ", Projected: " << projected_intensity << std::endl;
    
    runner.assert_true(normal_intensity >= 0.0 && normal_intensity <= 1.0, "Normal intensity valid range");
    runner.assert_true(projected_intensity >= 0.0 && projected_intensity <= 1.0, "Projected intensity valid range");
    
    // Test with time shift
    calc_tahiti.set_projection_settings(true, 2, 30);  // +2h 30m shift
    float shifted_intensity = calc_tahiti.get_projected_sun_intensity(test_time);
    std::cout << "Tahiti sun intensity with +2h30m shift: " << shifted_intensity << std::endl;
    
    runner.print_summary();
}

void test_moon_rise_set() {
    std::cout << "\n=== Moon Rise/Set Tests ===" << std::endl;
    TestRunner runner;
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    calc.set_timezone_offset(-8.0);  // PST = UTC-8
    
    AstronomicalCalculator::DateTime test_date(2025, 1, 8, 12, 0, 0);
    auto moon_times = calc.get_moon_rise_set_times(test_date);
    
    std::cout << "Moon times - Rise: " << (moon_times.rise_valid ? "valid" : "none");
    if (moon_times.rise_valid) {
        std::cout << " at " << std::setfill('0') << std::setw(2) << (moon_times.rise_minutes / 60)
                  << ":" << std::setfill('0') << std::setw(2) << (moon_times.rise_minutes % 60);
    }
    std::cout << ", Set: " << (moon_times.set_valid ? "valid" : "none");
    if (moon_times.set_valid) {
        std::cout << " at " << std::setfill('0') << std::setw(2) << (moon_times.set_minutes / 60)
                  << ":" << std::setfill('0') << std::setw(2) << (moon_times.set_minutes % 60);
    }
    std::cout << std::endl;
    
    // At least one should be valid (rise or set) for most locations and dates
    runner.assert_true(moon_times.rise_valid || moon_times.set_valid, "At least one moon time calculated");
    
    if (moon_times.rise_valid) {
        runner.assert_true(moon_times.rise_minutes < 1440, "Rise time within 24 hours");
    }
    if (moon_times.set_valid) {
        runner.assert_true(moon_times.set_minutes < 1440, "Set time within 24 hours");
    }
    
    runner.print_summary();
}

void test_sun_rise_set() {
    std::cout << "\n=== Sun Rise/Set Tests ===" << std::endl;
    TestRunner runner;
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    calc.set_timezone_offset(-8.0);  // PST = UTC-8
    
    AstronomicalCalculator::DateTime test_date(2025, 1, 8, 12, 0, 0);
    auto sun_times = calc.get_sun_rise_set_times(test_date);
    
    std::cout << "Sun times - Rise: " << (sun_times.rise_valid ? "valid" : "none");
    if (sun_times.rise_valid) {
        std::cout << " at " << std::setfill('0') << std::setw(2) << (sun_times.rise_minutes / 60)
                  << ":" << std::setfill('0') << std::setw(2) << (sun_times.rise_minutes % 60);
    }
    std::cout << ", Set: " << (sun_times.set_valid ? "valid" : "none");
    if (sun_times.set_valid) {
        std::cout << " at " << std::setfill('0') << std::setw(2) << (sun_times.set_minutes / 60)
                  << ":" << std::setfill('0') << std::setw(2) << (sun_times.set_minutes % 60);
    }
    std::cout << std::endl;
    
    // Sun should have both rise and set for most locations and dates
    runner.assert_true(sun_times.rise_valid && sun_times.set_valid, "Both sun rise and set calculated");
    
    if (sun_times.rise_valid) {
        runner.assert_true(sun_times.rise_minutes < 1440, "Sunrise time within 24 hours");
        runner.assert_true(sun_times.rise_minutes >= 420 && sun_times.rise_minutes <= 480, "Sunrise between 7:00-8:00 AM for SF in January");
    }
    if (sun_times.set_valid) {
        runner.assert_true(sun_times.set_minutes < 1440, "Sunset time within 24 hours");
        runner.assert_true(sun_times.set_minutes >= 1020 && sun_times.set_minutes <= 1080, "Sunset between 5:00-6:00 PM for SF in January");
    }
    
    runner.print_summary();
}

void run_comprehensive_test() {
    std::cout << "=== COMPREHENSIVE ASTRONOMICAL CALCULATOR TESTS ===" << std::endl;
    std::cout << "Testing date: January 8, 2025" << std::endl;
    
    test_julian_day_calculation();
    test_moon_phase();
    test_sun_position();
    test_sun_intensity();
    test_time_projection();
    test_moon_rise_set();
    test_sun_rise_set();
    
    std::cout << "\n=== ALL TESTS COMPLETED ===" << std::endl;
}

int main() {
    run_comprehensive_test();
    return 0;
}