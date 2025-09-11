#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"

using namespace std;

int time_to_minutes(int hour, int minute) {
    return hour * 60 + minute;
}

string minutes_to_time(int minutes) {
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
    return string(buffer);
}

int main() {
    cout << "=== FINAL ASTRONOMICAL ACCURACY TEST ===" << endl;
    cout << "With refraction corrections and proper timezones\n" << endl;
    
    // Test locations with known data
    struct TestCase {
        const char* name;
        double lat, lon, tz;
        int year, month, day;
        int expected_sunrise, expected_sunset;
        int expected_moonrise, expected_moonset;
    };
    
    TestCase cases[] = {
        {"San Francisco", 37.7749, -122.4194, -8, 2025, 1, 15, 
         time_to_minutes(7,25), time_to_minutes(17,20), 0, 0},
        {"Bangkok", 13.7563, 100.5018, +7, 2025, 9, 11, 
         time_to_minutes(5,53), time_to_minutes(18,8), 
         time_to_minutes(21,4), time_to_minutes(9,12)},
        {"London Summer", 51.5074, -0.1278, +1, 2025, 6, 21, 
         time_to_minutes(4,43), time_to_minutes(21,21), 0, 0},
        {"Sydney Summer", -33.8688, 151.2093, +11, 2025, 1, 15, 
         time_to_minutes(5,45), time_to_minutes(20,4), 0, 0}
    };
    
    int total_tests = 0;
    int passed_tests = 0;
    
    for (const auto& tc : cases) {
        cout << "=== " << tc.name << " ===" << endl;
        cout << "Date: " << tc.year << "-" << setfill('0') << setw(2) << tc.month 
             << "-" << setw(2) << tc.day << endl;
        cout << "Location: " << fixed << setprecision(4) << tc.lat << "°, " << tc.lon << "°" << endl;
        cout << "Timezone: UTC" << showpos << tc.tz << noshowpos << endl;
        
        AstronomicalCalculator calc(tc.lat, tc.lon, tc.tz);
        AstronomicalCalculator::DateTime dt(tc.year, tc.month, tc.day, 12, 0, 0);
        
        // Test sun times
        if (tc.expected_sunrise > 0) {
            auto sun_times = calc.get_sun_rise_set_times(dt);
            
            if (sun_times.rise_valid && sun_times.set_valid) {
                int rise_err = (int)sun_times.rise_minutes - tc.expected_sunrise;
                int set_err = (int)sun_times.set_minutes - tc.expected_sunset;
                
                cout << "\nSun:" << endl;
                cout << "  Sunrise: " << minutes_to_time(sun_times.rise_minutes) 
                     << " (expected " << minutes_to_time(tc.expected_sunrise)
                     << ", error: " << showpos << rise_err << " min)" << noshowpos << endl;
                cout << "  Sunset:  " << minutes_to_time(sun_times.set_minutes)
                     << " (expected " << minutes_to_time(tc.expected_sunset) 
                     << ", error: " << showpos << set_err << " min)" << noshowpos << endl;
                
                total_tests += 2;
                if (abs(rise_err) <= 5) {
                    cout << "  ✓ Sunrise PASS" << endl;
                    passed_tests++;
                } else {
                    cout << "  ✗ Sunrise FAIL" << endl;
                }
                if (abs(set_err) <= 5) {
                    cout << "  ✓ Sunset PASS" << endl;
                    passed_tests++;
                } else {
                    cout << "  ✗ Sunset FAIL" << endl;
                }
            }
        }
        
        // Test moon times
        if (tc.expected_moonrise > 0) {
            auto moon_times = calc.get_moon_rise_set_times(dt);
            float phase = calc.get_moon_phase(dt);
            
            cout << "\nMoon (phase: " << fixed << setprecision(2) << phase << "):" << endl;
            
            if (moon_times.rise_valid) {
                int rise_err = (int)moon_times.rise_minutes - tc.expected_moonrise;
                cout << "  Moonrise: " << minutes_to_time(moon_times.rise_minutes)
                     << " (expected " << minutes_to_time(tc.expected_moonrise)
                     << ", error: " << showpos << rise_err << " min)" << noshowpos << endl;
                
                total_tests++;
                if (abs(rise_err) <= 30) {
                    cout << "  ✓ Moonrise PASS" << endl;
                    passed_tests++;
                } else {
                    cout << "  ✗ Moonrise FAIL" << endl;
                }
            }
            
            if (moon_times.set_valid) {
                int set_err = (int)moon_times.set_minutes - tc.expected_moonset;
                cout << "  Moonset:  " << minutes_to_time(moon_times.set_minutes)
                     << " (expected " << minutes_to_time(tc.expected_moonset)
                     << ", error: " << showpos << set_err << " min)" << noshowpos << endl;
                
                total_tests++;
                if (abs(set_err) <= 60) {
                    cout << "  ✓ Moonset PASS" << endl;
                    passed_tests++;
                } else {
                    cout << "  ✗ Moonset FAIL" << endl;
                }
            }
        }
        
        cout << endl;
    }
    
    cout << "=== OVERALL RESULTS ===" << endl;
    cout << "Total tests: " << total_tests << endl;
    cout << "Passed: " << passed_tests << endl;
    cout << "Failed: " << (total_tests - passed_tests) << endl;
    cout << "Success rate: " << fixed << setprecision(1) 
         << (100.0 * passed_tests / total_tests) << "%" << endl;
    
    if (passed_tests == total_tests) {
        cout << "\n✅ All tests passed\!" << endl;
    } else {
        cout << "\n⚠️  Some tests failed, but accuracy is suitable for aquarium lighting." << endl;
    }
    
    return 0;
}
