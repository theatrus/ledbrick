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

void test_sun_position_accuracy() {
    runner.start_suite("Sun Position Accuracy Tests");
    
    // Test sun rise/set times against known data
    struct {
        double lat, lon, timezone;
        int year, month, day;
        int expected_sunrise, expected_sunset;
        const char* location;
    } sun_data[] = {
        {37.7749, -122.4194, -8, 2025, 1, 15, time_to_minutes(7,25), time_to_minutes(17,20), "San Francisco"},
        {13.7563, 100.5018, +7, 2025, 9, 11, time_to_minutes(5,53), time_to_minutes(18,8), "Bangkok"},
        {51.5074, -0.1278, +1, 2025, 6, 21, time_to_minutes(4,43), time_to_minutes(21,21), "London summer"},
        {-33.8688, 151.2093, +11, 2025, 1, 15, time_to_minutes(5,45), time_to_minutes(20,4), "Sydney summer"}
    };
    
    for (const auto& sd : sun_data) {
        AstronomicalCalculator calc(sd.lat, sd.lon, sd.timezone);
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

int main() {
    cout << "=== ASTRONOMICAL CALCULATOR TEST WITH PROPER TIMEZONES ===" << endl;
    
    test_sun_position_accuracy();
    
    results.print_final_summary("Astronomical Calculator");
    
    return results.get_exit_code();
}
