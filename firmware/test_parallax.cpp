#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"

using namespace std;

int main() {
    cout << "=== MOON PARALLAX INVESTIGATION ===\n" << endl;
    
    // Moon's horizontal parallax varies from about 54' to 61' (0.9° to 1.02°)
    // This is the maximum parallax when moon is at horizon
    double typical_parallax = 57.0 / 60.0;  // 57 arcminutes = 0.95°
    
    cout << "Moon's typical horizontal parallax: " << typical_parallax << "°" << endl;
    cout << "This makes the moon appear LOWER than calculated (topocentric vs geocentric)" << endl;
    
    // Bangkok coordinates
    AstronomicalCalculator calc(13.7563, 100.5018);
    AstronomicalCalculator::DateTime dt(2025, 9, 11, 0, 0, 0);
    
    cout << "\nIf we account for parallax:" << endl;
    cout << "Apparent moonrise occurs when geocentric altitude = parallax" << endl;
    cout << "So moon appears at horizon when center is at +" << typical_parallax << "°" << endl;
    
    // Find when moon is at this altitude
    cout << "\nSearching for moon at +" << typical_parallax << "°..." << endl;
    for (int minutes = 20*60+40; minutes <= 21*60+10; minutes++) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        auto pos = calc.calculate_moon_position(dt);
        
        if (fabs(pos.altitude - typical_parallax) < 0.05) {
            cout << "Found at " << setfill('0') << setw(2) << dt.hour 
                 << ":" << setw(2) << dt.minute 
                 << " (altitude: " << pos.altitude << "°)" << endl;
            cout << "This is " << ((21*60+4) - minutes) << " minutes before 21:04" << endl;
            break;
        }
    }
    
    cout << "\n=== COMBINED CORRECTIONS ===" << endl;
    cout << "For moonrise (upper limb at horizon):" << endl;
    cout << "1. Refraction raises apparent position by 0.567°" << endl;
    cout << "2. Parallax lowers apparent position by ~0.95°" << endl;
    cout << "3. Semi-diameter adds 0.258° for upper limb" << endl;
    cout << "Net correction: 0.567 - 0.95 + 0.258 = " << (0.567 - 0.95 + 0.258) << "°" << endl;
    
    double net_correction = 0.567 - typical_parallax + 0.258;
    cout << "\nMoonrise when geocentric altitude = " << net_correction << "°" << endl;
    
    // Find this altitude
    for (int minutes = 19*60; minutes <= 21*60+10; minutes++) {
        dt.hour = minutes / 60;
        dt.minute = minutes % 60;
        auto pos = calc.calculate_moon_position(dt);
        
        if (pos.altitude >= net_correction && pos.altitude < net_correction + 0.1) {
            cout << "Found at " << setfill('0') << setw(2) << dt.hour 
                 << ":" << setw(2) << dt.minute 
                 << " (altitude: " << pos.altitude << "°)" << endl;
            int diff = (21*60+4) - minutes;
            cout << "This is ";
            if (diff > 0) cout << diff << " minutes before";
            else cout << -diff << " minutes after";
            cout << " 21:04" << endl;
            
            if (abs(diff) < 5) {
                cout << "\n*** THIS MATCHES\! ***" << endl;
                cout << "Timeanddate.com appears to include parallax correction\!" << endl;
            }
            break;
        }
    }
    
    return 0;
}
