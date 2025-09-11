#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"

using namespace std;

int main() {
    // Bangkok coordinates
    AstronomicalCalculator calc(13.7563, 100.5018);
    AstronomicalCalculator::DateTime dt(2025, 9, 11, 0, 0, 0);
    
    cout << "=== ALTITUDE SCAN FOR BANGKOK MOONRISE ===\n" << endl;
    cout << "Scanning around expected time 21:04..." << endl;
    cout << "Time   Altitude  Notes" << endl;
    cout << "-----  --------  -----" << endl;
    
    // Check from 20:30 to 21:30
    for (int minutes = 20*60+30; minutes <= 21*60+30; minutes += 2) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        
        auto pos = calc.calculate_moon_position(dt);
        
        cout << setfill('0') << setw(2) << dt.hour << ":" << setw(2) << dt.minute
             << "  " << fixed << setprecision(3) << setw(8) << pos.altitude << "°";
        
        // Check for different possible thresholds
        if (pos.altitude >= -0.825 - 0.05 && pos.altitude <= -0.825 + 0.05) {
            cout << " <- Near -0.825° (our threshold)";
        } else if (pos.altitude >= -0.567 - 0.05 && pos.altitude <= -0.567 + 0.05) {
            cout << " <- Near -0.567° (refraction only)";
        } else if (pos.altitude >= -0.25 - 0.05 && pos.altitude <= -0.25 + 0.05) {
            cout << " <- Near -0.25° (semi-diameter only)";
        } else if (pos.altitude >= -0.05 && pos.altitude <= 0.05) {
            cout << " <- Near 0° (geometric)";
        }
        
        cout << endl;
    }
    
    cout << "\n=== STANDARD VALUES ===" << endl;
    cout << "Refraction: 34' = 0.567°" << endl;
    cout << "Moon semi-diameter: 15.5' = 0.258°" << endl;
    cout << "Total: 49.5' = 0.825°" << endl;
    
    cout << "\n=== ANALYSIS ===" << endl;
    cout << "Expected moonrise at 21:04 corresponds to altitude around -0.25°" << endl;
    cout << "This suggests timeanddate.com may be using semi-diameter correction only" << endl;
    cout << "or a different refraction model." << endl;
    
    return 0;
}
