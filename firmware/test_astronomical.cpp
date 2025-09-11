#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"
#include "test_framework.h"

using namespace std;

void test_julian_day_calculation() {
    TEST_SUITE("Julian Day Tests");
    
    AstronomicalCalculator calc(0, 0);
    
    // Test J2000 epoch (January 1, 2000, 12:00 UTC)
    AstronomicalCalculator::DateTime j2000(2000, 1, 1, 12, 0, 0);
    double jd = calc.calculate_julian_day(j2000);
    TEST_ASSERT_EQUAL_FLOAT(2451545.0, jd, 0.001, "J2000 epoch calculation");
    
    // Test current date is in reasonable range
    AstronomicalCalculator::DateTime now(2025, 1, 8, 12, 0, 0);
    jd = calc.calculate_julian_day(now);
    TEST_ASSERT(jd > 2460000 && jd < 2470000, 
        "Current date JD range check (expected range: 2460000-2470000, actual: " + to_string(jd) + ")");
}

void test_moon_phase() {
    TEST_SUITE("Moon Phase Tests");
    
    AstronomicalCalculator calc(0, 0);
    
    // Test phase is in valid range
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    float phase = calc.get_moon_phase(dt);
    TEST_ASSERT(phase >= 0.0f && phase <= 1.0f, 
        "Moon phase in valid range (expected: 0.0-1.0, actual: " + to_string(phase) + ")");
    
    cout << "Moon phase on 2025-01-08: " << fixed << setprecision(3) << phase << endl;
}

void test_sun_position() {
    TEST_SUITE("Sun Position Tests");
    
    // San Francisco coordinates
    AstronomicalCalculator calc(37.7749, -122.4194);
    
    // Test sun position at noon
    AstronomicalCalculator::DateTime noon(2025, 1, 8, 12, 0, 0);
    auto pos = calc.calculate_sun_position(noon);
    
    cout << "Sun position at noon SF: altitude=" << fixed << setprecision(1) 
         << pos.altitude << "°, azimuth=" << pos.azimuth << "°" << endl;
    
    TEST_ASSERT(pos.altitude > 0, "Sun above horizon at noon in January SF (expected: > 0, actual: " 
        + to_string(pos.altitude) + ")");
    TEST_ASSERT(pos.azimuth > 150 && pos.azimuth < 210, 
        "Sun roughly south at noon (expected range: 150-210, actual: " + to_string(pos.azimuth) + ")");
}

void test_sun_intensity() {
    TEST_SUITE("Sun Intensity Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194);
    
    // Test at different times
    AstronomicalCalculator::DateTime midnight(2025, 1, 8, 0, 0, 0);
    AstronomicalCalculator::DateTime sunrise(2025, 1, 8, 7, 30, 0);
    AstronomicalCalculator::DateTime noon(2025, 1, 8, 12, 0, 0);
    
    float intensity_midnight = calc.get_sun_intensity(midnight);
    float intensity_sunrise = calc.get_sun_intensity(sunrise);
    float intensity_noon = calc.get_sun_intensity(noon);
    
    cout << "Sun intensity - Midnight: " << intensity_midnight 
         << ", Sunrise: " << intensity_sunrise 
         << ", Noon: " << intensity_noon << endl;
    
    TEST_ASSERT_EQUAL_FLOAT(0.0f, intensity_midnight, 0.001f, "No sun intensity at midnight");
    TEST_ASSERT(intensity_noon >= 0.49f, "High sun intensity at noon (expected: >= 0.49, actual: " 
        + to_string(intensity_noon) + ")");
    TEST_ASSERT(intensity_sunrise >= 0.0f && intensity_sunrise <= intensity_noon,
        "Sunrise intensity between midnight and noon (expected range: 0.0-" 
        + to_string(intensity_noon) + ", actual: " + to_string(intensity_sunrise) + ")");
}

void test_time_projection() {
    TEST_SUITE("Time Projection Tests");
    
    // Test Tahiti location projecting Singapore sunrise to Pacific time
    AstronomicalCalculator calc(-17.5, -149.5);  // Tahiti
    
    // Test without projection
    calc.set_projection_settings(false, 0, 0);
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 10, 0, 0);  // 10 AM local
    float normal_intensity = calc.get_sun_intensity(dt);
    
    // Enable projection to shift Singapore time to appear at Pacific time
    calc.set_projection_settings(true, 8, 0);  // +8 hours shift
    float projected_intensity = calc.get_projected_sun_intensity(dt);
    
    cout << "Tahiti sun intensity - Normal: " << normal_intensity 
         << ", Projected: " << projected_intensity << endl;
    
    TEST_ASSERT(normal_intensity >= 0.0f && normal_intensity <= 1.0f,
        "Normal intensity valid range (expected: 0.0-1.0, actual: " + to_string(normal_intensity) + ")");
    TEST_ASSERT(projected_intensity >= 0.0f && projected_intensity <= 1.0f,
        "Projected intensity valid range (expected: 0.0-1.0, actual: " + to_string(projected_intensity) + ")");
    
    // Test with different time shift
    calc.set_projection_settings(true, 2, 30);  // +2h30m shift
    float shifted_intensity = calc.get_projected_sun_intensity(dt);
    cout << "Tahiti sun intensity with +2h30m shift: " << shifted_intensity << endl;
}

void test_moon_rise_set() {
    TEST_SUITE("Moon Rise/Set Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    auto times = calc.get_moon_rise_set_times(dt);
    
    cout << "Moon times - Rise: " << (times.rise_valid ? "valid" : "invalid");
    if (times.rise_valid) {
        cout << " at " << setfill('0') << setw(2) << times.rise_minutes / 60 
             << ":" << setw(2) << times.rise_minutes % 60;
    }
    cout << ", Set: " << (times.set_valid ? "valid" : "invalid");
    if (times.set_valid) {
        cout << " at " << setfill('0') << setw(2) << times.set_minutes / 60 
             << ":" << setw(2) << times.set_minutes % 60;
    }
    cout << endl;
    
    TEST_ASSERT(times.rise_valid || times.set_valid, "At least one moon time calculated");
    
    if (times.rise_valid) {
        TEST_ASSERT(times.rise_minutes < 1440, "Rise time within 24 hours");
    }
    if (times.set_valid) {
        TEST_ASSERT(times.set_minutes < 1440, "Set time within 24 hours");
    }
}

void test_sun_rise_set() {
    TEST_SUITE("Sun Rise/Set Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    auto times = calc.get_sun_rise_set_times(dt);
    
    cout << "Sun times - Rise: " << (times.rise_valid ? "valid" : "invalid");
    if (times.rise_valid) {
        cout << " at " << setfill('0') << setw(2) << times.rise_minutes / 60 
             << ":" << setw(2) << times.rise_minutes % 60;
    }
    cout << ", Set: " << (times.set_valid ? "valid" : "invalid");
    if (times.set_valid) {
        cout << " at " << setfill('0') << setw(2) << times.set_minutes / 60 
             << ":" << setw(2) << times.set_minutes % 60;
    }
    cout << endl;
    
    TEST_ASSERT(times.rise_valid && times.set_valid, "Both sun rise and set calculated");
    TEST_ASSERT(times.rise_minutes < 1440, "Sunrise time within 24 hours");
    TEST_ASSERT(times.rise_minutes >= 420 && times.rise_minutes <= 480, 
        "Sunrise between 7:00-8:00 AM for SF in January (expected range: 420-480 min, actual: " 
        + to_string(times.rise_minutes) + ")");
    TEST_ASSERT(times.set_minutes < 1440, "Sunset time within 24 hours");
    TEST_ASSERT(times.set_minutes >= 1020 && times.set_minutes <= 1080,
        "Sunset between 5:00-6:00 PM for SF in January (expected range: 1020-1080 min, actual: " 
        + to_string(times.set_minutes) + ")");
}

void test_singapore_pacific_offset() {
    TEST_SUITE("Singapore Pacific Offset Tests");
    
    // Singapore location
    AstronomicalCalculator calc(1.3521, 103.8198);
    
    // Enable projection to shift Singapore sunrise to Pacific morning
    calc.set_projection_settings(true, -16, 0);  // UTC+8 to UTC-8 = -16 hours
    
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    auto projected = calc.get_projected_sun_rise_set_times(dt);
    
    cout << "Singapore sun times with Pacific offset - ";
    cout << "Rise: " << setfill('0') << setw(2) << projected.rise_minutes / 60 
         << ":" << setw(2) << projected.rise_minutes % 60;
    cout << " (Pacific equivalent: " << ((projected.rise_minutes + 480) % 1440) / 60 
         << ":" << setw(2) << ((projected.rise_minutes + 480) % 1440) % 60 << ")";
    cout << ", Set: " << setw(2) << projected.set_minutes / 60 
         << ":" << setw(2) << projected.set_minutes % 60;
    cout << " (Pacific equivalent: " << ((projected.set_minutes + 480) % 1440) / 60 
         << ":" << setw(2) << ((projected.set_minutes + 480) % 1440) % 60 << ")" << endl;
    
    TEST_ASSERT(projected.rise_valid, "Singapore projected sunrise calculated");
    TEST_ASSERT(projected.rise_minutes >= 90 && projected.rise_minutes <= 150,
        "Projected sunrise appears around 2 AM Singapore time (expected range: 90-150 min, actual: " 
        + to_string(projected.rise_minutes) + ")");
}

void test_negative_time_shift() {
    TEST_SUITE("Negative Time Shift Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194);  // San Francisco
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    
    // Get base times
    auto base_times = calc.get_sun_rise_set_times(dt);
    
    // Apply negative time shift
    calc.set_projection_settings(true, -4, 0);  // -4 hours
    auto shifted_times = calc.get_projected_sun_rise_set_times(dt);
    
    cout << "Actual sunrise: " << base_times.rise_minutes << " min, "
         << "Projected sunrise: " << shifted_times.rise_minutes << " min" << endl;
    cout << "Actual sunset: " << base_times.set_minutes << " min, "
         << "Projected sunset: " << shifted_times.set_minutes << " min" << endl;
    
    TEST_ASSERT(shifted_times.rise_valid && shifted_times.set_valid, 
        "Projected times are valid with negative shift");
    
    // Check that times shifted correctly (earlier by 4 hours = 240 minutes)
    int expected_rise = (base_times.rise_minutes - 240 + 1440) % 1440;
    int expected_set = (base_times.set_minutes - 240 + 1440) % 1440;
    
    TEST_ASSERT_EQUAL(expected_rise, shifted_times.rise_minutes, 
        "Projected sunrise is 4 hours earlier (expected: " + to_string(expected_rise) 
        + ", actual: " + to_string(shifted_times.rise_minutes) + ")");
    TEST_ASSERT_EQUAL(expected_set, shifted_times.set_minutes,
        "Projected sunset is 4 hours earlier (expected: " + to_string(expected_set) 
        + ", actual: " + to_string(shifted_times.set_minutes) + ")");
    
    // Test solar noon calculation
    int solar_noon = (shifted_times.rise_minutes + shifted_times.set_minutes) / 2;
    if (shifted_times.set_minutes < shifted_times.rise_minutes) {
        solar_noon = (shifted_times.rise_minutes + shifted_times.set_minutes + 1440) / 2;
        if (solar_noon >= 1440) solar_noon -= 1440;
    }
    cout << "Calculated solar noon: " << solar_noon << " min (" 
         << solar_noon / 60 << ":" << solar_noon % 60 << ")" << endl;
    
    TEST_ASSERT(solar_noon >= 480 && solar_noon <= 540, 
        "Solar noon is shifted correctly (expected around 8:23, actual: " 
        + to_string(solar_noon / 60) + ":" + to_string(solar_noon % 60) + ")");
}

int main() {
    cout << "=== COMPREHENSIVE ASTRONOMICAL CALCULATOR TESTS ===" << endl;
    cout << "Testing date: January 8, 2025" << endl << endl;
    
    test_julian_day_calculation();
    test_moon_phase();
    test_sun_position();
    test_sun_intensity();
    test_time_projection();
    test_moon_rise_set();
    test_sun_rise_set();
    test_singapore_pacific_offset();
    test_negative_time_shift();
    
    TEST_SUMMARY("Astronomical Calculator");
    
    return TEST_FAILED_COUNT > 0 ? 1 : 0;
}