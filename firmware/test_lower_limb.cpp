#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"

using namespace std;

int main() {
    cout << "=== TESTING LOWER LIMB THEORY ===\n" << endl;
    
    // Bangkok coordinates
    AstronomicalCalculator calc(13.7563, 100.5018);
    AstronomicalCalculator::DateTime dt(2025, 9, 11, 20, 46, 0);
    
    // At geometric horizon crossing (20:46)
    auto pos = calc.calculate_moon_position(dt);
    cout << "At 20:46 (geometric horizon crossing):" << endl;
    cout << "Moon center altitude: " << fixed << setprecision(3) << pos.altitude << "°" << endl;
    cout << "Moon upper limb: " << (pos.altitude + 0.258) << "°" << endl;
    cout << "Moon lower limb: " << (pos.altitude - 0.258) << "°" << endl;
    
    // Now check at 21:04
    dt.hour = 21;
    dt.minute = 4;
    pos = calc.calculate_moon_position(dt);
    cout << "\nAt 21:04 (timeanddate.com moonrise):" << endl;
    cout << "Moon center altitude: " << pos.altitude << "°" << endl;
    cout << "Moon upper limb: " << (pos.altitude + 0.258) << "°" << endl;
    cout << "Moon lower limb: " << (pos.altitude - 0.258) << "°" << endl;
    
    cout << "\n=== THEORY CHECK ===" << endl;
    cout << "If moonrise is when lower limb appears at horizon with refraction:" << endl;
    cout << "Moon center should be at: +0.258° (semi-diameter) +0.567° (refraction) = +0.825°" << endl;
    
    // Find when moon center is at +0.825°
    cout << "\nSearching for when moon center is at +0.825°..." << endl;
    for (int minutes = 20*60+30; minutes <= 21*60+10; minutes++) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        pos = calc.calculate_moon_position(dt);
        
        if (fabs(pos.altitude - 0.825) < 0.05) {
            cout << "Found at " << setfill('0') << setw(2) << dt.hour 
                 << ":" << setw(2) << dt.minute 
                 << " (altitude: " << pos.altitude << "°)" << endl;
            cout << "This is " << (minutes - (21*60+4)) << " minutes from 21:04" << endl;
            break;
        }
    }
    
    // Alternative: maybe they use apparent horizon (with refraction) but lower limb
    cout << "\nAlternative: lower limb at refracted horizon" << endl;
    cout << "Moon center should be at: +0.258° (semi-diameter) = +0.258°" << endl;
    
    // Find when moon center is at +0.258°
    for (int minutes = 20*60+30; minutes <= 21*60+10; minutes++) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        pos = calc.calculate_moon_position(dt);
        
        if (fabs(pos.altitude - 0.258) < 0.05) {
            cout << "Found at " << setfill('0') << setw(2) << dt.hour 
                 << ":" << setw(2) << dt.minute 
                 << " (altitude: " << pos.altitude << "°)" << endl;
            cout << "This is " << (minutes - (21*60+4)) << " minutes from 21:04" << endl;
            break;
        }
    }
    
    return 0;
}
