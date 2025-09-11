#pragma once

#include <cmath>
#include <cstdint>

/**
 * Standalone Astronomical Calculator
 * Pure C++ implementation with no external dependencies
 * Handles sun/moon position, intensity, and time projection calculations
 */
class AstronomicalCalculator {
public:
    struct DateTime {
        int year;
        int month;
        int day;
        int hour;
        int minute;
        int second;
        
        DateTime(int y = 2025, int m = 1, int d = 1, int h = 12, int min = 0, int s = 0)
            : year(y), month(m), day(d), hour(h), minute(min), second(s) {}
    };
    
    struct CelestialPosition {
        double altitude;   // Degrees above horizon (-90 to +90)
        double azimuth;    // Degrees from north (0-360)
        
        CelestialPosition(double alt = -90.0, double az = 0.0)
            : altitude(alt), azimuth(az) {}
    };
    
    struct MoonTimes {
        bool rise_valid = false;
        bool set_valid = false;
        uint16_t rise_minutes = 0;  // Minutes from midnight
        uint16_t set_minutes = 0;   // Minutes from midnight
    };
    
    struct SunTimes {
        bool rise_valid = false;
        bool set_valid = false;
        uint16_t rise_minutes = 0;  // Minutes from midnight
        uint16_t set_minutes = 0;   // Minutes from midnight
    };
    
    // Constructor
    AstronomicalCalculator(double latitude = 37.7749, double longitude = -122.4194);
    
    // Configuration
    void set_location(double latitude, double longitude);
    void set_projection_settings(bool enabled, int shift_hours = 0, int shift_minutes = 0);
    void set_timezone_offset(double hours_from_utc) { timezone_offset_hours_ = hours_from_utc; }
    
    // Core astronomical calculations
    double calculate_julian_day(const DateTime& dt) const;
    float get_moon_phase(const DateTime& dt) const;
    
    // Position calculations
    CelestialPosition calculate_sun_position(const DateTime& dt) const;
    CelestialPosition calculate_moon_position(const DateTime& dt) const;
    CelestialPosition calculate_sun_position_at_time(double julian_day) const;
    CelestialPosition calculate_moon_position_at_time(double julian_day) const;
    
    // Intensity calculations
    float get_sun_intensity(const DateTime& dt) const;
    float get_moon_intensity(const DateTime& dt) const;
    float get_projected_sun_intensity(const DateTime& dt) const;
    float get_projected_moon_intensity(const DateTime& dt) const;
    
    // Rise/set calculations
    MoonTimes get_moon_rise_set_times(const DateTime& dt) const;
    SunTimes get_sun_rise_set_times(const DateTime& dt) const;
    
    // Projected rise/set calculations (with time shift mapping)
    SunTimes get_projected_sun_rise_set_times(const DateTime& dt) const;
    MoonTimes get_projected_moon_rise_set_times(const DateTime& dt) const;
    
    // Helper functions
    double get_projected_julian_day(const DateTime& dt) const;
    
private:
    // Location settings
    double latitude_;
    double longitude_;
    
    // Projection settings
    bool projection_enabled_;
    int time_shift_hours_;
    int time_shift_minutes_;
    
    // Timezone settings
    double timezone_offset_hours_;
    
    // Internal calculation helpers
    float calculate_sun_intensity_from_position(const CelestialPosition& pos) const;
    float calculate_moon_intensity_from_position(const CelestialPosition& pos, float phase) const;
};