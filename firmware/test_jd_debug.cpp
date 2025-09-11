#include <iostream>
#include <iomanip>
#include "astronomical_calculator.h"

using namespace std;

int main() {
    cout << "=== JULIAN DAY CALCULATION DEBUG ===" << endl;
    
    // Create calculator with 0 longitude to avoid timezone issues
    AstronomicalCalculator calc(0, 0);
    
    // Test known Julian dates
    struct TestCase {
        int year, month, day, hour, minute, second;
        double expected_jd;
        const char* description;
    };
    
    TestCase cases[] = {
        {2000, 1, 1, 12, 0, 0, 2451545.0, "J2000 epoch"},
        {2025, 1, 1, 0, 0, 0, 2460676.5, "2025 New Year midnight UTC"},
        {2025, 9, 11, 0, 0, 0, 2460929.5, "2025-09-11 midnight UTC"},
        {2024, 9, 11, 0, 0, 0, 2460564.5, "2024-09-11 midnight UTC"},
        {2025, 9, 11, 12, 0, 0, 2460930.0, "2025-09-11 noon UTC"}
    };
    
    for (const auto& tc : cases) {
        AstronomicalCalculator::DateTime dt(tc.year, tc.month, tc.day, tc.hour, tc.minute, tc.second);
        double jd = calc.calculate_julian_day(dt);
        double diff = jd - tc.expected_jd;
        
        cout << tc.description << ":" << endl;
        cout << "  Expected: " << fixed << setprecision(1) << tc.expected_jd << endl;
        cout << "  Got:      " << jd << endl;
        cout << "  Error:    " << diff << " days" << endl;
        cout << endl;
    }
    
    // Now test with timezone offset
    cout << "=== TIMEZONE IMPACT ===" << endl;
    
    // San Francisco at longitude -122.4194
    AstronomicalCalculator sf_calc(37.7749, -122.4194);
    AstronomicalCalculator::DateTime sf_time(2025, 1, 15, 7, 25, 0);  // 7:25 AM local
    
    cout << "San Francisco sunrise Jan 15, 2025 at 7:25 AM PST:" << endl;
    cout << "Longitude: -122.4194°" << endl;
    cout << "Calculated timezone offset: " << (-122.4194 / 15.0) << " hours" << endl;
    cout << "Actual timezone offset: -8 hours (PST)" << endl;
    cout << "Difference: " << ((-122.4194 / 15.0) - (-8)) << " hours = " 
         << (((-122.4194 / 15.0) - (-8)) * 60) << " minutes" << endl;
    
    return 0;
}
