#include "astronomical_calculator.h"
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Astronomical constants for rise/set calculations
// Standard atmospheric refraction at horizon in degrees
static const double REFRACTION_AT_HORIZON = 34.0 / 60.0;  // 34 arcminutes

// Solar and lunar semi-diameters in degrees  
static const double SUN_SEMI_DIAMETER = 16.0 / 60.0;      // 16 arcminutes
static const double MOON_SEMI_DIAMETER = 15.5 / 60.0;     // 15.5 arcminutes

// Rise/set altitude thresholds (negative because we want upper limb at horizon)
static const double SUN_RISE_SET_ALTITUDE = -(REFRACTION_AT_HORIZON + SUN_SEMI_DIAMETER);   // -0.833°
static const double MOON_RISE_SET_ALTITUDE = -(REFRACTION_AT_HORIZON + MOON_SEMI_DIAMETER); // -0.825°

// Helper function to normalize angle to 0-360 degrees
static double normalize_degrees(double angle) {
    angle = fmod(angle, 360.0);
    if (angle < 0) angle += 360.0;
    return angle;
}

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
    
    // Time in Julian centuries from J2000.0
    double T = (jd - 2451545.0) / 36525.0;
    double T2 = T * T;
    double T3 = T2 * T;
    double T4 = T3 * T;
    
    // Sun's mean longitude
    double Ls = 280.46646 + 36000.76983 * T + 0.0003032 * T2;
    Ls = normalize_degrees(Ls);
    
    // Moon's mean longitude
    double Lm = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0;
    Lm = normalize_degrees(Lm);
    
    // Sun's mean anomaly
    double Ms = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0;
    Ms = normalize_degrees(Ms);
    double Ms_rad = Ms * M_PI / 180.0;
    
    // Moon's mean anomaly
    double Mm = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0;
    Mm = normalize_degrees(Mm);
    double Mm_rad = Mm * M_PI / 180.0;
    
    // Moon's mean elongation
    double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0;
    D = normalize_degrees(D);
    double D_rad = D * M_PI / 180.0;
    
    // Moon's argument of latitude
    double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0;
    F = normalize_degrees(F);
    double F_rad = F * M_PI / 180.0;
    
    // Earth's eccentricity correction
    double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    
    // Sun's equation of center
    double C_sun = (1.914602 - 0.004817 * T - 0.000014 * T2) * sin(Ms_rad) +
                   (0.019993 - 0.000101 * T) * sin(2 * Ms_rad) +
                   0.000289 * sin(3 * Ms_rad);
    
    // Sun's true longitude
    double sun_true_long = Ls + C_sun;
    sun_true_long = normalize_degrees(sun_true_long);
    
    // Moon's longitude corrections (main terms for phase calculation)
    double moon_corr = 6.288774 * sin(Mm_rad) +
                       1.274027 * sin(2*D_rad - Mm_rad) +
                       0.658314 * sin(2*D_rad) +
                       0.213618 * sin(2*Mm_rad) -
                       0.185116 * sin(Ms_rad) * E -
                       0.114332 * sin(2*F_rad) +
                       0.058793 * sin(2*D_rad - 2*Mm_rad) +
                       0.057066 * sin(2*D_rad - Ms_rad - Mm_rad) * E +
                       0.053322 * sin(2*D_rad + Mm_rad) +
                       0.045758 * sin(2*D_rad - Ms_rad) * E;
    
    // Moon's true longitude
    double moon_true_long = Lm + moon_corr;
    moon_true_long = normalize_degrees(moon_true_long);
    
    // Phase angle (elongation)
    double phase_angle = normalize_degrees(moon_true_long - sun_true_long);
    
    // Convert to phase (0 = new moon, 0.5 = full moon, 1 = new moon again)
    float phase = static_cast<float>(phase_angle / 360.0);
    
    return phase;
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
    // Time in Julian centuries from J2000.0
    double T = (julian_day - 2451545.0) / 36525.0;
    double T2 = T * T;
    double T3 = T2 * T;
    double T4 = T3 * T;
    
    // Moon's mean longitude (L')
    double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0;
    Lp = normalize_degrees(Lp);
    
    // Moon's mean elongation (D)
    double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0;
    D = normalize_degrees(D);
    
    // Sun's mean anomaly (M)
    double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0;
    M = normalize_degrees(M);
    
    // Moon's mean anomaly (M')
    double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0;
    Mp = normalize_degrees(Mp);
    
    // Moon's argument of latitude (F)
    double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0;
    F = normalize_degrees(F);
    
    // Three further arguments for corrections
    double A1 = normalize_degrees(119.75 + 131.849 * T);
    double A2 = normalize_degrees(53.09 + 479264.290 * T);
    double A3 = normalize_degrees(313.45 + 481266.484 * T);
    
    // Earth's eccentricity correction factor
    double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    
    // Convert to radians for calculations
    double D_rad = D * M_PI / 180.0;
    double M_rad = M * M_PI / 180.0;
    double Mp_rad = Mp * M_PI / 180.0;
    double F_rad = F * M_PI / 180.0;
    
    // Sum the longitude periodic terms (simplified - using main terms only)
    double sum_l = 0.0;
    
    // Primary longitude corrections (in units of 0.000001 degrees)
    sum_l += 6288774 * sin(Mp_rad);
    sum_l += 1274027 * sin(2*D_rad - Mp_rad);
    sum_l += 658314 * sin(2*D_rad);
    sum_l += 213618 * sin(2*Mp_rad);
    sum_l -= 185116 * sin(M_rad) * E;
    sum_l -= 114332 * sin(2*F_rad);
    sum_l += 58793 * sin(2*D_rad - 2*Mp_rad);
    sum_l += 57066 * sin(2*D_rad - M_rad - Mp_rad) * E;
    sum_l += 53322 * sin(2*D_rad + Mp_rad);
    sum_l += 45758 * sin(2*D_rad - M_rad) * E;
    sum_l -= 40923 * sin(M_rad - Mp_rad) * E;
    sum_l -= 34720 * sin(D_rad);
    sum_l -= 30383 * sin(M_rad + Mp_rad) * E;
    sum_l += 15327 * sin(2*D_rad - 2*F_rad);
    sum_l -= 12528 * sin(Mp_rad + 2*F_rad);
    sum_l += 10980 * sin(Mp_rad - 2*F_rad);
    sum_l += 10675 * sin(4*D_rad - Mp_rad);
    sum_l += 10034 * sin(3*Mp_rad);
    sum_l += 8548 * sin(4*D_rad - 2*Mp_rad);
    sum_l -= 7888 * sin(2*D_rad + M_rad - Mp_rad) * E;
    sum_l -= 6766 * sin(2*D_rad + M_rad) * E;
    sum_l -= 5163 * sin(D_rad - Mp_rad);
    sum_l += 4987 * sin(D_rad + M_rad) * E;
    sum_l += 4036 * sin(2*D_rad - M_rad + Mp_rad) * E;
    
    // Add correction from additional arguments
    sum_l += 3994 * sin(A1 * M_PI / 180.0);
    sum_l += 3861 * sin(A2 * M_PI / 180.0);
    sum_l += 3665 * sin(A3 * M_PI / 180.0);
    
    // Apply longitude correction
    double moon_longitude = Lp + sum_l / 1000000.0;
    
    // Calculate latitude periodic terms (simplified)
    double sum_b = 0.0;
    
    // Primary latitude corrections (in units of 0.000001 degrees)
    sum_b += 5128122 * sin(F_rad);
    sum_b += 280602 * sin(Mp_rad + F_rad);
    sum_b += 277693 * sin(Mp_rad - F_rad);
    sum_b += 173237 * sin(2*D_rad - F_rad);
    sum_b += 55413 * sin(2*D_rad - Mp_rad + F_rad);
    sum_b += 46271 * sin(2*D_rad - Mp_rad - F_rad);
    sum_b += 32573 * sin(2*D_rad + F_rad);
    sum_b += 17198 * sin(2*Mp_rad + F_rad);
    sum_b += 9266 * sin(2*D_rad + Mp_rad - F_rad);
    sum_b += 8822 * sin(2*Mp_rad - F_rad);
    sum_b += 8216 * sin(2*D_rad - M_rad - F_rad) * E;
    sum_b += 4324 * sin(2*D_rad - 2*Mp_rad - F_rad);
    sum_b += 4200 * sin(2*D_rad + Mp_rad + F_rad);
    
    // Apply latitude correction
    double moon_latitude = sum_b / 1000000.0;
    
    // Convert to radians
    double lambda = moon_longitude * M_PI / 180.0;
    double beta = moon_latitude * M_PI / 180.0;
    
    // Calculate nutation in longitude (simplified)
    double omega = 125.04452 - 1934.136261 * T + 0.0020708 * T2 + T3 / 450000.0;
    double omega_rad = omega * M_PI / 180.0;
    double delta_psi = -17.20 * sin(omega_rad) / 3600.0;  // degrees
    
    // Mean obliquity of the ecliptic
    double epsilon_0 = 23.439291111 - 0.0130042 * T - 0.00000016 * T2 + 0.000000504 * T3;
    double epsilon = epsilon_0 + 0.00256 * cos(omega_rad) / 3600.0;  // True obliquity in degrees
    double epsilon_rad = epsilon * M_PI / 180.0;
    
    // Apply nutation to longitude
    lambda += delta_psi * M_PI / 180.0;
    
    // Convert ecliptic to equatorial coordinates
    double alpha = atan2(sin(lambda) * cos(epsilon_rad) - tan(beta) * sin(epsilon_rad), cos(lambda));
    double delta = asin(sin(beta) * cos(epsilon_rad) + cos(beta) * sin(epsilon_rad) * sin(lambda));
    
    // Ensure right ascension is positive
    if (alpha < 0) alpha += 2 * M_PI;
    
    // Calculate Greenwich Mean Sidereal Time
    double GMST = 280.46061837 + 360.98564736629 * (julian_day - 2451545.0) + 
                   0.000387933 * T2 - T3 / 38710000.0;
    GMST = normalize_degrees(GMST);
    
    // Calculate Local Sidereal Time
    double LST = GMST + longitude_;
    LST = normalize_degrees(LST);
    double LST_rad = LST * M_PI / 180.0;
    
    // Calculate hour angle
    double H = LST_rad - alpha;
    
    // Observer's latitude in radians
    double phi = latitude_ * M_PI / 180.0;
    // Calculate altitude and azimuth
    CelestialPosition pos;
    
    // Altitude
    double sin_alt = sin(phi) * sin(delta) + cos(phi) * cos(delta) * cos(H);
    pos.altitude = asin(sin_alt) * 180.0 / M_PI;
    
    // Azimuth
    double y = sin(H);
    double x = cos(H) * sin(phi) - tan(delta) * cos(phi);
    double A = atan2(y, x);
    pos.azimuth = normalize_degrees(A * 180.0 / M_PI + 180.0);
    
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
    
    // Track all moon events over extended period
    struct MoonEvent {
        bool is_rise;
        int minutes_from_midnight;  // Minutes from target day midnight
        double altitude_before;
        double altitude_after;
        bool is_nighttime;  // Event occurs during typical night hours
    };
    
    std::vector<MoonEvent> all_events;
    
    // Search from 12 hours before midnight to 36 hours after
    const int search_start = -720;  // 12 hours before midnight
    const int search_end = 2160;    // 36 hours after midnight
    
    double prev_altitude = -90.0;
    
    // Check every 5 minutes for better precision
    for (int minutes = search_start; minutes <= search_end; minutes += 5) {
        double hours_offset = minutes / 60.0;
        double jd = jd_base + hours_offset / 24.0;
        
        auto pos = calculate_moon_position_at_time(jd);
        double altitude = pos.altitude;
        
        if (prev_altitude != -90.0) {
            // Check for horizon crossings at the appropriate altitude for rise/set
            // This accounts for atmospheric refraction and upper limb detection
            if (prev_altitude < MOON_RISE_SET_ALTITUDE && altitude >= MOON_RISE_SET_ALTITUDE) {
                // Moon rise detected - upper limb at horizon with refraction
                MoonEvent event;
                event.is_rise = true;
                event.minutes_from_midnight = minutes;
                event.altitude_before = prev_altitude;
                event.altitude_after = altitude;
                
                // Determine if this is a nighttime event
                // Consider nighttime as 18:00-06:00 (6 PM to 6 AM)
                int hour_of_day = ((minutes + 1440) % 1440) / 60;
                event.is_nighttime = (hour_of_day >= 18 || hour_of_day < 6);
                
                all_events.push_back(event);
            }
            else if (prev_altitude >= MOON_RISE_SET_ALTITUDE && altitude < MOON_RISE_SET_ALTITUDE) {
                // Moon set detected - upper limb at horizon with refraction
                MoonEvent event;
                event.is_rise = false;
                event.minutes_from_midnight = minutes;
                event.altitude_before = prev_altitude;
                event.altitude_after = altitude;
                
                // Determine if this is a nighttime event
                int hour_of_day = ((minutes + 1440) % 1440) / 60;
                event.is_nighttime = (hour_of_day >= 18 || hour_of_day < 6);
                
                all_events.push_back(event);
            }
        }
        
        prev_altitude = altitude;
    }
    
    // Now intelligently select the best rise/set pair for aquarium lighting
    // Priority:
    // 1. Evening rise (18:00-23:59) paired with morning set (00:00-12:00)
    // 2. Any nighttime rise paired with any set
    // 3. Any rise/set pair that maximizes nighttime moon visibility
    
    MoonEvent* best_rise = nullptr;
    MoonEvent* best_set = nullptr;
    int best_score = -1;
    
    // Look for evening rises (18:00-23:59 on target day)
    for (auto& event : all_events) {
        if (event.is_rise) {
            int minutes_in_day = event.minutes_from_midnight;
            if (minutes_in_day >= 1080 && minutes_in_day < 1440) {  // 18:00-23:59
                // This is an evening rise, look for corresponding morning set
                for (auto& set_event : all_events) {
                    if (!set_event.is_rise && set_event.minutes_from_midnight > event.minutes_from_midnight) {
                        // Found a set after this rise
                        int set_minutes_in_day = set_event.minutes_from_midnight;
                        
                        // Score this pair based on lighting usefulness
                        int score = 0;
                        if (event.is_nighttime) score += 10;
                        if (set_event.is_nighttime) score += 10;
                        if (set_minutes_in_day >= 1440 && set_minutes_in_day < 2160) score += 5;  // Next morning set
                        
                        // Prefer longer moon visibility periods
                        int duration = set_event.minutes_from_midnight - event.minutes_from_midnight;
                        if (duration > 360 && duration < 900) score += 5;  // 6-15 hour duration
                        
                        if (score > best_score) {
                            best_score = score;
                            best_rise = &event;
                            best_set = &set_event;
                        }
                        break;  // Only check first set after this rise
                    }
                }
            }
        }
    }
    
    // If no evening rise found, look for any nighttime events
    if (!best_rise) {
        for (auto& event : all_events) {
            if (event.is_rise && event.is_nighttime) {
                int minutes_in_day = event.minutes_from_midnight;
                if (minutes_in_day >= -360 && minutes_in_day < 1800) {  // Within reasonable range
                    for (auto& set_event : all_events) {
                        if (!set_event.is_rise && 
                            set_event.minutes_from_midnight > event.minutes_from_midnight &&
                            set_event.minutes_from_midnight - event.minutes_from_midnight < 900) {
                            best_rise = &event;
                            best_set = &set_event;
                            break;
                        }
                    }
                    if (best_rise) break;
                }
            }
        }
    }
    
    // Fall back to any rise/set pair if no nighttime events found
    if (!best_rise && !all_events.empty()) {
        for (auto& event : all_events) {
            if (event.is_rise) {
                int minutes_in_day = event.minutes_from_midnight;
                if (minutes_in_day >= 0 && minutes_in_day < 1440) {
                    best_rise = &event;
                    // Find corresponding set
                    for (auto& set_event : all_events) {
                        if (!set_event.is_rise && set_event.minutes_from_midnight > event.minutes_from_midnight) {
                            best_set = &set_event;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
    
    // Convert selected events to result
    if (best_rise) {
        // Fine-tune the crossing time using linear interpolation
        // Calculate where altitude crosses MOON_RISE_SET_ALTITUDE
        double ratio = (MOON_RISE_SET_ALTITUDE - best_rise->altitude_before) / 
                      (best_rise->altitude_after - best_rise->altitude_before);
        int fine_minutes = best_rise->minutes_from_midnight - 5 + static_cast<int>(5 * ratio);
        
        // Normalize to 0-1439 range
        while (fine_minutes < 0) fine_minutes += 1440;
        while (fine_minutes >= 1440) fine_minutes -= 1440;
        
        result.rise_minutes = static_cast<uint16_t>(fine_minutes);
        result.rise_valid = true;
    }
    
    if (best_set) {
        // Fine-tune the crossing time using linear interpolation
        // Calculate where altitude crosses MOON_RISE_SET_ALTITUDE
        double ratio = (best_set->altitude_before - MOON_RISE_SET_ALTITUDE) / 
                      (best_set->altitude_before - best_set->altitude_after);
        int fine_minutes = best_set->minutes_from_midnight - 5 + static_cast<int>(5 * ratio);
        
        // Normalize to 0-1439 range
        while (fine_minutes < 0) fine_minutes += 1440;
        while (fine_minutes >= 1440) fine_minutes -= 1440;
        
        result.set_minutes = static_cast<uint16_t>(fine_minutes);
        result.set_valid = true;
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
        
        // Check for horizon crossings at the appropriate altitude for rise/set
        if (prev_altitude != -90.0) {  // Skip first iteration
            if (prev_altitude < SUN_RISE_SET_ALTITUDE && altitude >= SUN_RISE_SET_ALTITUDE && !found_rise) {
                // Sun rise detected - upper limb at horizon with refraction
                result.rise_minutes = minutes - 7;  // Approximate midpoint of 15-minute interval
                if (result.rise_minutes < 0) result.rise_minutes += 1440;
                result.rise_valid = true;
                found_rise = true;
            }
            else if (prev_altitude >= SUN_RISE_SET_ALTITUDE && altitude < SUN_RISE_SET_ALTITUDE && !found_set) {
                // Sun set detected - upper limb at horizon with refraction
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
        int new_rise_minutes = static_cast<int>(actual_times.rise_minutes) + shift_minutes;
        // Ensure the result is in the range [0, 1439]
        while (new_rise_minutes < 0) new_rise_minutes += 1440;
        while (new_rise_minutes >= 1440) new_rise_minutes -= 1440;
        projected_times.rise_minutes = static_cast<uint16_t>(new_rise_minutes);
    }
    
    if (actual_times.set_valid) {
        // Add the time shift (convert to total minutes) 
        int shift_minutes = time_shift_hours_ * 60 + time_shift_minutes_;
        int new_set_minutes = static_cast<int>(actual_times.set_minutes) + shift_minutes;
        // Ensure the result is in the range [0, 1439]
        while (new_set_minutes < 0) new_set_minutes += 1440;
        while (new_set_minutes >= 1440) new_set_minutes -= 1440;
        projected_times.set_minutes = static_cast<uint16_t>(new_set_minutes);
    }
    
    return projected_times;
}

AstronomicalCalculator::MoonTimes AstronomicalCalculator::get_projected_moon_rise_set_times(const DateTime& dt) const {
    if (!projection_enabled_) {
        return get_moon_rise_set_times(dt);
    }
    
    // Calculate the actual moon rise/set times first
    MoonTimes actual_times = get_moon_rise_set_times(dt);
    
    // Apply the time shift to the results
    MoonTimes projected_times = actual_times;
    
    if (actual_times.rise_valid) {
        // Add the time shift (convert to total minutes)
        int shift_minutes = time_shift_hours_ * 60 + time_shift_minutes_;
        int new_rise_minutes = static_cast<int>(actual_times.rise_minutes) + shift_minutes;
        // Ensure the result is in the range [0, 1439]
        while (new_rise_minutes < 0) new_rise_minutes += 1440;
        while (new_rise_minutes >= 1440) new_rise_minutes -= 1440;
        projected_times.rise_minutes = static_cast<uint16_t>(new_rise_minutes);
    }
    
    if (actual_times.set_valid) {
        // Add the time shift (convert to total minutes) 
        int shift_minutes = time_shift_hours_ * 60 + time_shift_minutes_;
        int new_set_minutes = static_cast<int>(actual_times.set_minutes) + shift_minutes;
        // Ensure the result is in the range [0, 1439]
        while (new_set_minutes < 0) new_set_minutes += 1440;
        while (new_set_minutes >= 1440) new_set_minutes -= 1440;
        projected_times.set_minutes = static_cast<uint16_t>(new_set_minutes);
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