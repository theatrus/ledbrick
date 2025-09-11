#include <iostream>
#include <iomanip>
#include "astronomical_calculator.h"

using namespace std;

int main() {
    // Bangkok coordinates
    AstronomicalCalculator calc(13.7563, 100.5018);
    
    cout << "=== BANGKOK MOON TIMING IMPROVEMENT ===\n" << endl;
    
    // Test Sep 11, 2025
    AstronomicalCalculator::DateTime dt(2025, 9, 11, 12, 0, 0);
    auto times = calc.get_moon_rise_set_times(dt);
    
    cout << "September 11, 2025 Bangkok:" << endl;
    cout << "Expected moonrise: 21:04" << endl;
    cout << "Our calculation:   " << setfill('0') << setw(2) << times.rise_minutes / 60 
         << ":" << setw(2) << times.rise_minutes % 60 << endl;
    
    int error = times.rise_minutes - (21 * 60 + 4);
    cout << "Error: " << error << " minutes ";
    if (error < 0) cout << "(early)"; else cout << "(late)";
    cout << endl;
    
    cout << "\nExpected moonset:  09:12" << endl;
    cout << "Our calculation:   " << setfill('0') << setw(2) << times.set_minutes / 60 
         << ":" << setw(2) << times.set_minutes % 60 << endl;
    
    int set_error = times.set_minutes - (9 * 60 + 12);
    cout << "Error: " << set_error << " minutes ";
    if (set_error < 0) cout << "(early)"; else cout << "(late)";
    cout << endl;
    
    cout << "\n=== IMPROVEMENT SUMMARY ===" << endl;
    cout << "Before refraction fix: moonrise was 19 minutes early" << endl;
    cout << "After refraction fix:  moonrise is " << abs(error) << " minutes ";
    if (error < 0) cout << "early"; else cout << "late";
    cout << endl;
    cout << "Improvement: " << (19 - abs(error)) << " minutes better accuracy\!" << endl;
    
    // Check what altitude we're detecting at
    dt.hour = times.rise_minutes / 60;
    dt.minute = times.rise_minutes % 60;
    auto pos = calc.calculate_moon_position(dt);
    cout << "\nMoon altitude at detected rise: " << fixed << setprecision(3) 
         << pos.altitude << "°" << endl;
    cout << "Target altitude (with refraction): -0.825°" << endl;
    cout << "Difference: " << (pos.altitude - (-0.825)) << "°" << endl;
    
    return 0;
}
