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
    cout << "=== SUNRISE/SUNSET WITH PROPER TIMEZONES ===" << endl;
    
    // Test data with proper timezone offsets
    struct TestLocation {
        double lat, lon;
        double timezone_offset;  // Hours from UTC
        int year, month, day;
        int expected_sunrise, expected_sunset;
        const char* location;
        const char* timezone_name;
    };
    
    TestLocation locations[] = {
        {37.7749, -122.4194, -8, 2025, 1, 15, time_to_minutes(7,25), time_to_minutes(17,20), 
         "San Francisco", "PST (UTC-8)"},
        {13.7563, 100.5018, +7, 2025, 9, 11, time_to_minutes(5,53), time_to_minutes(18,8), 
         "Bangkok", "ICT (UTC+7)"},
        {51.5074, -0.1278, +1, 2025, 6, 21, time_to_minutes(4,43), time_to_minutes(21,21), 
         "London summer", "BST (UTC+1)"},
        {-33.8688, 151.2093, +11, 2025, 1, 15, time_to_minutes(5,45), time_to_minutes(20,4), 
         "Sydney summer", "AEDT (UTC+11)"}
    };
    
    for (const auto& loc : locations) {
        cout << "\n" << loc.location << " (" << loc.timezone_name << ")" << endl;
        cout << "Date: " << loc.year << "-" << setfill('0') << setw(2) << loc.month 
             << "-" << setw(2) << loc.day << endl;
        
        // Create calculator with proper timezone
        AstronomicalCalculator calc(loc.lat, loc.lon);
        calc.set_timezone_offset(loc.timezone_offset);
        
        // Get sunrise/sunset times
        AstronomicalCalculator::DateTime dt(loc.year, loc.month, loc.day, 12, 0, 0);
        auto sun_times = calc.get_sun_rise_set_times(dt);
        
        if (sun_times.rise_valid && sun_times.set_valid) {
            int rise_error = (int)sun_times.rise_minutes - loc.expected_sunrise;
            int set_error = (int)sun_times.set_minutes - loc.expected_sunset;
            
            cout << "Sunrise: Expected " << minutes_to_time(loc.expected_sunrise)
                 << ", Got " << minutes_to_time(sun_times.rise_minutes)
                 << " (error: " << showpos << rise_error << " min)" << noshowpos << endl;
            cout << "Sunset:  Expected " << minutes_to_time(loc.expected_sunset)
                 << ", Got " << minutes_to_time(sun_times.set_minutes)
                 << " (error: " << showpos << set_error << " min)" << noshowpos << endl;
            
            if (abs(rise_error) <= 5 && abs(set_error) <= 5) {
                cout << "✓ PASS - Within 5 minute tolerance" << endl;
            } else {
                cout << "✗ FAIL - Outside tolerance" << endl;
            }
        } else {
            cout << "ERROR: Failed to calculate sun times" << endl;
        }
    }
    
    cout << "\n=== COMPARISON: Auto-timezone vs Manual ===" << endl;
    
    // Compare San Francisco with and without manual timezone
    AstronomicalCalculator sf_auto(37.7749, -122.4194);  // Auto timezone
    AstronomicalCalculator sf_manual(37.7749, -122.4194);
    sf_manual.set_timezone_offset(-8.0);  // Manual PST
    
    AstronomicalCalculator::DateTime dt(2025, 1, 15, 12, 0, 0);
    auto auto_times = sf_auto.get_sun_rise_set_times(dt);
    auto manual_times = sf_manual.get_sun_rise_set_times(dt);
    
    cout << "San Francisco sunrise:" << endl;
    cout << "  Auto timezone:   " << minutes_to_time(auto_times.rise_minutes) << endl;
    cout << "  Manual PST:      " << minutes_to_time(manual_times.rise_minutes) << endl;
    cout << "  Difference:      " << (int)auto_times.rise_minutes - (int)manual_times.rise_minutes << " minutes" << endl;
    
    return 0;
}
