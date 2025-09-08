#include "astronomical_calculator.h"
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AstronomicalCalculator::AstronomicalCalculator(double latitude, double longitude)
    : latitude_(latitude), longitude_(longitude), 
      projection_enabled_(false), time_shift_hours_(0), time_shift_minutes_(0),
      timezone_offset_hours_(longitude / 15.0) {  // Default to longitude-based timezone
}

void AstronomicalCalculator::set_location(double latitude, double longitude) {
    latitude_ = latitude;
    longitude_ = longitude;
}

void AstronomicalCalculator::set_projection_settings(bool enabled, int shift_hours, int shift_minutes) {
    projection_enabled_ = enabled;
    time_shift_hours_ = shift_hours;
    time_shift_minutes_ = shift_minutes;
}

double AstronomicalCalculator::calculate_julian_day(const DateTime& dt) const {
    int year = dt.year;
    int month = dt.month;
    int day = dt.day;
    
    // Adjust for January/February
    if (month <= 2) {
        year--;
        month += 12;
    }
    
    // Julian Day calculation (Gregorian calendar)
    int a = year / 100;
    int b = 2 - a + (a / 4);
    double jd = static_cast<double>(static_cast<int>(365.25 * (year + 4716)) + 
                                    static_cast<int>(30.6001 * (month + 1)) + 
                                    day + b - 1524.5);
    
    // Add time of day
    jd += (dt.hour + dt.minute / 60.0 + dt.second / 3600.0) / 24.0;
    
    // Convert local time to UTC for astronomical calculations
    jd -= timezone_offset_hours_ / 24.0;
    
    return jd;
}

float AstronomicalCalculator::get_moon_phase(const DateTime& dt) const {
    double jd = calculate_julian_day(dt);
    
    // Days since J2000.0 (January 1, 2000, 12:00 TT)
    double days_since_j2000 = jd - 2451545.0;
    
    // Mean longitude of the Moon
    double moon_longitude = fmod(218.316 + 13.176396 * days_since_j2000, 360.0);
    if (moon_longitude < 0) moon_longitude += 360.0;
    
    // Mean longitude of the Sun  
    double sun_longitude = fmod(280.459 + 0.98564736 * days_since_j2000, 360.0);
    if (sun_longitude < 0) sun_longitude += 360.0;
    
    // Moon's elongation from the Sun
    double elongation = moon_longitude - sun_longitude;
    if (elongation < 0) elongation += 360.0;
    
    // Convert to phase (0.0 = new moon, 0.5 = full moon, 1.0 = new moon again)
    double phase = elongation / 360.0;
    
    return static_cast<float>(phase);
}

AstronomicalCalculator::CelestialPosition AstronomicalCalculator::calculate_sun_position(const DateTime& dt) const {
    double jd = calculate_julian_day(dt);
    return calculate_sun_position_at_time(jd);
}

AstronomicalCalculator::CelestialPosition AstronomicalCalculator::calculate_moon_position(const DateTime& dt) const {
    double jd = calculate_julian_day(dt);
    return calculate_moon_position_at_time(jd);
}

AstronomicalCalculator::CelestialPosition AstronomicalCalculator::calculate_sun_position_at_time(double julian_day) const {
    // Days since J2000.0
    double n = julian_day - 2451545.0;
    
    // Mean longitude of Sun
    double L = fmod(280.460 + 0.98564736 * n, 360.0);
    if (L < 0) L += 360.0;
    
    // Mean anomaly of Sun
    double g = fmod(357.528 + 0.98560028 * n, 360.0);
    if (g < 0) g += 360.0;
    double g_rad = g * M_PI / 180.0;
    
    // Ecliptic longitude of Sun
    double lambda = L + 1.915 * sin(g_rad) + 0.020 * sin(2.0 * g_rad);
    lambda = fmod(lambda, 360.0);
    if (lambda < 0) lambda += 360.0;
    double lambda_rad = lambda * M_PI / 180.0;
    
    // Obliquity of ecliptic (23.439 degrees for J2000)
    double epsilon = 23.439 * M_PI / 180.0;
    
    // Right ascension and declination
    double alpha = atan2(cos(epsilon) * sin(lambda_rad), cos(lambda_rad));
    if (alpha < 0) alpha += 2.0 * M_PI;
    double delta = asin(sin(epsilon) * sin(lambda_rad));
    
    // Greenwich Mean Sidereal Time (GMST)
    double t = n / 36525.0;  // Julian centuries since J2000
    double gmst = fmod(280.46061837 + 360.98564736629 * n + 0.000387933 * t * t, 360.0);
    if (gmst < 0) gmst += 360.0;
    
    // Local Mean Sidereal Time (LMST)
    double lmst = gmst + longitude_;
    lmst = fmod(lmst, 360.0);
    if (lmst < 0) lmst += 360.0;
    double lmst_rad = lmst * M_PI / 180.0;
    
    // Local Hour Angle
    double H = lmst_rad - alpha;
    
    // Convert coordinates to radians
    double lat_rad = latitude_ * M_PI / 180.0;
    
    // Calculate altitude and azimuth using spherical trigonometry
    CelestialPosition pos;
    
    // Altitude
    pos.altitude = asin(sin(lat_rad) * sin(delta) + cos(lat_rad) * cos(delta) * cos(H));
    pos.altitude = pos.altitude * 180.0 / M_PI;
    
    // Azimuth (measured from north, clockwise)
    double y = sin(H);
    double x = cos(H) * sin(lat_rad) - tan(delta) * cos(lat_rad);
    double azimuth = atan2(y, x) * 180.0 / M_PI;
    pos.azimuth = fmod(azimuth + 180.0, 360.0);  // Convert to 0-360 from north
    
    return pos;
}

AstronomicalCalculator::CelestialPosition AstronomicalCalculator::calculate_moon_position_at_time(double julian_day) const {
    // Days since J2000.0
    double d = julian_day - 2451545.0;
    
    // Calculate Moon's position
    double L = fmod(218.316 + 13.176396 * d, 360.0);
    if (L < 0) L += 360.0;
    
    double M = fmod(134.963 + 13.064993 * d, 360.0);
    if (M < 0) M += 360.0;
    M = M * M_PI / 180.0;
    
    // Moon's longitude with perturbation
    double moon_lon = L + 6.289 * sin(M);
    moon_lon = fmod(moon_lon, 360.0);
    if (moon_lon < 0) moon_lon += 360.0;
    moon_lon = moon_lon * M_PI / 180.0;
    
    // Moon's latitude
    double moon_lat = 5.128 * sin(fmod(93.272 + 13.229350 * d, 360.0) * M_PI / 180.0);
    moon_lat = moon_lat * M_PI / 180.0;
    
    // Convert to equatorial coordinates
    double ecliptic_obliquity = 23.439 * M_PI / 180.0;
    
    double alpha = atan2(sin(moon_lon) * cos(ecliptic_obliquity) - tan(moon_lat) * sin(ecliptic_obliquity), 
                        cos(moon_lon));
    double delta = asin(sin(moon_lat) * cos(ecliptic_obliquity) + 
                       cos(moon_lat) * sin(ecliptic_obliquity) * sin(moon_lon));
    
    // Greenwich Sidereal Time at 0h UT
    double gst0 = fmod(280.46061837 + 36525.0 * 360.98564736629 * d / 36525.0, 360.0);
    if (gst0 < 0) gst0 += 360.0;
    
    // Local sidereal time (accounting for longitude and time of day)
    double fractional_day = fmod(julian_day, 1.0);  // Time of day as fraction
    double ut_hours = fractional_day * 24.0;
    double lst = gst0 + longitude_ + 15.0 * ut_hours;
    lst = fmod(lst, 360.0);
    if (lst < 0) lst += 360.0;
    lst = lst * M_PI / 180.0;
    
    // Convert to radians
    double lat_rad = latitude_ * M_PI / 180.0;
    
    // Local Hour Angle
    double H = lst - alpha;
    
    // Calculate altitude and azimuth
    CelestialPosition pos;
    pos.altitude = asin(sin(lat_rad) * sin(delta) + cos(lat_rad) * cos(delta) * cos(H));
    pos.altitude = pos.altitude * 180.0 / M_PI;
    
    double azimuth = atan2(sin(H), cos(H) * sin(lat_rad) - tan(delta) * cos(lat_rad));
    pos.azimuth = fmod(azimuth * 180.0 / M_PI + 180.0, 360.0);
    
    return pos;
}

float AstronomicalCalculator::get_sun_intensity(const DateTime& dt) const {
    auto pos = calculate_sun_position(dt);
    return calculate_sun_intensity_from_position(pos);
}

float AstronomicalCalculator::get_moon_intensity(const DateTime& dt) const {
    auto pos = calculate_moon_position(dt);
    float phase = get_moon_phase(dt);
    return calculate_moon_intensity_from_position(pos, phase);
}

float AstronomicalCalculator::get_projected_sun_intensity(const DateTime& dt) const {
    if (!projection_enabled_) {
        return get_sun_intensity(dt);
    }
    
    double projected_jd = get_projected_julian_day(dt);
    auto pos = calculate_sun_position_at_time(projected_jd);
    return calculate_sun_intensity_from_position(pos);
}

float AstronomicalCalculator::get_projected_moon_intensity(const DateTime& dt) const {
    if (!projection_enabled_) {
        return get_moon_intensity(dt);
    }
    
    double projected_jd = get_projected_julian_day(dt);
    auto pos = calculate_moon_position_at_time(projected_jd);
    float phase = get_moon_phase(dt);  // Use current phase, not projected
    return calculate_moon_intensity_from_position(pos, phase);
}

AstronomicalCalculator::MoonTimes AstronomicalCalculator::get_moon_rise_set_times(const DateTime& dt) const {
    MoonTimes result;
    
    // Calculate Julian Day for start of day
    DateTime day_start(dt.year, dt.month, dt.day, 0, 0, 0);
    double jd_base = calculate_julian_day(day_start);
    
    double prev_altitude = -90.0;
    bool found_rise = false;
    bool found_set = false;
    
    // Check every 15 minutes throughout the day
    for (int minutes = 0; minutes < 1440 && (!found_rise || !found_set); minutes += 15) {
        int hours = minutes / 60;
        int mins = minutes % 60;
        
        // Calculate Julian Day for this specific time
        double jd = jd_base + (hours + mins / 60.0) / 24.0;
        
        // Use the position calculator
        auto pos = calculate_moon_position_at_time(jd);
        double altitude = pos.altitude;
        
        // Check for horizon crossings (0 degrees altitude)
        if (prev_altitude != -90.0) {  // Skip first iteration
            if (prev_altitude < 0.0 && altitude >= 0.0 && !found_rise) {
                // Moon rise detected
                result.rise_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
                if (result.rise_minutes < 0) result.rise_minutes += 1440;
                result.rise_valid = true;
                found_rise = true;
            }
            else if (prev_altitude >= 0.0 && altitude < 0.0 && !found_set) {
                // Moon set detected  
                result.set_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
                if (result.set_minutes < 0) result.set_minutes += 1440;
                result.set_valid = true;
                found_set = true;
            }
        }
        
        prev_altitude = altitude;
    }
    
    return result;
}

AstronomicalCalculator::SunTimes AstronomicalCalculator::get_sun_rise_set_times(const DateTime& dt) const {
    SunTimes result;
    
    // Calculate Julian Day for start of day
    DateTime day_start(dt.year, dt.month, dt.day, 0, 0, 0);
    double jd_base = calculate_julian_day(day_start);
    
    double prev_altitude = -90.0;
    bool found_rise = false;
    bool found_set = false;
    
    // Check every 15 minutes throughout the day
    for (int minutes = 0; minutes < 1440 && (!found_rise || !found_set); minutes += 15) {
        int hours = minutes / 60;
        int mins = minutes % 60;
        
        // Calculate Julian Day for this specific time
        double jd = jd_base + (hours + mins / 60.0) / 24.0;
        
        // Use the position calculator
        auto pos = calculate_sun_position_at_time(jd);
        double altitude = pos.altitude;
        
        // Check for horizon crossings (0 degrees altitude)
        if (prev_altitude != -90.0) {  // Skip first iteration
            if (prev_altitude < 0.0 && altitude >= 0.0 && !found_rise) {
                // Sun rise detected
                result.rise_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
                if (result.rise_minutes < 0) result.rise_minutes += 1440;
                result.rise_valid = true;
                found_rise = true;
            }
            else if (prev_altitude >= 0.0 && altitude < 0.0 && !found_set) {
                // Sun set detected  
                result.set_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
                if (result.set_minutes < 0) result.set_minutes += 1440;
                result.set_valid = true;
                found_set = true;
            }
        }
        
        prev_altitude = altitude;
    }
    
    return result;
}

AstronomicalCalculator::SunTimes AstronomicalCalculator::get_projected_sun_rise_set_times(const DateTime& dt) const {
    if (!projection_enabled_) {
        return get_sun_rise_set_times(dt);
    }
    
    // Calculate the actual sun rise/set times first
    SunTimes actual_times = get_sun_rise_set_times(dt);
    
    // Apply the time shift to the results
    SunTimes projected_times = actual_times;
    
    if (actual_times.rise_valid) {
        // Add the time shift (convert to total minutes)
        int shift_minutes = time_shift_hours_ * 60 + time_shift_minutes_;
        projected_times.rise_minutes = (actual_times.rise_minutes + shift_minutes) % 1440;
        if (projected_times.rise_minutes < 0) projected_times.rise_minutes += 1440;
    }
    
    if (actual_times.set_valid) {
        // Add the time shift (convert to total minutes) 
        int shift_minutes = time_shift_hours_ * 60 + time_shift_minutes_;
        projected_times.set_minutes = (actual_times.set_minutes + shift_minutes) % 1440;
        if (projected_times.set_minutes < 0) projected_times.set_minutes += 1440;
    }
    
    return projected_times;
}

double AstronomicalCalculator::get_projected_julian_day(const DateTime& dt) const {
    double jd = calculate_julian_day(dt);
    
    if (!projection_enabled_) {
        return jd;
    }
    
    // Apply time shift
    double shift_hours = time_shift_hours_ + time_shift_minutes_ / 60.0;
    jd += shift_hours / 24.0;  // Convert hours to days
    
    // Calculate timezone offset between our local time and the target location
    // Rough approximation: 15 degrees longitude = 1 hour time difference
    double longitude_offset_hours = longitude_ / 15.0;  // Target location offset from GMT
    
    // Apply the longitude-based time offset to synchronize local solar times
    jd -= longitude_offset_hours / 24.0;
    
    return jd;
}

float AstronomicalCalculator::calculate_sun_intensity_from_position(const CelestialPosition& pos) const {
    // Handle different sun positions with atmospheric effects
    if (pos.altitude <= -6.0) {
        // Below civil twilight - complete darkness
        return 0.0f;
    } else if (pos.altitude <= 0.0) {
        // Twilight period - gradual transition from 0 to 0.1
        float twilight_factor = (pos.altitude + 6.0) / 6.0;  // 0.0 to 1.0
        return 0.1f * twilight_factor;
    } else if (pos.altitude <= 6.0) {
        // Dawn/dusk transition - from 0.1 to full intensity
        float dawn_factor = pos.altitude / 6.0;  // 0.0 to 1.0
        double altitude_rad = pos.altitude * M_PI / 180.0;
        float base_intensity = static_cast<float>(sin(altitude_rad));
        return 0.1f + (base_intensity - 0.1f) * dawn_factor;
    } else {
        // Above 6 degrees - full daylight calculation
        double altitude_rad = pos.altitude * M_PI / 180.0;
        float intensity = static_cast<float>(sin(altitude_rad));
        
        // Add slight atmospheric dimming for very low sun
        if (pos.altitude < 30.0) {
            float atm_factor = 0.7f + 0.3f * (pos.altitude / 30.0f);
            intensity *= atm_factor;
        }
        
        return intensity;
    }
}

float AstronomicalCalculator::calculate_moon_intensity_from_position(const CelestialPosition& pos, float phase) const {
    // If below horizon, return 0
    if (pos.altitude <= 0.0) {
        return 0.0f;
    }
    
    // Scale altitude from 0-90 degrees to 0.0-1.0 intensity
    // Use sine curve for more natural intensity scaling
    double altitude_rad = pos.altitude * M_PI / 180.0;
    float base_intensity = static_cast<float>(sin(altitude_rad));
    
    // Factor in moon phase - new moon is dim, full moon is bright
    float phase_brightness = 0.1f + 0.9f * (1.0f - abs(phase - 0.5f) * 2.0f);  // Peak at 0.5 (full moon)
    
    float intensity = base_intensity * phase_brightness;
    
    return intensity;
}