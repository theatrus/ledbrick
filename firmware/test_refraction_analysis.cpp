/**
 * Test to analyze refraction and limb corrections
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include "astronomical_calculator.h"

using namespace std;

// Standard atmospheric refraction at horizon
const double REFRACTION_AT_HORIZON = 34.0 / 60.0;  // 34 arcminutes = 0.5667 degrees

// Sun and Moon semi-diameters
const double SUN_SEMI_DIAMETER = 16.0 / 60.0;     // 16 arcminutes = 0.2667 degrees  
const double MOON_SEMI_DIAMETER = 15.5 / 60.0;    // 15.5 arcminutes = 0.2583 degrees

int main() {
    cout << "=== REFRACTION AND LIMB CORRECTION ANALYSIS ===" << endl;
    cout << "Checking what corrections are applied in rise/set calculations" << endl;
    cout << endl;
    
    // Bangkok coordinates
    AstronomicalCalculator calc(13.7563, 100.5018);
    AstronomicalCalculator::DateTime dt(2025, 9, 11, 0, 0, 0);
    
    cout << "Standard corrections for rise/set calculations:" << endl;
    cout << "- Atmospheric refraction at horizon: " << REFRACTION_AT_HORIZON << "°" << endl;
    cout << "- Sun semi-diameter: " << SUN_SEMI_DIAMETER << "°" << endl;
    cout << "- Moon semi-diameter: " << MOON_SEMI_DIAMETER << "°" << endl;
    cout << "- Total correction for upper limb: " 
         << (REFRACTION_AT_HORIZON + MOON_SEMI_DIAMETER) << "°" << endl;
    cout << endl;
    
    // For rise/set, the upper limb should cross altitude = -(refraction + semi-diameter)
    double rise_set_altitude = -(REFRACTION_AT_HORIZON + MOON_SEMI_DIAMETER);
    cout << "Moon should 'rise' when center is at altitude: " << rise_set_altitude << "°" << endl;
    cout << endl;
    
    // Scan around expected moonrise time to see what altitude triggers rise
    cout << "Scanning moon altitude around expected rise time (21:04):" << endl;
    cout << "Time   Altitude   Notes" << endl;
    cout << "-----  ---------  -----" << endl;
    
    for (int hour = 20; hour <= 21; hour++) {
        for (int min = 0; min < 60; min += 5) {
            dt.hour = hour;
            dt.minute = min;
            
            auto pos = calc.calculate_moon_position(dt);
            
            cout << setfill('0') << setw(2) << hour << ":" << setw(2) << min
                 << "  " << setw(8) << fixed << setprecision(3) << pos.altitude << "°";
            
            // Check if near standard rise altitude
            if (pos.altitude >= rise_set_altitude - 0.1 && 
                pos.altitude <= rise_set_altitude + 0.1) {
                cout << "  <-- Near expected rise altitude";
            } else if (pos.altitude >= -0.1 && pos.altitude <= 0.1) {
                cout << "  <-- Near geometric horizon";
            } else if (pos.altitude >= -REFRACTION_AT_HORIZON - 0.1 && 
                       pos.altitude <= -REFRACTION_AT_HORIZON + 0.1) {
                cout << "  <-- Near refracted horizon";
            }
            
            cout << endl;
        }
    }
    
    // Check our detected rise time
    auto moon_times = calc.get_moon_rise_set_times(dt);
    cout << endl << "Our algorithm detects moonrise at: ";
    if (moon_times.rise_valid) {
        cout << setfill('0') << setw(2) << moon_times.rise_minutes / 60 
             << ":" << setw(2) << moon_times.rise_minutes % 60 << endl;
    } else {
        cout << "Not found" << endl;
    }
    
    cout << "Expected (timeanddate.com): 21:04" << endl;
    cout << endl;
    
    // Calculate the correction we're missing
    if (moon_times.rise_valid) {
        dt.hour = moon_times.rise_minutes / 60;
        dt.minute = moon_times.rise_minutes % 60;
        auto pos_at_rise = calc.calculate_moon_position(dt);
        
        cout << "Altitude at our detected rise: " << pos_at_rise.altitude << "°" << endl;
        cout << "This suggests we're detecting rise when moon crosses: " 
             << pos_at_rise.altitude << "°" << endl;
        
        double missing_correction = rise_set_altitude - pos_at_rise.altitude;
        cout << "Missing correction: " << missing_correction << "° ("
             << (missing_correction * 60) << " arcminutes)" << endl;
    }
    
    // Also check parallax
    cout << endl << "Moon parallax analysis:" << endl;
    cout << "The moon's parallax can be up to 1° (varies with distance)" << endl;
    cout << "This would make the moon appear lower than calculated" << endl;
    cout << "and could account for some of the timing difference." << endl;
    
    // Time difference analysis
    cout << endl << "Timing error analysis:" << endl;
    cout << "19 minute error at moonrise corresponds to:" << endl;
    
    // Moon moves about 13 degrees per day = 0.54 degrees per hour = 0.009 degrees per minute
    double moon_motion_per_minute = 13.0 / (24 * 60);
    double altitude_difference = 19 * moon_motion_per_minute * cos(13.7563 * M_PI / 180.0);
    
    cout << "- Altitude difference: ~" << fixed << setprecision(2) 
         << altitude_difference << "° (" << (altitude_difference * 60) << " arcminutes)" << endl;
    cout << "- This matches well with moon semi-diameter + small corrections" << endl;
    
    return 0;
}