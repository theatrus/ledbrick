#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"

using namespace std;

int main() {
    // Bangkok coordinates
    AstronomicalCalculator calc(13.7563, 100.5018);
    AstronomicalCalculator::DateTime dt(2025, 9, 11, 0, 0, 0);
    
    cout << "=== FINDING ALTITUDE CROSSING FOR 21:04 ===\n" << endl;
    
    // Scan backwards from 21:04 to find when moon was at various key altitudes
    double target_alt = 4.061;  // Altitude at 21:04
    
    cout << "Working backwards from 21:04 (altitude " << target_alt << "°)..." << endl;
    cout << "\nTime   Altitude  Time Diff from 21:04" << endl;
    cout << "-----  --------  -------------------" << endl;
    
    for (int minutes = 21*60+4; minutes >= 19*60; minutes -= 2) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        
        auto pos = calc.calculate_moon_position(dt);
        int diff = (21*60+4) - minutes;
        
        cout << setfill('0') << setw(2) << dt.hour << ":" << setw(2) << dt.minute
             << "  " << fixed << setprecision(3) << setw(8) << pos.altitude << "°"
             << "  -" << diff << " min";
        
        // Check key altitudes
        if (fabs(pos.altitude - 0.0) < 0.1) {
            cout << " <- Geometric horizon (0°)";
        } else if (fabs(pos.altitude - (-0.258)) < 0.1) {
            cout << " <- Moon semi-diameter";
        } else if (fabs(pos.altitude - (-0.567)) < 0.1) {
            cout << " <- Refraction only";
        } else if (fabs(pos.altitude - (-0.825)) < 0.1) {
            cout << " <- Our threshold";
        }
        
        cout << endl;
    }
    
    // Calculate moon's angular velocity at this time
    dt.hour = 21;
    dt.minute = 0;
    auto pos1 = calc.calculate_moon_position(dt);
    dt.minute = 10;
    auto pos2 = calc.calculate_moon_position(dt);
    
    double angular_velocity = (pos2.altitude - pos1.altitude) / 10.0;  // degrees per minute
    
    cout << "\n=== ANALYSIS ===" << endl;
    cout << "Moon's vertical angular velocity: " << angular_velocity << "° per minute" << endl;
    cout << "Time for moon to rise from -0.825° to 0°: " << (0.825 / angular_velocity) << " minutes" << endl;
    cout << "Time for moon to rise from 0° to 4.061°: " << (4.061 / angular_velocity) << " minutes" << endl;
    
    return 0;
}
