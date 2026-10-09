#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <string>
#include "astronomical_calculator.h"
#include "test_framework.h"

using namespace std;

// Global test runner and results
TestRunner runner;
TestResults results;

void test_julian_day_calculation() {
    runner.start_suite("Julian Day Tests");
    
    AstronomicalCalculator calc(0, 0, 0);  // lat, lon, timezone
    
    // Test J2000 epoch (January 1, 2000, 12:00 UTC)
    AstronomicalCalculator::DateTime j2000(2000, 1, 1, 12, 0, 0);
    double jd = calc.calculate_julian_day(j2000);
    runner.assert_equals(2451545.0, jd, 0.001, "J2000 epoch calculation");
    
    // Test current date is in reasonable range
    AstronomicalCalculator::DateTime now(2025, 1, 8, 12, 0, 0);
    jd = calc.calculate_julian_day(now);
    runner.assert_true(jd > 2460000 && jd < 2470000, 
        "Current date JD range check (expected range: 2460000-2470000, actual: " + to_string(jd) + ")");
    
    results.add_suite_results(runner);
}

void test_moon_phase() {
    runner.start_suite("Moon Phase Tests");
    
    AstronomicalCalculator calc(0, 0, 0);
    
    // Test phase is in valid range
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    float phase = calc.get_moon_phase(dt);
    runner.assert_true(phase >= 0.0f && phase <= 1.0f, 
        "Moon phase in valid range (expected: 0.0-1.0, actual: " + to_string(phase) + ")");
    
    cout << "Moon phase on 2025-01-08: " << fixed << setprecision(3) << phase << endl;

    // Published lunar phases (UTC): the phase is the position in the cycle, the
    // illumination the share that is lit
    struct Event { int y, mo, d, h, mi; float phase; float lit; const char* name; };
    const Event events[] = {
        {2025, 1, 6, 23, 56, 0.25f, 0.5f, "First quarter 2025-01-06"},
        {2025, 1, 13, 22, 27, 0.5f, 1.0f, "Full moon 2025-01-13"},
        {2025, 1, 21, 20, 31, 0.75f, 0.5f, "Last quarter 2025-01-21"},
        {2025, 1, 29, 12, 36, 0.0f, 0.0f, "New moon 2025-01-29"},
    };
    for (const auto& e : events) {
        AstronomicalCalculator::DateTime when(e.y, e.mo, e.d, e.h, e.mi, 0);
        float p = calc.get_moon_phase(when);
        float off = std::fabs(p - e.phase);
        off = std::min(off, 1.0f - off);  // the cycle wraps at new moon
        runner.assert_true(off < 0.01f, std::string(e.name) + ": phase " + to_string(p));
        runner.assert_equals(e.lit, calc.get_moon_illumination(when), 0.02f, std::string(e.name) + ": share lit");
    }

    // Two days before the new moon of 2026-10-10: a thin waning crescent, not a nearly
    // full moon, though it is 0.93 of the way through the cycle
    AstronomicalCalculator::DateTime crescent(2026, 10, 8, 18, 0, 0);
    float crescent_phase = calc.get_moon_phase(crescent);
    runner.assert_true(crescent_phase > 0.9f && crescent_phase < 0.96f, "2026-10-08 late in the cycle: " + to_string(crescent_phase));
    runner.assert_true(calc.get_moon_illumination(crescent) < 0.1f,
                       "2026-10-08 under 10% lit: " + to_string(calc.get_moon_illumination(crescent)));
    runner.assert_equals(0.0f, AstronomicalCalculator::moon_illumination_from_phase(0.0f), 1e-6f, "New: none lit");
    runner.assert_equals(1.0f, AstronomicalCalculator::moon_illumination_from_phase(0.5f), 1e-6f, "Full: all lit");
    runner.assert_equals(0.5f, AstronomicalCalculator::moon_illumination_from_phase(0.75f), 1e-6f, "Last quarter: half lit");

    results.add_suite_results(runner);
}

void test_sun_position() {
    runner.start_suite("Sun Position Tests");
    
    // San Francisco coordinates
    AstronomicalCalculator calc(37.7749, -122.4194, -8);  // PST timezone
    
    // Test sun position at noon
    AstronomicalCalculator::DateTime noon(2025, 1, 8, 12, 0, 0);
    auto pos = calc.calculate_sun_position(noon);
    
    cout << "Sun position at noon SF: altitude=" << fixed << setprecision(1) 
         << pos.altitude << "°, azimuth=" << pos.azimuth << "°" << endl;
    
    runner.assert_true(pos.altitude > 0, "Sun above horizon at noon in January SF (expected: > 0, actual: " 
        + to_string(pos.altitude) + ")");
    runner.assert_true(pos.azimuth > 150 && pos.azimuth < 210, 
        "Sun roughly south at noon (expected range: 150-210, actual: " + to_string(pos.azimuth) + ")");
    
    results.add_suite_results(runner);
}

void test_sun_intensity() {
    runner.start_suite("Sun Intensity Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194, -8);
    
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
    
    runner.assert_equals(0.0f, intensity_midnight, 0.001f, "No sun intensity at midnight");
    runner.assert_true(intensity_noon >= 0.49f, "High sun intensity at noon (expected: >= 0.49, actual: " 
        + to_string(intensity_noon) + ")");
    runner.assert_true(intensity_sunrise >= 0.0f && intensity_sunrise <= intensity_noon,
        "Sunrise intensity between midnight and noon (expected range: 0.0-" 
        + to_string(intensity_noon) + ", actual: " + to_string(intensity_sunrise) + ")");
    
    results.add_suite_results(runner);
}

void test_time_projection() {
    runner.start_suite("Time Projection Tests");
    
    // Test Tahiti location projecting Singapore sunrise to Pacific time
    AstronomicalCalculator calc(-17.5, -149.5, -10);  // Tahiti timezone
    
    // Test without projection
    calc.set_projection_settings(false, 0, 0);
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 10, 0, 0);  // 10 AM local
    float normal_intensity = calc.get_sun_intensity(dt);
    
    // Enable projection to shift Singapore time to appear at Pacific time
    calc.set_projection_settings(true, 8, 0);  // +8 hours shift
    float projected_intensity = calc.get_projected_sun_intensity(dt);
    
    cout << "Tahiti sun intensity - Normal: " << normal_intensity 
         << ", Projected: " << projected_intensity << endl;
    
    runner.assert_true(normal_intensity >= 0.0f && normal_intensity <= 1.0f,
        "Normal intensity valid range (expected: 0.0-1.0, actual: " + to_string(normal_intensity) + ")");
    runner.assert_true(projected_intensity >= 0.0f && projected_intensity <= 1.0f,
        "Projected intensity valid range (expected: 0.0-1.0, actual: " + to_string(projected_intensity) + ")");
    
    // Test with different time shift
    calc.set_projection_settings(true, 2, 30);  // +2h30m shift
    float shifted_intensity = calc.get_projected_sun_intensity(dt);
    cout << "Tahiti sun intensity with +2h30m shift: " << shifted_intensity << endl;
    
    results.add_suite_results(runner);
}

void test_moon_rise_set() {
    runner.start_suite("Moon Rise/Set Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194, -8);  // San Francisco PST
    
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
    
    runner.assert_true(times.rise_valid || times.set_valid, "At least one moon time calculated");
    
    if (times.rise_valid) {
        runner.assert_true(times.rise_minutes < 1440, "Rise time within 24 hours");
    }
    if (times.set_valid) {
        runner.assert_true(times.set_minutes < 1440, "Set time within 24 hours");
    }
    
    results.add_suite_results(runner);
}

void test_sun_rise_set() {
    runner.start_suite("Sun Rise/Set Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194, -8);  // San Francisco PST
    
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
    
    runner.assert_true(times.rise_valid && times.set_valid, "Both sun rise and set calculated");
    runner.assert_true(times.rise_minutes < 1440, "Sunrise time within 24 hours");
    runner.assert_true(times.rise_minutes >= 420 && times.rise_minutes <= 480, 
        "Sunrise between 7:00-8:00 AM for SF in January (expected range: 420-480 min, actual: " 
        + to_string(times.rise_minutes) + ")");
    runner.assert_true(times.set_minutes < 1440, "Sunset time within 24 hours");
    runner.assert_true(times.set_minutes >= 1020 && times.set_minutes <= 1080,
        "Sunset between 5:00-6:00 PM for SF in January (expected range: 1020-1080 min, actual: " 
        + to_string(times.set_minutes) + ")");
    
    results.add_suite_results(runner);
}

void test_singapore_pacific_offset() {
    runner.start_suite("Singapore Pacific Offset Tests");
    
    // Singapore location
    AstronomicalCalculator calc(1.3521, 103.8198, +8);  // Singapore timezone UTC+8
    
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
    
    runner.assert_true(projected.rise_valid, "Singapore projected sunrise calculated");
    // Singapore sunrise ~7:08 AM - 16 hours = 3:08 PM previous day = 15:08 (908 minutes)
    runner.assert_true(projected.rise_minutes >= 880 && projected.rise_minutes <= 940,
        "Projected sunrise appears around 3 PM (expected range: 880-940 min, actual: " 
        + to_string(projected.rise_minutes) + ")");
    
    results.add_suite_results(runner);
}

void test_negative_time_shift() {
    runner.start_suite("Negative Time Shift Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194, -8);  // San Francisco PST
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
    
    runner.assert_true(shifted_times.rise_valid && shifted_times.set_valid, 
        "Projected times are valid with negative shift");
    
    // Check that times shifted correctly (earlier by 4 hours = 240 minutes)
    int expected_rise = (base_times.rise_minutes - 240 + 1440) % 1440;
    int expected_set = (base_times.set_minutes - 240 + 1440) % 1440;
    
    runner.assert_equals(expected_rise, (int)shifted_times.rise_minutes, 
        "Projected sunrise is 4 hours earlier (expected: " + to_string(expected_rise) 
        + ", actual: " + to_string(shifted_times.rise_minutes) + ")");
    runner.assert_equals(expected_set, (int)shifted_times.set_minutes,
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
    
    runner.assert_true(solar_noon >= 480 && solar_noon <= 540, 
        "Solar noon is shifted correctly (expected around 8:23, actual: " 
        + to_string(solar_noon / 60) + ":" + to_string(solar_noon % 60) + ")");
    
    results.add_suite_results(runner);
}

void test_sun_rise_set_accuracy() {
    runner.start_suite("Sunrise/Sunset Accuracy Tests");

    // San Francisco, compared with NOAA solar calculator times (local clock time)
    struct Case { int year, month, day; double tz; int rise, set; const char* label; };
    const Case cases[] = {
        {2025, 6, 21, -7.0, 5 * 60 + 48, 20 * 60 + 35, "June solstice"},
        {2025, 12, 21, -8.0, 7 * 60 + 21, 16 * 60 + 54, "December solstice"},
        {2025, 3, 20, -7.0, 7 * 60 + 12, 19 * 60 + 21, "March equinox"},
    };
    for (const auto& c : cases) {
        AstronomicalCalculator calc(37.7749, -122.4194, c.tz);
        auto times = calc.get_sun_rise_set_times(AstronomicalCalculator::DateTime(c.year, c.month, c.day, 12, 0, 0));
        runner.assert_true(times.rise_valid && std::abs(static_cast<int>(times.rise_minutes) - c.rise) <= 3,
            string(c.label) + " sunrise within 3 min (expected " + to_string(c.rise) + ", actual " +
            to_string(times.rise_minutes) + ")");
        runner.assert_true(times.set_valid && std::abs(static_cast<int>(times.set_minutes) - c.set) <= 3,
            string(c.label) + " sunset within 3 min (expected " + to_string(c.set) + ", actual " +
            to_string(times.set_minutes) + ")");
    }

    results.add_suite_results(runner);
}

void test_projected_intensity_matches_shift() {
    runner.start_suite("Projected Intensity Tests");

    // Projection moves rise/set later by the shift, so the projected sky at t must be the real sky at t - shift
    AstronomicalCalculator real(37.7749, -122.4194, -7.0);
    AstronomicalCalculator projected(37.7749, -122.4194, -7.0);
    projected.set_projection_settings(true, 3, 0);
    for (int hour = 9; hour <= 21; hour += 4) {
        AstronomicalCalculator::DateTime at(2025, 6, 21, hour, 0, 0);
        AstronomicalCalculator::DateTime earlier(2025, 6, 21, hour - 3, 0, 0);
        runner.assert_equals(real.get_sun_intensity(earlier), projected.get_projected_sun_intensity(at), 0.001f,
            "Projected sun at " + to_string(hour) + ":00 equals real sun 3 h earlier");
    }

    results.add_suite_results(runner);
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
    test_sun_rise_set_accuracy();
    test_projected_intensity_matches_shift();
    
    results.print_final_summary("Astronomical Calculator");
    
    return results.get_exit_code();
}