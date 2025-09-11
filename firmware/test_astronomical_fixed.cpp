/**
 * Astronomical Calculator Tests
 * Comprehensive test suite for sun and moon calculations
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include "astronomical_calculator.h"
#include "test_framework.h"

using namespace std;

// Global test runner and results
TestRunner runner;
TestResults results;

// Helper to convert HH:MM to minutes
int time_to_minutes(int hour, int minute) {
    return hour * 60 + minute;
}

// Helper to format minutes as HH:MM
string minutes_to_time(int minutes) {
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
    return string(buffer);
}

void test_julian_day_calculation() {
    runner.start_suite("Julian Day Calculation Tests");
    
    AstronomicalCalculator calc(0, 0);
    
    // Test J2000 epoch
    AstronomicalCalculator::DateTime j2000(2000, 1, 1, 12, 0, 0);
    double jd = calc.calculate_julian_day(j2000);
    runner.assert_equals(2451545.0, jd, 0.001, "J2000 epoch calculation");
    
    // Test current date range
    AstronomicalCalculator::DateTime now(2025, 1, 8, 12, 0, 0);
    jd = calc.calculate_julian_day(now);
    runner.assert_true(jd > 2460000 && jd < 2470000, 
        "Current date JD in expected range");
    
    // Test specific dates
    struct {
        int year, month, day;
        double expected_jd;
        const char* description;
    } test_dates[] = {
        {2025, 9, 11, 2460565.5, "2025-09-11 midnight"},
        {2025, 1, 1, 2460676.5, "2025 New Year"},
        {2000, 3, 1, 2451605.5, "2000 leap year March"}
    };
    
    for (const auto& td : test_dates) {
        AstronomicalCalculator::DateTime dt(td.year, td.month, td.day, 0, 0, 0);
        jd = calc.calculate_julian_day(dt);
        runner.assert_equals(td.expected_jd, jd, 0.001, td.description);
    }
    
    results.add_suite_results(runner);
}

void test_moon_phase() {
    runner.start_suite("Moon Phase Calculation Tests");
    
    AstronomicalCalculator calc(0, 0);
    
    // Test phase range
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    float phase = calc.get_moon_phase(dt);
    runner.assert_true(phase >= 0.0f && phase <= 1.0f, 
        "Moon phase in valid range [0,1]");
    
    // Test known moon phases
    struct {
        int year, month, day;
        float expected_phase;
        float tolerance;
        const char* description;
    } phase_tests[] = {
        {2025, 1, 13, 0.50f, 0.02f, "Full moon Jan 13, 2025"},
        {2025, 1, 29, 0.00f, 0.02f, "New moon Jan 29, 2025"},
        {2025, 9, 7, 0.50f, 0.02f, "Full moon Sep 7, 2025"},
        {2025, 9, 11, 0.64f, 0.03f, "Waning gibbous Sep 11, 2025"}
    };
    
    for (const auto& pt : phase_tests) {
        AstronomicalCalculator::DateTime dt(pt.year, pt.month, pt.day, 12, 0, 0);
        phase = calc.get_moon_phase(dt);
        
        // Handle new moon wrap-around
        if (pt.expected_phase == 0.0f && phase > 0.95f) {
            phase = 1.0f - phase;  // Convert to distance from new moon
        }
        
        runner.assert_equals(pt.expected_phase, phase, pt.tolerance, pt.description);
    }
    
    results.add_suite_results(runner);
}

void test_sun_position_accuracy() {
    runner.start_suite("Sun Position Accuracy Tests");
    
    // Test sun rise/set times against known data
    struct {
        double lat, lon;
        int year, month, day;
        int expected_sunrise, expected_sunset;
        const char* location;
    } sun_data[] = {
        {37.7749, -122.4194, 2025, 1, 15, time_to_minutes(7,25), time_to_minutes(17,20), "San Francisco"},
        {13.7563, 100.5018, 2025, 9, 11, time_to_minutes(5,53), time_to_minutes(18,8), "Bangkok"},
        {51.5074, -0.1278, 2025, 6, 21, time_to_minutes(4,43), time_to_minutes(21,21), "London summer"},
        {-33.8688, 151.2093, 2025, 1, 15, time_to_minutes(5,45), time_to_minutes(20,4), "Sydney summer"}
    };
    
    for (const auto& sd : sun_data) {
        AstronomicalCalculator calc(sd.lat, sd.lon);
        AstronomicalCalculator::DateTime dt(sd.year, sd.month, sd.day, 12, 0, 0);
        
        auto sun_times = calc.get_sun_rise_set_times(dt);
        
        if (sun_times.rise_valid && sun_times.set_valid) {
            int rise_error = abs((int)sun_times.rise_minutes - sd.expected_sunrise);
            int set_error = abs((int)sun_times.set_minutes - sd.expected_sunset);
            
            cout << sd.location << " - Sunrise: expected " << minutes_to_time(sd.expected_sunrise)
                 << ", got " << minutes_to_time(sun_times.rise_minutes) 
                 << " (error: " << rise_error << " min)" << endl;
            
            // Allow 5 minute tolerance for sun calculations
            runner.assert_true(rise_error <= 5, 
                string(sd.location) + " sunrise accuracy");
            runner.assert_true(set_error <= 5,
                string(sd.location) + " sunset accuracy");
        } else {
            runner.assert_true(false, string(sd.location) + " sun calculation failed");
        }
    }
    
    results.add_suite_results(runner);
}

void test_moon_position_accuracy() {
    runner.start_suite("Moon Position Accuracy Tests");
    
    // Test Bangkok moon times for Sep 11-12, 2025
    struct {
        double lat, lon;
        int year, month, day;
        int expected_moonrise, expected_moonset;
        const char* location;
        int rise_tolerance, set_tolerance;
    } moon_data[] = {
        {13.7563, 100.5018, 2025, 9, 11, time_to_minutes(21,4), time_to_minutes(9,12), "Bangkok Sep 11", 30, 60},
        {13.7563, 100.5018, 2025, 9, 12, time_to_minutes(21,55), time_to_minutes(10,14), "Bangkok Sep 12", 30, 60},
        {37.7749, -122.4194, 2025, 1, 13, time_to_minutes(18,46), time_to_minutes(7,55), "SF Full Moon", 45, 45}
    };
    
    for (const auto& md : moon_data) {
        AstronomicalCalculator calc(md.lat, md.lon);
        AstronomicalCalculator::DateTime dt(md.year, md.month, md.day, 12, 0, 0);
        
        auto moon_times = calc.get_moon_rise_set_times(dt);
        
        cout << md.location << " - ";
        
        if (moon_times.rise_valid) {
            int rise_error = abs((int)moon_times.rise_minutes - md.expected_moonrise);
            cout << "Moonrise: expected " << minutes_to_time(md.expected_moonrise)
                 << ", got " << minutes_to_time(moon_times.rise_minutes)
                 << " (error: " << rise_error << " min)" << endl;
            
            runner.assert_true(rise_error <= md.rise_tolerance,
                string(md.location) + " moonrise accuracy");
        }
        
        if (moon_times.set_valid) {
            int set_error = abs((int)moon_times.set_minutes - md.expected_moonset);
            cout << "         Moonset: expected " << minutes_to_time(md.expected_moonset)
                 << ", got " << minutes_to_time(moon_times.set_minutes)
                 << " (error: " << set_error << " min)" << endl;
            
            runner.assert_true(set_error <= md.set_tolerance,
                string(md.location) + " moonset accuracy");
        }
    }
    
    results.add_suite_results(runner);
}

void test_time_projection() {
    runner.start_suite("Time Projection Tests");
    
    // Singapore location projecting to Pacific time
    AstronomicalCalculator calc(1.3521, 103.8198);
    
    // Get base times
    AstronomicalCalculator::DateTime dt(2025, 1, 8, 12, 0, 0);
    auto base_times = calc.get_sun_rise_set_times(dt);
    
    // Enable projection to Pacific time (Singapore UTC+8 to Pacific UTC-8 = -16 hours)
    calc.set_projection_settings(true, -16, 0);
    auto projected = calc.get_projected_sun_rise_set_times(dt);
    
    runner.assert_true(projected.rise_valid && projected.set_valid,
        "Projected times are valid");
    
    // Projected sunrise should appear very early in Singapore time
    runner.assert_true(projected.rise_minutes < 180,  // Before 3 AM
        "Singapore sunrise projected to Pacific morning appears early");
    
    results.add_suite_results(runner);
}

void test_edge_cases() {
    runner.start_suite("Edge Case Tests");
    
    // Arctic circle during summer
    AstronomicalCalculator arctic(71.0, 25.0);
    AstronomicalCalculator::DateTime summer(2025, 6, 21, 12, 0, 0);
    auto arctic_sun = arctic.get_sun_rise_set_times(summer);
    
    // During polar day, might not have both rise and set
    runner.assert_true(!arctic_sun.rise_valid || !arctic_sun.set_valid,
        "Arctic summer polar day detection");
    
    // Equator
    AstronomicalCalculator equator(0.0, 0.0);
    auto eq_times = equator.get_sun_rise_set_times(summer);
    runner.assert_true(eq_times.rise_valid && eq_times.set_valid,
        "Equator has sunrise and sunset");
    
    // Day length at equator should be ~12 hours
    int day_length = eq_times.set_minutes - eq_times.rise_minutes;
    runner.assert_true(abs(day_length - 720) < 15,
        "Equator day length approximately 12 hours");
    
    results.add_suite_results(runner);
}

void test_moon_daily_progression() {
    runner.start_suite("Moon Daily Progression Tests");
    
    AstronomicalCalculator calc(37.7749, -122.4194);
    
    // Check moonrise progression over several days
    int last_rise = -1;
    cout << "\nMoon rise progression:" << endl;
    
    for (int day = 10; day <= 15; day++) {
        AstronomicalCalculator::DateTime dt(2025, 1, day, 12, 0, 0);
        auto times = calc.get_moon_rise_set_times(dt);
        
        if (times.rise_valid) {
            cout << "Jan " << day << ": " << minutes_to_time(times.rise_minutes);
            
            if (last_rise > 0) {
                int shift = times.rise_minutes - last_rise;
                if (shift < 0) shift += 1440;  // Handle day wrap
                cout << " (+" << shift << " min)";
                
                // Moon should rise 40-60 minutes later each day
                runner.assert_true(shift >= 40 && shift <= 60,
                    "Daily moonrise shift in expected range");
            }
            cout << endl;
            
            last_rise = times.rise_minutes;
        }
    }
    
    results.add_suite_results(runner);
}

void analyze_systematic_errors() {
    cout << "\n=== SYSTEMATIC ERROR ANALYSIS ===" << endl;
    cout << "Identifying patterns in calculation errors..." << endl;
    
    // Test multiple Bangkok dates to find error pattern
    AstronomicalCalculator calc(13.7563, 100.5018);
    
    cout << "\nBangkok moon calculations (Sep 2025):" << endl;
    cout << "Date       Calc Rise  Error (vs timeanddate.com)" << endl;
    cout << "---------  ---------  -------------------------" << endl;
    
    int total_error = 0;
    int count = 0;
    
    for (int day = 10; day <= 13; day++) {
        AstronomicalCalculator::DateTime dt(2025, 9, day, 12, 0, 0);
        auto times = calc.get_moon_rise_set_times(dt);
        
        if (times.rise_valid) {
            // Known times from timeanddate.com
            int expected = 0;
            if (day == 10) expected = time_to_minutes(20, 17);
            else if (day == 11) expected = time_to_minutes(21, 4);
            else if (day == 12) expected = time_to_minutes(21, 55);
            else if (day == 13) expected = time_to_minutes(22, 50);
            
            if (expected > 0) {
                int error = (int)times.rise_minutes - expected;
                total_error += error;
                count++;
                
                cout << "Sep " << setw(2) << day << "     " 
                     << minutes_to_time(times.rise_minutes) << "  "
                     << showpos << error << " min" << noshowpos << endl;
            }
        }
    }
    
    if (count > 0) {
        cout << "\nAverage error: " << (total_error / count) << " minutes" << endl;
        cout << "Our calculations tend to be " 
             << abs(total_error / count) << " minutes "
             << (total_error < 0 ? "early" : "late") << endl;
    }
    
    cout << "\nPossible causes of systematic errors:" << endl;
    cout << "1. Atmospheric refraction model differences" << endl;
    cout << "2. Moon's upper limb vs center calculations" << endl;
    cout << "3. Parallax corrections not fully applied" << endl;
    cout << "4. DeltaT (TT-UT) not accounted for" << endl;
}

int main() {
    cout << "=== ASTRONOMICAL CALCULATOR TEST SUITE ===" << endl;
    cout << "Testing astronomical calculations for accuracy" << endl;
    cout << "============================================" << endl;
    
    test_julian_day_calculation();
    test_moon_phase();
    test_sun_position_accuracy();
    test_moon_position_accuracy();
    test_time_projection();
    test_edge_cases();
    test_moon_daily_progression();
    
    results.print_final_summary("Astronomical Calculator");
    
    // Additional analysis
    analyze_systematic_errors();
    
    return results.get_exit_code();
}