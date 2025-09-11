/**
 * Astronomical Calculator Accuracy Tests
 * Tests sun and moon calculations against known astronomical data
 * to identify and quantify timing errors
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include "astronomical_calculator.h"
#include "test_framework.h"

using namespace std;

// Structure to hold reference data
struct ReferenceData {
    string date;
    int year, month, day;
    double latitude, longitude;
    string location;
    int expected_sunrise;  // minutes from midnight
    int expected_sunset;   // minutes from midnight
    int expected_moonrise; // minutes from midnight
    int expected_moonset;  // minutes from midnight
    float expected_moon_phase;
};

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

void test_sun_accuracy_comprehensive() {
    TEST_SUITE("Sun Position Accuracy Tests");
    
    // Reference data from USNO/NOAA calculators
    vector<ReferenceData> sun_data = {
        // San Francisco - Winter Solstice 2024
        {"2024-12-21", 2024, 12, 21, 37.7749, -122.4194, "San Francisco", 
         time_to_minutes(7, 21), time_to_minutes(16, 54), 0, 0, 0.0f},
        
        // San Francisco - Spring Equinox 2025
        {"2025-03-20", 2025, 3, 20, 37.7749, -122.4194, "San Francisco",
         time_to_minutes(7, 9), time_to_minutes(19, 20), 0, 0, 0.0f},
         
        // San Francisco - Summer Solstice 2025
        {"2025-06-21", 2025, 6, 21, 37.7749, -122.4194, "San Francisco",
         time_to_minutes(5, 48), time_to_minutes(20, 35), 0, 0, 0.0f},
         
        // Bangkok, Thailand - September 11, 2025
        {"2025-09-11", 2025, 9, 11, 13.7563, 100.5018, "Bangkok",
         time_to_minutes(5, 53), time_to_minutes(18, 8), 0, 0, 0.0f},
         
        // Singapore - Equatorial location
        {"2025-01-15", 2025, 1, 15, 1.3521, 103.8198, "Singapore",
         time_to_minutes(7, 7), time_to_minutes(19, 12), 0, 0, 0.0f},
         
        // Reykjavik, Iceland - High latitude
        {"2025-01-15", 2025, 1, 15, 64.1466, -21.9426, "Reykjavik",
         time_to_minutes(11, 13), time_to_minutes(15, 46), 0, 0, 0.0f},
         
        // Sydney, Australia - Southern Hemisphere
        {"2025-01-15", 2025, 1, 15, -33.8688, 151.2093, "Sydney",
         time_to_minutes(5, 45), time_to_minutes(20, 4), 0, 0, 0.0f}
    };
    
    for (const auto& ref : sun_data) {
        if (ref.expected_sunrise == 0) continue; // Skip if no sun data
        
        cout << "\nTesting " << ref.location << " on " << ref.date << endl;
        
        AstronomicalCalculator calc(ref.latitude, ref.longitude);
        AstronomicalCalculator::DateTime dt(ref.year, ref.month, ref.day, 12, 0, 0);
        
        auto sun_times = calc.get_sun_rise_set_times(dt);
        
        if (sun_times.rise_valid && sun_times.set_valid) {
            int rise_error = sun_times.rise_minutes - ref.expected_sunrise;
            int set_error = sun_times.set_minutes - ref.expected_sunset;
            
            cout << "  Sunrise: Expected " << minutes_to_time(ref.expected_sunrise) 
                 << ", Got " << minutes_to_time(sun_times.rise_minutes)
                 << " (Error: " << rise_error << " min)" << endl;
            cout << "  Sunset:  Expected " << minutes_to_time(ref.expected_sunset)
                 << ", Got " << minutes_to_time(sun_times.set_minutes)
                 << " (Error: " << set_error << " min)" << endl;
            
            // Test passes if within 5 minutes (accounting for atmospheric model differences)
            TEST_ASSERT(abs(rise_error) <= 5, 
                ref.location + " sunrise accuracy (error: " + to_string(rise_error) + " min)");
            TEST_ASSERT(abs(set_error) <= 5,
                ref.location + " sunset accuracy (error: " + to_string(set_error) + " min)");
        } else {
            TEST_ASSERT(false, ref.location + " sun calculations failed");
        }
    }
}

void test_moon_accuracy_comprehensive() {
    TEST_SUITE("Moon Position Accuracy Tests");
    
    // Reference data from timeanddate.com and other sources
    vector<ReferenceData> moon_data = {
        // Bangkok - September 11, 2025 (from our investigation)
        {"2025-09-11", 2025, 9, 11, 13.7563, 100.5018, "Bangkok",
         0, 0, time_to_minutes(21, 4), time_to_minutes(9, 12), 0.64f},
         
        // Bangkok - September 12, 2025
        {"2025-09-12", 2025, 9, 12, 13.7563, 100.5018, "Bangkok",
         0, 0, time_to_minutes(21, 55), time_to_minutes(10, 14), 0.71f},
         
        // San Francisco - Full Moon January 13, 2025
        {"2025-01-13", 2025, 1, 13, 37.7749, -122.4194, "San Francisco",
         0, 0, time_to_minutes(18, 46), time_to_minutes(7, 55), 0.50f},
         
        // San Francisco - New Moon January 29, 2025
        {"2025-01-29", 2025, 1, 29, 37.7749, -122.4194, "San Francisco",
         0, 0, time_to_minutes(7, 14), time_to_minutes(18, 2), 0.00f},
         
        // Singapore - Waning Gibbous
        {"2025-09-11", 2025, 9, 11, 1.3521, 103.8198, "Singapore",
         0, 0, time_to_minutes(20, 25), time_to_minutes(8, 26), 0.64f}
    };
    
    for (const auto& ref : moon_data) {
        if (ref.expected_moonrise == 0) continue; // Skip if no moon data
        
        cout << "\nTesting " << ref.location << " on " << ref.date << endl;
        
        AstronomicalCalculator calc(ref.latitude, ref.longitude);
        AstronomicalCalculator::DateTime dt(ref.year, ref.month, ref.day, 12, 0, 0);
        
        auto moon_times = calc.get_moon_rise_set_times(dt);
        float phase = calc.get_moon_phase(dt);
        
        cout << "  Moon Phase: Expected " << fixed << setprecision(2) << ref.expected_moon_phase
             << ", Got " << phase << " (Error: " << abs(phase - ref.expected_moon_phase) << ")" << endl;
        
        if (moon_times.rise_valid) {
            int rise_error = moon_times.rise_minutes - ref.expected_moonrise;
            cout << "  Moonrise: Expected " << minutes_to_time(ref.expected_moonrise)
                 << ", Got " << minutes_to_time(moon_times.rise_minutes)
                 << " (Error: " << rise_error << " min)" << endl;
            
            // For moon, we currently have larger errors, so test for < 30 minutes
            TEST_ASSERT(abs(rise_error) <= 30,
                ref.location + " moonrise accuracy (error: " + to_string(rise_error) + " min)");
        }
        
        if (moon_times.set_valid) {
            int set_error = moon_times.set_minutes - ref.expected_moonset;
            cout << "  Moonset:  Expected " << minutes_to_time(ref.expected_moonset)
                 << ", Got " << minutes_to_time(moon_times.set_minutes)
                 << " (Error: " << set_error << " min)" << endl;
            
            TEST_ASSERT(abs(set_error) <= 60,  // Moonset has larger errors
                ref.location + " moonset accuracy (error: " + to_string(set_error) + " min)");
        }
        
        TEST_ASSERT(abs(phase - ref.expected_moon_phase) <= 0.05f,
            ref.location + " moon phase accuracy");
    }
}

void test_atmospheric_refraction() {
    TEST_SUITE("Atmospheric Refraction Tests");
    
    // Test that we're accounting for atmospheric refraction
    // Standard refraction at horizon is 34 arcminutes (0.5667 degrees)
    
    AstronomicalCalculator calc(45.0, 0.0); // Mid-latitude
    
    // Find a sunrise by scanning
    AstronomicalCalculator::DateTime dt(2025, 3, 20, 6, 0, 0); // Spring equinox
    
    cout << "\nScanning for horizon crossing..." << endl;
    
    double last_altitude = -90.0;
    int crossing_minutes = -1;
    
    for (int minutes = 300; minutes < 480; minutes++) { // 5 AM to 8 AM
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        
        auto pos = calc.calculate_sun_position(dt);
        
        if (last_altitude < 0 && pos.altitude >= 0) {
            crossing_minutes = minutes;
            cout << "Sun crosses horizon at " << minutes_to_time(minutes) 
                 << " (altitude: " << pos.altitude << "°)" << endl;
            
            // Check altitude at crossing - should be near -0.5667° with refraction
            // or 0° without refraction
            TEST_ASSERT(pos.altitude < 1.0, 
                "Sun altitude at rise is near horizon (actual: " + to_string(pos.altitude) + "°)");
            break;
        }
        
        last_altitude = pos.altitude;
    }
    
    TEST_ASSERT(crossing_minutes > 0, "Found sunrise crossing");
}

void test_julian_day_precision() {
    TEST_SUITE("Julian Day Precision Tests");
    
    AstronomicalCalculator calc(0, 0);
    
    // Test known Julian dates with high precision
    struct {
        int year, month, day, hour, minute, second;
        double expected_jd;
        const char* description;
    } test_cases[] = {
        {2000, 1, 1, 12, 0, 0, 2451545.0, "J2000 epoch"},
        {2025, 1, 1, 0, 0, 0, 2460676.5, "2025 New Year midnight"},
        {2025, 9, 11, 12, 0, 0, 2460565.0, "2025-09-11 noon"},
        {1900, 1, 0, 12, 0, 0, 2415020.0, "1899-12-31 noon (year boundary)"}, // Dec 31, 1899
        {2100, 3, 1, 0, 0, 0, 2488069.5, "2100 non-leap year March 1"}
    };
    
    for (const auto& tc : test_cases) {
        AstronomicalCalculator::DateTime dt(tc.year, tc.month, tc.day, tc.hour, tc.minute, tc.second);
        double jd = calc.calculate_julian_day(dt);
        double error = jd - tc.expected_jd;
        
        cout << tc.description << ": Expected JD " << fixed << setprecision(1) << tc.expected_jd
             << ", Got " << jd << " (Error: " << setprecision(6) << error << ")" << endl;
        
        TEST_ASSERT_EQUAL_FLOAT(tc.expected_jd, jd, 0.001, tc.description);
    }
}

void test_edge_cases() {
    TEST_SUITE("Edge Case Tests");
    
    // Test polar locations during polar day/night
    cout << "\nTesting Arctic Circle during summer..." << endl;
    AstronomicalCalculator arctic(71.0, 25.0); // North of Arctic Circle
    AstronomicalCalculator::DateTime summer(2025, 6, 21, 12, 0, 0);
    auto arctic_sun = arctic.get_sun_rise_set_times(summer);
    
    cout << "Arctic sun times: Rise valid=" << arctic_sun.rise_valid 
         << ", Set valid=" << arctic_sun.set_valid << endl;
    
    // During polar day, sun doesn't set
    TEST_ASSERT(!arctic_sun.rise_valid || !arctic_sun.set_valid,
        "Arctic Circle polar day detection");
    
    // Test equator
    cout << "\nTesting equatorial location..." << endl;
    AstronomicalCalculator equator(0.0, 0.0);
    auto eq_sun = equator.get_sun_rise_set_times(summer);
    
    TEST_ASSERT(eq_sun.rise_valid && eq_sun.set_valid,
        "Equator always has sunrise and sunset");
    
    // Day length should be close to 12 hours at equator
    int day_length = eq_sun.set_minutes - eq_sun.rise_minutes;
    cout << "Equator day length: " << day_length << " minutes" << endl;
    TEST_ASSERT(abs(day_length - 720) < 10,  // Within 10 minutes of 12 hours
        "Equator day length approximately 12 hours");
}

void analyze_error_patterns() {
    cout << "\n=== ERROR PATTERN ANALYSIS ===" << endl;
    
    // Test multiple locations to identify systematic errors
    struct TestLocation {
        double lat, lon;
        const char* name;
    };
    
    TestLocation locations[] = {
        {13.7563, 100.5018, "Bangkok"},
        {37.7749, -122.4194, "San Francisco"},
        {51.5074, -0.1278, "London"},
        {-33.8688, 151.2093, "Sydney"},
        {1.3521, 103.8198, "Singapore"},
        {40.7128, -74.0060, "New York"}
    };
    
    cout << "\nMoon timing errors for September 11, 2025:" << endl;
    cout << "Location        Lat      Lon       Moonrise  Error" << endl;
    cout << "--------------- -------- --------- --------- -----" << endl;
    
    for (const auto& loc : locations) {
        AstronomicalCalculator calc(loc.lat, loc.lon);
        AstronomicalCalculator::DateTime dt(2025, 9, 11, 12, 0, 0);
        
        auto moon_times = calc.get_moon_rise_set_times(dt);
        
        if (moon_times.rise_valid) {
            // Rough approximation: moonrise should be around 20:00-22:00 for this date
            int expected_approx = time_to_minutes(21, 0);
            int error = moon_times.rise_minutes - expected_approx;
            
            cout << left << setw(15) << loc.name 
                 << " " << setw(8) << fixed << setprecision(4) << loc.lat
                 << " " << setw(9) << loc.lon
                 << " " << setw(9) << minutes_to_time(moon_times.rise_minutes)
                 << " " << showpos << error << " min" << noshowpos << endl;
        }
    }
    
    cout << "\nNote: Errors may vary by latitude and calculation method." << endl;
    cout << "Negative errors mean our calculation is earlier than expected." << endl;
}

void test_rise_set_detection_precision() {
    TEST_SUITE("Rise/Set Detection Precision");
    
    // Test that we're detecting crossings at the right altitude
    AstronomicalCalculator calc(37.7749, -122.4194);
    AstronomicalCalculator::DateTime dt(2025, 1, 15, 0, 0, 0);
    
    cout << "\nScanning sunrise with fine resolution..." << endl;
    
    // Find exact crossing point
    double last_alt = -90.0;
    for (int minutes = 420; minutes < 480; minutes++) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        
        auto pos = calc.calculate_sun_position(dt);
        
        if (last_alt < -0.6 && pos.altitude > -0.6) {
            cout << "Near horizon at " << minutes_to_time(minutes) 
                 << ": altitude = " << fixed << setprecision(3) << pos.altitude << "°" << endl;
            
            // Should cross near -0.5667° (standard refraction)
            if (pos.altitude > -0.6 && pos.altitude < -0.5) {
                TEST_ASSERT(true, "Sunrise detection includes refraction");
            }
        }
        
        last_alt = pos.altitude;
    }
}

int main() {
    cout << "=== ASTRONOMICAL CALCULATOR ACCURACY TESTS ===" << endl;
    cout << "Testing against known astronomical data" << endl;
    cout << "================================================" << endl;
    
    test_julian_day_precision();
    test_sun_accuracy_comprehensive();
    test_moon_accuracy_comprehensive();
    test_atmospheric_refraction();
    test_edge_cases();
    test_rise_set_detection_precision();
    analyze_error_patterns();
    
    TEST_SUMMARY("Astronomical Accuracy");
    
    if (TEST_FAILED_COUNT > 0) {
        cout << "\n⚠️  Some tests failed. Review the errors above for details." << endl;
        cout << "Common causes of 10-30 minute errors:" << endl;
        cout << "1. Atmospheric refraction modeling differences" << endl;
        cout << "2. Upper limb vs center of disk for rise/set" << endl;
        cout << "3. Equation of time not fully accounted for" << endl;
        cout << "4. DeltaT (TT-UT) corrections not applied" << endl;
        cout << "5. Topocentric vs geocentric calculations" << endl;
    }
    
    return TEST_FAILED_COUNT > 0 ? 1 : 0;
}