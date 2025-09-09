#include "scheduler.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

// Use cJSON for lightweight JSON parsing
// cJSON is a single-file library perfect for embedded systems
extern "C" {
    #include "cJSON.h"
}

LEDScheduler::LEDScheduler(uint8_t num_channels) 
    : num_channels_(num_channels) {
    // Initialize channel configs with default colors
    channel_configs_.resize(num_channels_);
    // Default colors for common LED types
    const std::vector<std::string> default_colors = {
        "#FFFFFF", // Channel 1: White
        "#0000FF", // Channel 2: Blue
        "#00FFFF", // Channel 3: Cyan
        "#00FF00", // Channel 4: Green
        "#FF0000", // Channel 5: Red
        "#FF00FF", // Channel 6: Magenta
        "#FFFF00", // Channel 7: Yellow
        "#FF8000"  // Channel 8: Orange
    };
    
    for (uint8_t i = 0; i < num_channels_; i++) {
        channel_configs_[i].rgb_hex = i < default_colors.size() ? default_colors[i] : "#FFFFFF";
        channel_configs_[i].max_current = 2.0f; // Default 2A
        channel_configs_[i].name = "Channel " + std::to_string(i + 1);
    }
}

void LEDScheduler::set_num_channels(uint8_t channels) {
    if (channels < 1 || channels > 16) {
        return; // Invalid channel count
    }
    num_channels_ = channels;
    
    // Update channel configs
    const std::vector<std::string> default_colors = {
        "#FFFFFF", "#0000FF", "#00FFFF", "#00FF00",
        "#FF0000", "#FF00FF", "#FFFF00", "#FF8000"
    };
    
    size_t old_size = channel_configs_.size();
    channel_configs_.resize(num_channels_);
    
    // Initialize new channels if expanded
    for (uint8_t i = old_size; i < num_channels_; i++) {
        channel_configs_[i].rgb_hex = i < default_colors.size() ? default_colors[i] : "#FFFFFF";
        channel_configs_[i].max_current = 2.0f;
        channel_configs_[i].name = "Channel " + std::to_string(i + 1);
    }
    
    // Update existing schedule points to match new channel count
    for (auto& point : schedule_points_) {
        point.pwm_values.resize(num_channels_, 0.0f);
        point.current_values.resize(num_channels_, 0.0f);
    }
    
    // Update presets too
    for (auto& preset : presets_) {
        for (auto& point : preset.second) {
            point.pwm_values.resize(num_channels_, 0.0f);
            point.current_values.resize(num_channels_, 0.0f);
        }
    }
}

void LEDScheduler::add_schedule_point(const SchedulePoint& point) {
    if (!validate_point(point)) {
        return;
    }
    
    // Remove existing point at same time
    remove_schedule_point(point.time_minutes);
    
    // Add new point
    schedule_points_.push_back(point);
    sort_schedule_points();
}

void LEDScheduler::set_schedule_point(uint16_t time_minutes, const std::vector<float>& pwm_values, 
                                     const std::vector<float>& current_values) {
    if (time_minutes >= 1440) {
        return; // Invalid time
    }
    
    SchedulePoint point(time_minutes, pwm_values, current_values);
    
    // Resize vectors to match channel count
    point.pwm_values.resize(num_channels_, 0.0f);
    point.current_values.resize(num_channels_, 0.0f);
    
    add_schedule_point(point);
}

void LEDScheduler::add_dynamic_schedule_point(DynamicTimeType type, int16_t offset_minutes,
                                             const std::vector<float>& pwm_values,
                                             const std::vector<float>& current_values) {
    SchedulePoint point(type, offset_minutes, pwm_values, current_values);
    
    // Resize vectors to match channel count
    point.pwm_values.resize(num_channels_, 0.0f);
    point.current_values.resize(num_channels_, 0.0f);
    
    // Remove any existing dynamic point with same type and offset
    remove_dynamic_schedule_point(type, offset_minutes);
    
    schedule_points_.push_back(point);
    // Don't sort here - dynamic points need astronomical times to resolve
}

void LEDScheduler::remove_schedule_point(uint16_t time_minutes) {
    schedule_points_.erase(
        std::remove_if(schedule_points_.begin(), schedule_points_.end(),
                      [time_minutes](const SchedulePoint& p) { 
                          return p.time_type == DynamicTimeType::FIXED && 
                                 p.time_minutes == time_minutes; 
                      }),
        schedule_points_.end()
    );
}

void LEDScheduler::remove_dynamic_schedule_point(DynamicTimeType type, int16_t offset_minutes) {
    schedule_points_.erase(
        std::remove_if(schedule_points_.begin(), schedule_points_.end(),
                      [type, offset_minutes](const SchedulePoint& p) { 
                          return p.time_type == type && 
                                 p.offset_minutes == offset_minutes; 
                      }),
        schedule_points_.end()
    );
}

void LEDScheduler::clear_schedule() {
    schedule_points_.clear();
}

LEDScheduler::InterpolationResult LEDScheduler::get_values_at_time(uint16_t current_time_minutes) const {
    if (current_time_minutes >= 1440) {
        return InterpolationResult(); // Invalid time
    }
    
    return interpolate_values(current_time_minutes);
}

LEDScheduler::InterpolationResult LEDScheduler::get_values_at_time_with_astro(uint16_t current_time_minutes, 
                                                                            const AstronomicalTimes& astro_times) const {
    if (current_time_minutes >= 1440) {
        return InterpolationResult(); // Invalid time
    }
    
    return interpolate_values_with_astro(current_time_minutes, astro_times);
}

void LEDScheduler::set_astronomical_times(const AstronomicalTimes& times) {
    astronomical_times_ = times;
}

uint16_t LEDScheduler::calculate_dynamic_time(const SchedulePoint& point, const AstronomicalTimes& astro_times) const {
    uint16_t base_time = 0;
    
    switch (point.time_type) {
        case DynamicTimeType::FIXED:
            return point.time_minutes;
            
        case DynamicTimeType::SUNRISE_RELATIVE:
            base_time = astro_times.sunrise_minutes;
            break;
            
        case DynamicTimeType::SUNSET_RELATIVE:
            base_time = astro_times.sunset_minutes;
            break;
            
        case DynamicTimeType::SOLAR_NOON:
            base_time = astro_times.solar_noon_minutes;
            break;
            
        case DynamicTimeType::CIVIL_DAWN:
            base_time = astro_times.civil_dawn_minutes;
            break;
            
        case DynamicTimeType::CIVIL_DUSK:
            base_time = astro_times.civil_dusk_minutes;
            break;
            
        case DynamicTimeType::NAUTICAL_DAWN:
            base_time = astro_times.nautical_dawn_minutes;
            break;
            
        case DynamicTimeType::NAUTICAL_DUSK:
            base_time = astro_times.nautical_dusk_minutes;
            break;
            
        case DynamicTimeType::ASTRONOMICAL_DAWN:
            base_time = astro_times.astronomical_dawn_minutes;
            break;
            
        case DynamicTimeType::ASTRONOMICAL_DUSK:
            base_time = astro_times.astronomical_dusk_minutes;
            break;
    }
    
    // Apply offset
    int32_t calculated_time = static_cast<int32_t>(base_time) + point.offset_minutes;
    
    // Wrap around to stay within 0-1439 range
    while (calculated_time < 0) {
        calculated_time += 1440;
    }
    while (calculated_time >= 1440) {
        calculated_time -= 1440;
    }
    
    return static_cast<uint16_t>(calculated_time);
}

LEDScheduler::DynamicTimeType LEDScheduler::string_to_dynamic_time_type(const std::string& type_str) {
    if (type_str == "fixed") {
        return DynamicTimeType::FIXED;
    } else if (type_str == "sunrise_relative") {
        return DynamicTimeType::SUNRISE_RELATIVE;
    } else if (type_str == "sunset_relative") {
        return DynamicTimeType::SUNSET_RELATIVE;
    } else if (type_str == "solar_noon") {
        return DynamicTimeType::SOLAR_NOON;
    } else if (type_str == "civil_dawn") {
        return DynamicTimeType::CIVIL_DAWN;
    } else if (type_str == "civil_dusk") {
        return DynamicTimeType::CIVIL_DUSK;
    } else if (type_str == "nautical_dawn") {
        return DynamicTimeType::NAUTICAL_DAWN;
    } else if (type_str == "nautical_dusk") {
        return DynamicTimeType::NAUTICAL_DUSK;
    } else if (type_str == "astronomical_dawn") {
        return DynamicTimeType::ASTRONOMICAL_DAWN;
    } else if (type_str == "astronomical_dusk") {
        return DynamicTimeType::ASTRONOMICAL_DUSK;
    } else {
        return DynamicTimeType::FIXED; // Default fallback
    }
}

std::string LEDScheduler::dynamic_time_type_to_string(DynamicTimeType type) {
    switch (type) {
        case DynamicTimeType::FIXED: return "fixed";
        case DynamicTimeType::SUNRISE_RELATIVE: return "sunrise_relative";
        case DynamicTimeType::SUNSET_RELATIVE: return "sunset_relative";
        case DynamicTimeType::SOLAR_NOON: return "solar_noon";
        case DynamicTimeType::CIVIL_DAWN: return "civil_dawn";
        case DynamicTimeType::CIVIL_DUSK: return "civil_dusk";
        case DynamicTimeType::NAUTICAL_DAWN: return "nautical_dawn";
        case DynamicTimeType::NAUTICAL_DUSK: return "nautical_dusk";
        case DynamicTimeType::ASTRONOMICAL_DAWN: return "astronomical_dawn";
        case DynamicTimeType::ASTRONOMICAL_DUSK: return "astronomical_dusk";
        default: return "fixed";
    }
}

LEDScheduler::InterpolationResult LEDScheduler::interpolate_values(uint16_t current_time) const {
    InterpolationResult result;
    result.pwm_values.resize(num_channels_, 0.0f);
    result.current_values.resize(num_channels_, 0.0f);
    
    if (schedule_points_.empty()) {
        return result;
    }
    
    // Handle single point case
    if (schedule_points_.size() == 1) {
        // Safety check - ensure vectors have correct size
        if (schedule_points_[0].pwm_values.size() != num_channels_ || 
            schedule_points_[0].current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        result.pwm_values = schedule_points_[0].pwm_values;
        result.current_values = schedule_points_[0].current_values;
        result.valid = true;
        return result;
    }
    
    // Find interpolation points
    const SchedulePoint* before = nullptr;
    const SchedulePoint* after = nullptr;
    
    // Check if we're before the first point
    if (current_time <= schedule_points_[0].time_minutes) {
        // Interpolate from midnight (0,0) to first point
        after = &schedule_points_[0];
        
        // Safety check - ensure vectors have correct size
        if (after->pwm_values.size() != num_channels_ || 
            after->current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        
        float ratio = after->time_minutes > 0 ? static_cast<float>(current_time) / after->time_minutes : 0.0f;
        
        for (size_t i = 0; i < num_channels_; i++) {
            result.pwm_values[i] = ratio * after->pwm_values[i];
            result.current_values[i] = ratio * after->current_values[i];
        }
        result.valid = true;
        return result;
    }
    
    // Check if we're after the last point
    if (current_time >= schedule_points_.back().time_minutes) {
        // Interpolate from last point to midnight (0,0)
        before = &schedule_points_.back();
        
        // Safety check - ensure vectors have correct size
        if (before->pwm_values.size() != num_channels_ || 
            before->current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        
        float total_span = 1440 - before->time_minutes;
        float ratio = total_span > 0 ? static_cast<float>(current_time - before->time_minutes) / total_span : 0.0f;
        
        for (size_t i = 0; i < num_channels_; i++) {
            result.pwm_values[i] = before->pwm_values[i] * (1.0f - ratio);
            result.current_values[i] = before->current_values[i] * (1.0f - ratio);
        }
        result.valid = true;
        return result;
    }
    
    // Find the two points we're between
    for (size_t i = 0; i < schedule_points_.size() - 1; i++) {
        if (current_time >= schedule_points_[i].time_minutes && 
            current_time <= schedule_points_[i + 1].time_minutes) {
            before = &schedule_points_[i];
            after = &schedule_points_[i + 1];
            break;
        }
    }
    
    if (before && after) {
        // Safety check - ensure vectors have correct size
        if (before->pwm_values.size() != num_channels_ || 
            before->current_values.size() != num_channels_ ||
            after->pwm_values.size() != num_channels_ || 
            after->current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        
        uint16_t time_span = after->time_minutes - before->time_minutes;
        float ratio = time_span > 0 ? static_cast<float>(current_time - before->time_minutes) / time_span : 0.0f;
        
        for (size_t i = 0; i < num_channels_; i++) {
            result.pwm_values[i] = before->pwm_values[i] + ratio * (after->pwm_values[i] - before->pwm_values[i]);
            result.current_values[i] = before->current_values[i] + ratio * (after->current_values[i] - before->current_values[i]);
        }
        result.valid = true;
    }
    
    return result;
}

void LEDScheduler::sort_schedule_points() {
    std::sort(schedule_points_.begin(), schedule_points_.end(),
              [](const SchedulePoint& a, const SchedulePoint& b) {
                  // Fixed points sort by time_minutes
                  // Dynamic points are not sorted here
                  if (a.time_type == DynamicTimeType::FIXED && b.time_type == DynamicTimeType::FIXED) {
                      return a.time_minutes < b.time_minutes;
                  }
                  // Put fixed points before dynamic ones for consistency
                  return a.time_type == DynamicTimeType::FIXED;
              });
}

std::vector<LEDScheduler::SchedulePoint> LEDScheduler::resolve_dynamic_points(const AstronomicalTimes& astro_times) const {
    std::vector<SchedulePoint> resolved_points;
    resolved_points.reserve(schedule_points_.size());
    
    for (const auto& point : schedule_points_) {
        SchedulePoint resolved_point = point;
        if (point.time_type != DynamicTimeType::FIXED) {
            resolved_point.time_minutes = calculate_dynamic_time(point, astro_times);
        }
        resolved_points.push_back(resolved_point);
    }
    
    // Sort the resolved points by actual time
    std::sort(resolved_points.begin(), resolved_points.end(),
              [](const SchedulePoint& a, const SchedulePoint& b) {
                  return a.time_minutes < b.time_minutes;
              });
              
    return resolved_points;
}

LEDScheduler::InterpolationResult LEDScheduler::interpolate_values_with_astro(uint16_t current_time, 
                                                                            const AstronomicalTimes& astro_times) const {
    // Resolve all dynamic points to actual times
    auto resolved_points = resolve_dynamic_points(astro_times);
    
    InterpolationResult result;
    result.pwm_values.resize(num_channels_, 0.0f);
    result.current_values.resize(num_channels_, 0.0f);
    
    if (resolved_points.empty()) {
        return result;
    }
    
    // Handle single point case
    if (resolved_points.size() == 1) {
        // Safety check - ensure vectors have correct size
        if (resolved_points[0].pwm_values.size() != num_channels_ || 
            resolved_points[0].current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        result.pwm_values = resolved_points[0].pwm_values;
        result.current_values = resolved_points[0].current_values;
        result.valid = true;
        return result;
    }
    
    // Find interpolation points
    const SchedulePoint* before = nullptr;
    const SchedulePoint* after = nullptr;
    
    // Find the points before and after current time
    for (size_t i = 0; i < resolved_points.size(); i++) {
        if (resolved_points[i].time_minutes <= current_time) {
            before = &resolved_points[i];
        }
        if (resolved_points[i].time_minutes >= current_time && !after) {
            after = &resolved_points[i];
            break;
        }
    }
    
    // If current time is before first point, wrap to use last point as 'before'
    if (!before && resolved_points.size() >= 2) {
        before = &resolved_points.back();
    }
    
    // If current time is after last point, wrap to use first point as 'after'
    if (!after && resolved_points.size() >= 2) {
        after = &resolved_points.front();
    }
    
    // Handle exact match
    if (before && after && before->time_minutes == current_time) {
        // Safety check - ensure vectors have correct size
        if (before->pwm_values.size() != num_channels_ || 
            before->current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        result.pwm_values = before->pwm_values;
        result.current_values = before->current_values;
        result.valid = true;
        return result;
    }
    
    // Interpolate
    if (before && after) {
        // Safety check - ensure vectors have correct size
        if (before->pwm_values.size() != num_channels_ || 
            before->current_values.size() != num_channels_ ||
            after->pwm_values.size() != num_channels_ || 
            after->current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        
        uint16_t time_span = after->time_minutes > before->time_minutes ? 
            after->time_minutes - before->time_minutes :
            (1440 - before->time_minutes) + after->time_minutes; // Handle wrap-around
            
        uint16_t elapsed = current_time >= before->time_minutes ?
            current_time - before->time_minutes :
            (1440 - before->time_minutes) + current_time; // Handle wrap-around
            
        float ratio = time_span > 0 ? static_cast<float>(elapsed) / time_span : 0.0f;
        
        for (size_t i = 0; i < num_channels_; i++) {
            result.pwm_values[i] = before->pwm_values[i] + ratio * (after->pwm_values[i] - before->pwm_values[i]);
            result.current_values[i] = before->current_values[i] + ratio * (after->current_values[i] - before->current_values[i]);
        }
        result.valid = true;
    }
    
    // Apply moon simulation if enabled
    if (moon_simulation_.enabled && astro_times.valid) {
        result = apply_moon_simulation(result, current_time, astro_times);
    }
    
    return result;
}

bool LEDScheduler::validate_point(const SchedulePoint& point) const {
    // For dynamic points, we don't validate time_minutes as it will be calculated
    if (point.time_type == DynamicTimeType::FIXED && point.time_minutes >= 1440) {
        return false;
    }
    
    // Validate offset for dynamic points
    if (point.time_type != DynamicTimeType::FIXED) {
        if (point.offset_minutes < -1439 || point.offset_minutes > 1439) {
            return false;
        }
    }
    
    if (point.pwm_values.size() != num_channels_ || point.current_values.size() != num_channels_) {
        return false;
    }
    
    // Validate ranges
    for (float pwm : point.pwm_values) {
        if (pwm < 0.0f || pwm > 100.0f) {
            return false;
        }
    }
    
    // Validate current values against per-channel limits
    for (size_t i = 0; i < point.current_values.size(); i++) {
        float current = point.current_values[i];
        if (current < 0.0f) {
            return false;
        }
        // Check against channel's max current if available
        if (i < channel_configs_.size()) {
            if (current > channel_configs_[i].max_current) {
                return false;
            }
        } else {
            // Fallback to default max of 2.0A
            if (current > 2.0f) {
                return false;
            }
        }
    }
    
    return true;
}

void LEDScheduler::set_moon_simulation(const MoonSimulation& config) {
    moon_simulation_ = config;
    // Resize base_intensity to match channel count
    moon_simulation_.base_intensity.resize(num_channels_, 0.0f);
}

void LEDScheduler::enable_moon_simulation(bool enabled) {
    moon_simulation_.enabled = enabled;
}

void LEDScheduler::set_moon_base_intensity(const std::vector<float>& intensity) {
    moon_simulation_.base_intensity = intensity;
    moon_simulation_.base_intensity.resize(num_channels_, 0.0f);
}

bool LEDScheduler::is_moon_visible(uint16_t current_time, const AstronomicalTimes& astro_times) const {
    // Check if moon is above horizon
    if (astro_times.moonrise_minutes == 0 && astro_times.moonset_minutes == 0) {
        return false; // No moon data available
    }
    
    // Handle cases where moon rise/set crosses midnight
    if (astro_times.moonrise_minutes < astro_times.moonset_minutes) {
        // Moon rises and sets on same day
        return current_time >= astro_times.moonrise_minutes && current_time <= astro_times.moonset_minutes;
    } else {
        // Moon rise/set crosses midnight
        return current_time >= astro_times.moonrise_minutes || current_time <= astro_times.moonset_minutes;
    }
}

LEDScheduler::InterpolationResult LEDScheduler::apply_moon_simulation(const InterpolationResult& base_result, 
                                                                     uint16_t current_time, 
                                                                     const AstronomicalTimes& astro_times) const {
    InterpolationResult result = base_result;
    
    // Check if moon should be visible
    if (!is_moon_visible(current_time, astro_times)) {
        return result; // Moon not visible, return original result
    }
    
    // Check if all channels are at or near zero
    bool all_channels_dark = true;
    const float threshold = 0.1f; // Consider values below 0.1% as "off"
    
    for (size_t i = 0; i < num_channels_; i++) {
        if (result.pwm_values[i] > threshold) {
            all_channels_dark = false;
            break;
        }
    }
    
    // If main lights are on, don't apply moonlight
    if (!all_channels_dark) {
        return result;
    }
    
    // Apply moonlight based on moon phase
    float moon_brightness = astro_times.moon_phase;
    
    // Convert moon phase (0=new, 0.5=full, 1=new) to brightness
    // Peak brightness at full moon (0.5)
    if (moon_brightness > 0.5f) {
        moon_brightness = 1.0f - moon_brightness;
    }
    moon_brightness *= 2.0f; // Scale 0-0.5 to 0-1.0
    
    // Apply moon simulation
    for (size_t i = 0; i < num_channels_; i++) {
        if (i < moon_simulation_.base_intensity.size()) {
            float moon_intensity = moon_simulation_.base_intensity[i];
            
            // Scale by moon phase if enabled
            if (moon_simulation_.phase_scaling) {
                moon_intensity *= moon_brightness;
            }
            
            // Apply moonlight (additive to ensure it shows even if base is 0)
            result.pwm_values[i] = moon_intensity;
            
            // Scale current proportionally (moonlight uses much less current)
            result.current_values[i] = moon_intensity * 0.02f; // 2% of PWM value as current
        }
    }
    
    return result;
}

void LEDScheduler::load_preset(const std::string& preset_name) {
    if (preset_name == "sunrise_sunset") {
        create_sunrise_sunset_preset();
        return;
    }
    
    if (preset_name == "dynamic_sunrise_sunset") {
        create_dynamic_sunrise_sunset_preset();
        return;
    }
    
    if (preset_name == "full_spectrum") {
        create_full_spectrum_preset();
        return;
    }
    
    if (preset_name == "simple") {
        create_simple_preset();
        return;
    }
    
    auto it = presets_.find(preset_name);
    if (it != presets_.end()) {
        schedule_points_ = it->second;
        sort_schedule_points();
    }
}

void LEDScheduler::save_preset(const std::string& preset_name) {
    presets_[preset_name] = schedule_points_;
}

std::vector<std::string> LEDScheduler::get_preset_names() const {
    std::vector<std::string> names;
    names.push_back("sunrise_sunset");
    names.push_back("dynamic_sunrise_sunset");
    names.push_back("full_spectrum");
    names.push_back("simple");
    
    for (const auto& preset : presets_) {
        names.push_back(preset.first);
    }
    
    return names;
}

void LEDScheduler::clear_preset(const std::string& preset_name) {
    presets_.erase(preset_name);
}

void LEDScheduler::create_sunrise_sunset_preset(uint16_t sunrise_minutes, uint16_t sunset_minutes) {
    clear_schedule();
    
    // Calculate solar noon (midpoint between sunrise and sunset)
    uint16_t noon_minutes = 720; // Default 12:00 PM
    if (sunset_minutes > sunrise_minutes) {
        noon_minutes = (sunrise_minutes + sunset_minutes) / 2;
    }
    
    // Sunrise - Dawn (gradually increasing warm light)
    add_schedule_point(SchedulePoint(sunrise_minutes,
        std::vector<float>(num_channels_, 20.0f),  // 20% PWM
        std::vector<float>(num_channels_, 0.3f)));  // 0.3A current
    
    // Solar noon - Peak intensity
    add_schedule_point(SchedulePoint(noon_minutes,
        std::vector<float>(num_channels_, 85.0f),  // 85% PWM
        std::vector<float>(num_channels_, 1.8f)));  // 1.8A current
    
    // Sunset - Evening (warm, dimmer light)
    add_schedule_point(SchedulePoint(sunset_minutes,
        std::vector<float>(num_channels_, 15.0f),  // 15% PWM  
        std::vector<float>(num_channels_, 0.2f)));  // 0.2A current
    
    // Night - Off
    add_schedule_point(SchedulePoint(sunset_minutes + 60,
        std::vector<float>(num_channels_, 0.0f),   // 0% PWM
        std::vector<float>(num_channels_, 0.0f))); // 0A current
}

void LEDScheduler::create_full_spectrum_preset() {
    clear_schedule();
    
    // Create varied spectrum throughout the day
    std::vector<float> morning_pwm = {40, 60, 80, 100, 80, 60, 40, 20};
    std::vector<float> morning_current = {0.6f, 1.0f, 1.5f, 2.0f, 1.5f, 1.0f, 0.6f, 0.3f};
    morning_pwm.resize(num_channels_, morning_pwm.back());
    morning_current.resize(num_channels_, morning_current.back());
    
    std::vector<float> noon_pwm = {80, 100, 100, 100, 100, 100, 80, 60};
    std::vector<float> noon_current = {1.5f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 1.5f, 1.0f};
    noon_pwm.resize(num_channels_, noon_pwm.back());
    noon_current.resize(num_channels_, noon_current.back());
    
    std::vector<float> afternoon_pwm = {60, 80, 100, 100, 80, 60, 40, 30};
    std::vector<float> afternoon_current = {1.0f, 1.5f, 2.0f, 2.0f, 1.5f, 1.0f, 0.6f, 0.4f};
    afternoon_pwm.resize(num_channels_, afternoon_pwm.back());
    afternoon_current.resize(num_channels_, afternoon_current.back());
    
    std::vector<float> evening_pwm = {20, 30, 40, 60, 40, 30, 20, 10};
    std::vector<float> evening_current = {0.3f, 0.4f, 0.6f, 1.0f, 0.6f, 0.4f, 0.3f, 0.1f};
    evening_pwm.resize(num_channels_, evening_pwm.back());
    evening_current.resize(num_channels_, evening_current.back());
    
    add_schedule_point(SchedulePoint(480, morning_pwm, morning_current));     // 8 AM
    add_schedule_point(SchedulePoint(720, noon_pwm, noon_current));          // 12 PM
    add_schedule_point(SchedulePoint(960, afternoon_pwm, afternoon_current)); // 4 PM
    add_schedule_point(SchedulePoint(1200, evening_pwm, evening_current));   // 8 PM
}

void LEDScheduler::create_simple_preset() {
    clear_schedule();
    
    // Simple on/off schedule
    add_schedule_point(SchedulePoint(480,  // 8 AM - On
        std::vector<float>(num_channels_, 70.0f),
        std::vector<float>(num_channels_, 1.2f)));
    
    add_schedule_point(SchedulePoint(1200, // 8 PM - Off
        std::vector<float>(num_channels_, 0.0f),
        std::vector<float>(num_channels_, 0.0f)));
}

void LEDScheduler::create_dynamic_sunrise_sunset_preset() {
    clear_schedule();
    
    // Pre-dawn (30 minutes before sunrise) - Moonlight simulation
    add_dynamic_schedule_point(DynamicTimeType::SUNRISE_RELATIVE, -30,
        std::vector<float>(num_channels_, 5.0f),   // 5% PWM - moonlight
        std::vector<float>(num_channels_, 0.1f));  // 0.1A current
    
    // Sunrise - Dawn begins
    add_dynamic_schedule_point(DynamicTimeType::SUNRISE_RELATIVE, 0,
        std::vector<float>(num_channels_, 20.0f),  // 20% PWM
        std::vector<float>(num_channels_, 0.3f));  // 0.3A current
    
    // Post-sunrise (30 minutes after) - Morning ramp up
    add_dynamic_schedule_point(DynamicTimeType::SUNRISE_RELATIVE, 30,
        std::vector<float>(num_channels_, 50.0f),  // 50% PWM
        std::vector<float>(num_channels_, 1.0f));  // 1.0A current
    
    // Solar noon - Peak intensity
    add_dynamic_schedule_point(DynamicTimeType::SOLAR_NOON, 0,
        std::vector<float>(num_channels_, 85.0f),  // 85% PWM
        std::vector<float>(num_channels_, 1.8f));  // 1.8A current
    
    // Pre-sunset (30 minutes before) - Evening ramp down
    add_dynamic_schedule_point(DynamicTimeType::SUNSET_RELATIVE, -30,
        std::vector<float>(num_channels_, 50.0f),  // 50% PWM
        std::vector<float>(num_channels_, 1.0f));  // 1.0A current
    
    // Sunset - Dusk begins
    add_dynamic_schedule_point(DynamicTimeType::SUNSET_RELATIVE, 0,
        std::vector<float>(num_channels_, 20.0f),  // 20% PWM
        std::vector<float>(num_channels_, 0.3f));  // 0.3A current
    
    // Post-sunset (30 minutes after) - Night/Moonlight
    add_dynamic_schedule_point(DynamicTimeType::SUNSET_RELATIVE, 30,
        std::vector<float>(num_channels_, 5.0f),   // 5% PWM - moonlight
        std::vector<float>(num_channels_, 0.1f));  // 0.1A current
}

LEDScheduler::SerializedData LEDScheduler::serialize() const {
    SerializedData result;
    result.num_points = static_cast<uint16_t>(schedule_points_.size());
    result.num_channels = num_channels_;
    
    // Calculate required size
    size_t required_size = 0;
    for (const auto& point : schedule_points_) {
        required_size += 1; // time_type
        required_size += 2; // time_minutes or offset_minutes
        required_size += 1 + (point.pwm_values.size() * 4); // pwm count + values
        required_size += 1 + (point.current_values.size() * 4); // current count + values
    }
    
    result.data.reserve(required_size);
    size_t pos = 0;
    
    for (const auto& point : schedule_points_) {
        // Write time_type
        result.data.push_back(static_cast<uint8_t>(point.time_type));
        pos++;
        
        // Write time_minutes or offset depending on type
        if (point.time_type == DynamicTimeType::FIXED) {
            write_uint16(result.data, pos, point.time_minutes);
        } else {
            write_uint16(result.data, pos, static_cast<uint16_t>(point.offset_minutes + 1440)); // Store as unsigned
        }
        
        // Write PWM values
        result.data.push_back(static_cast<uint8_t>(point.pwm_values.size()));
        pos++;
        for (float value : point.pwm_values) {
            write_float(result.data, pos, value);
        }
        
        // Write current values
        result.data.push_back(static_cast<uint8_t>(point.current_values.size()));
        pos++;
        for (float value : point.current_values) {
            write_float(result.data, pos, value);
        }
    }
    
    return result;
}

bool LEDScheduler::deserialize(const SerializedData& data) {
    if (data.num_channels == 0 || data.num_channels > 16) {
        return false;
    }
    
    std::vector<SchedulePoint> new_points;
    size_t pos = 0;
    
    for (uint16_t i = 0; i < data.num_points; i++) {
        if (pos + 3 > data.data.size()) return false; // Need at least type + 2 bytes for time/offset
        
        SchedulePoint point;
        
        // Read time_type
        point.time_type = static_cast<DynamicTimeType>(data.data[pos++]);
        
        // Read time_minutes or offset depending on type
        uint16_t time_value = read_uint16(data.data, pos);
        if (point.time_type == DynamicTimeType::FIXED) {
            point.time_minutes = time_value;
            point.offset_minutes = 0;
        } else {
            point.time_minutes = 0; // Will be calculated
            point.offset_minutes = static_cast<int16_t>(time_value) - 1440; // Convert back from unsigned
        }
        
        // Read PWM values
        if (pos >= data.data.size()) return false;
        uint8_t pwm_count = data.data[pos++];
        point.pwm_values.reserve(pwm_count);
        
        for (uint8_t j = 0; j < pwm_count; j++) {
            if (pos + 4 > data.data.size()) return false;
            point.pwm_values.push_back(read_float(data.data, pos));
        }
        
        // Read current values
        if (pos >= data.data.size()) return false;
        uint8_t current_count = data.data[pos++];
        point.current_values.reserve(current_count);
        
        for (uint8_t j = 0; j < current_count; j++) {
            if (pos + 4 > data.data.size()) return false;
            point.current_values.push_back(read_float(data.data, pos));
        }
        
        // Validate that the point has the correct number of channels
        if (point.pwm_values.size() != data.num_channels || 
            point.current_values.size() != data.num_channels) {
            return false;  // Invalid data - channel count mismatch
        }
        
        new_points.push_back(point);
    }
    
    // Success - update state
    num_channels_ = data.num_channels;
    schedule_points_ = std::move(new_points);
    sort_schedule_points();
    return true;
}

std::string LEDScheduler::export_json() const {
    // Create root object
    cJSON* root = cJSON_CreateObject();
    if (!root) return "{}";
    
    // Use RAII to ensure cleanup
    struct JSONDeleter {
        cJSON* json;
        ~JSONDeleter() { if (json) cJSON_Delete(json); }
    } deleter{root};
    
    // Add num_channels
    cJSON_AddNumberToObject(root, "num_channels", num_channels_);
    
    // Add astronomical times if available
    cJSON* astro_obj = cJSON_CreateObject();
    if (astro_obj) {
        cJSON_AddNumberToObject(astro_obj, "sunrise_minutes", astronomical_times_.sunrise_minutes);
        cJSON_AddNumberToObject(astro_obj, "sunset_minutes", astronomical_times_.sunset_minutes);
        cJSON_AddNumberToObject(astro_obj, "civil_dawn_minutes", astronomical_times_.civil_dawn_minutes);
        cJSON_AddNumberToObject(astro_obj, "civil_dusk_minutes", astronomical_times_.civil_dusk_minutes);
        cJSON_AddNumberToObject(astro_obj, "nautical_dawn_minutes", astronomical_times_.nautical_dawn_minutes);
        cJSON_AddNumberToObject(astro_obj, "nautical_dusk_minutes", astronomical_times_.nautical_dusk_minutes);
        cJSON_AddNumberToObject(astro_obj, "solar_noon_minutes", astronomical_times_.solar_noon_minutes);
        cJSON_AddItemToObject(root, "astronomical_times", astro_obj);
    }
    
    // Add channel_configs array
    cJSON* channels_array = cJSON_CreateArray();
    if (channels_array) {
        for (uint8_t i = 0; i < num_channels_; i++) {
            cJSON* channel_obj = cJSON_CreateObject();
            if (channel_obj) {
                cJSON_AddStringToObject(channel_obj, "rgb_hex", channel_configs_[i].rgb_hex.c_str());
                cJSON_AddNumberToObject(channel_obj, "max_current", channel_configs_[i].max_current);
                cJSON_AddStringToObject(channel_obj, "name", channel_configs_[i].name.c_str());
                cJSON_AddItemToArray(channels_array, channel_obj);
            }
        }
        cJSON_AddItemToObject(root, "channel_configs", channels_array);
    }
    
    // Create schedule_points array
    cJSON* points_array = cJSON_CreateArray();
    if (!points_array) return "{}";
    cJSON_AddItemToObject(root, "schedule_points", points_array);
    
    // Add each schedule point
    for (const auto& point : schedule_points_) {
        cJSON* point_obj = cJSON_CreateObject();
        if (!point_obj) continue;
        
        // Add time_type
        cJSON_AddStringToObject(point_obj, "time_type", 
                               dynamic_time_type_to_string(point.time_type).c_str());
        
        // Add offset_minutes for dynamic points
        if (point.time_type != DynamicTimeType::FIXED) {
            cJSON_AddNumberToObject(point_obj, "offset_minutes", point.offset_minutes);
        }
        
        // Calculate actual time for dynamic points
        uint16_t actual_time = point.time_minutes;
        if (point.time_type != DynamicTimeType::FIXED) {
            actual_time = calculate_dynamic_time(point, astronomical_times_);
        }
        
        // Add time_minutes (calculated for dynamic points)
        cJSON_AddNumberToObject(point_obj, "time_minutes", actual_time);
        
        // Add time_formatted
        char time_str[6];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", 
                 actual_time / 60, actual_time % 60);
        cJSON_AddStringToObject(point_obj, "time_formatted", time_str);
        
        // Add pwm_values array
        cJSON* pwm_array = cJSON_CreateArray();
        if (pwm_array) {
            for (float value : point.pwm_values) {
                cJSON* number = cJSON_CreateNumber(value);
                if (number) cJSON_AddItemToArray(pwm_array, number);
            }
            cJSON_AddItemToObject(point_obj, "pwm_values", pwm_array);
        }
        
        // Add current_values array
        cJSON* current_array = cJSON_CreateArray();
        if (current_array) {
            for (float value : point.current_values) {
                cJSON* number = cJSON_CreateNumber(value);
                if (number) cJSON_AddItemToArray(current_array, number);
            }
            cJSON_AddItemToObject(point_obj, "current_values", current_array);
        }
        
        // Add point to array
        cJSON_AddItemToArray(points_array, point_obj);
    }
    
    // Add moon simulation configuration
    cJSON* moon_obj = cJSON_CreateObject();
    if (moon_obj) {
        cJSON_AddBoolToObject(moon_obj, "enabled", moon_simulation_.enabled);
        cJSON_AddBoolToObject(moon_obj, "phase_scaling", moon_simulation_.phase_scaling);
        
        // Add base_intensity array
        cJSON* intensity_array = cJSON_CreateArray();
        if (intensity_array) {
            for (float intensity : moon_simulation_.base_intensity) {
                cJSON* number = cJSON_CreateNumber(intensity);
                if (number) cJSON_AddItemToArray(intensity_array, number);
            }
            cJSON_AddItemToObject(moon_obj, "base_intensity", intensity_array);
        }
        
        cJSON_AddItemToObject(root, "moon_simulation", moon_obj);
    }
    
    // Convert to string with formatting
    char* json_str = cJSON_Print(root);
    if (!json_str) return "{}";
    
    std::string result(json_str);
    cJSON_free(json_str);
    
    return result;
}

std::string LEDScheduler::export_json_minified() const {
    // Create root object
    cJSON* root = cJSON_CreateObject();
    if (!root) return "{}";
    
    // Use RAII to ensure cleanup
    struct JSONDeleter {
        cJSON* json;
        ~JSONDeleter() { if (json) cJSON_Delete(json); }
    } deleter{root};
    
    // Add num_channels
    cJSON_AddNumberToObject(root, "num_channels", num_channels_);
    
    // Add channel_configs array
    cJSON* channels_array = cJSON_CreateArray();
    if (channels_array) {
        for (uint8_t i = 0; i < num_channels_; i++) {
            cJSON* channel_obj = cJSON_CreateObject();
            if (channel_obj) {
                cJSON_AddStringToObject(channel_obj, "rgb_hex", channel_configs_[i].rgb_hex.c_str());
                cJSON_AddNumberToObject(channel_obj, "max_current", channel_configs_[i].max_current);
                cJSON_AddStringToObject(channel_obj, "name", channel_configs_[i].name.c_str());
                cJSON_AddItemToArray(channels_array, channel_obj);
            }
        }
        cJSON_AddItemToObject(root, "channel_configs", channels_array);
    }
    
    // Create schedule_points array
    cJSON* points_array = cJSON_CreateArray();
    if (!points_array) return "{}";
    cJSON_AddItemToObject(root, "schedule_points", points_array);
    
    // Add each schedule point
    for (const auto& point : schedule_points_) {
        cJSON* point_obj = cJSON_CreateObject();
        if (!point_obj) continue;
        
        // Add time_type
        cJSON_AddStringToObject(point_obj, "time_type", 
                               dynamic_time_type_to_string(point.time_type).c_str());
        
        // Add offset_minutes for dynamic points
        if (point.time_type != DynamicTimeType::FIXED) {
            cJSON_AddNumberToObject(point_obj, "offset_minutes", point.offset_minutes);
        }
        
        // Calculate actual time for dynamic points
        uint16_t actual_time = point.time_minutes;
        if (point.time_type != DynamicTimeType::FIXED) {
            actual_time = calculate_dynamic_time(point, astronomical_times_);
        }
        
        // Add time_minutes (calculated for dynamic points)
        cJSON_AddNumberToObject(point_obj, "time_minutes", actual_time);
        
        // Add time_formatted
        char time_str[6];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", 
                 actual_time / 60, actual_time % 60);
        cJSON_AddStringToObject(point_obj, "time_formatted", time_str);
        
        // Add pwm_values array
        cJSON* pwm_array = cJSON_CreateArray();
        if (pwm_array) {
            for (float value : point.pwm_values) {
                cJSON* number = cJSON_CreateNumber(value);
                if (number) cJSON_AddItemToArray(pwm_array, number);
            }
            cJSON_AddItemToObject(point_obj, "pwm_values", pwm_array);
        }
        
        // Add current_values array
        cJSON* current_array = cJSON_CreateArray();
        if (current_array) {
            for (float value : point.current_values) {
                cJSON* number = cJSON_CreateNumber(value);
                if (number) cJSON_AddItemToArray(current_array, number);
            }
            cJSON_AddItemToObject(point_obj, "current_values", current_array);
        }
        
        // Add point to array
        cJSON_AddItemToArray(points_array, point_obj);
    }
    
    // Add moon simulation configuration
    cJSON* moon_obj = cJSON_CreateObject();
    if (moon_obj) {
        cJSON_AddBoolToObject(moon_obj, "enabled", moon_simulation_.enabled);
        cJSON_AddBoolToObject(moon_obj, "phase_scaling", moon_simulation_.phase_scaling);
        
        // Add base_intensity array
        cJSON* intensity_array = cJSON_CreateArray();
        if (intensity_array) {
            for (float intensity : moon_simulation_.base_intensity) {
                cJSON* number = cJSON_CreateNumber(intensity);
                if (number) cJSON_AddItemToArray(intensity_array, number);
            }
            cJSON_AddItemToObject(moon_obj, "base_intensity", intensity_array);
        }
        
        cJSON_AddItemToObject(root, "moon_simulation", moon_obj);
    }
    
    // Convert to string WITHOUT formatting (minified)
    char* json_str = cJSON_PrintUnformatted(root);
    if (!json_str) return "{}";
    
    std::string result(json_str);
    cJSON_free(json_str);
    
    return result;
}

bool LEDScheduler::import_json(const std::string& json_str) {
    // Parse JSON using cJSON library
    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        return false;
    }
    
    // Use RAII to ensure cleanup
    struct JSONDeleter {
        cJSON* json;
        ~JSONDeleter() { if (json) cJSON_Delete(json); }
    } deleter{root};
    
    // Clear existing schedule
    clear_schedule();
    
    // Parse num_channels
    cJSON* num_channels_item = cJSON_GetObjectItem(root, "num_channels");
    if (cJSON_IsNumber(num_channels_item)) {
        set_num_channels(static_cast<uint8_t>(num_channels_item->valueint));
    }
    
    // Parse channel_configs array
    cJSON* channels_array = cJSON_GetObjectItem(root, "channel_configs");
    if (cJSON_IsArray(channels_array)) {
        int channel_idx = 0;
        cJSON* channel_item = NULL;
        cJSON_ArrayForEach(channel_item, channels_array) {
            if (channel_idx >= num_channels_) break;
            if (!cJSON_IsObject(channel_item)) continue;
            
            ChannelConfig config;
            
            cJSON* rgb_hex_item = cJSON_GetObjectItem(channel_item, "rgb_hex");
            if (cJSON_IsString(rgb_hex_item)) {
                config.rgb_hex = rgb_hex_item->valuestring;
            }
            
            cJSON* max_current_item = cJSON_GetObjectItem(channel_item, "max_current");
            if (cJSON_IsNumber(max_current_item)) {
                config.max_current = static_cast<float>(max_current_item->valuedouble);
            }
            
            cJSON* name_item = cJSON_GetObjectItem(channel_item, "name");
            if (cJSON_IsString(name_item)) {
                config.name = name_item->valuestring;
            }
            
            set_channel_config(channel_idx, config);
            channel_idx++;
        }
    }
    
    // Parse schedule_points array
    cJSON* points_array = cJSON_GetObjectItem(root, "schedule_points");
    if (!cJSON_IsArray(points_array)) {
        return false;
    }
    
    cJSON* point_item = NULL;
    cJSON_ArrayForEach(point_item, points_array) {
        if (!cJSON_IsObject(point_item)) continue;
        
        // Parse time_type
        DynamicTimeType time_type = DynamicTimeType::FIXED;
        cJSON* time_type_item = cJSON_GetObjectItem(point_item, "time_type");
        if (cJSON_IsString(time_type_item)) {
            time_type = string_to_dynamic_time_type(time_type_item->valuestring);
        }
        
        // Parse offset_minutes (for dynamic points)
        int offset_minutes = 0;
        cJSON* offset_item = cJSON_GetObjectItem(point_item, "offset_minutes");
        if (cJSON_IsNumber(offset_item)) {
            offset_minutes = offset_item->valueint;
        }
        
        // Parse time_minutes
        uint16_t time_minutes = 0;
        cJSON* time_item = cJSON_GetObjectItem(point_item, "time_minutes");
        if (cJSON_IsNumber(time_item)) {
            time_minutes = static_cast<uint16_t>(time_item->valueint);
        }
        
        // Parse pwm_values array
        std::vector<float> pwm_values;
        cJSON* pwm_array = cJSON_GetObjectItem(point_item, "pwm_values");
        if (cJSON_IsArray(pwm_array)) {
            cJSON* value = NULL;
            cJSON_ArrayForEach(value, pwm_array) {
                if (cJSON_IsNumber(value)) {
                    pwm_values.push_back(static_cast<float>(value->valuedouble));
                }
            }
        }
        
        // Parse current_values array
        std::vector<float> current_values;
        cJSON* current_array = cJSON_GetObjectItem(point_item, "current_values");
        if (cJSON_IsArray(current_array)) {
            cJSON* value = NULL;
            cJSON_ArrayForEach(value, current_array) {
                if (cJSON_IsNumber(value)) {
                    current_values.push_back(static_cast<float>(value->valuedouble));
                }
            }
        }
        
        // Add the schedule point
        if (!pwm_values.empty() && !current_values.empty()) {
            if (time_type == DynamicTimeType::FIXED) {
                set_schedule_point(time_minutes, pwm_values, current_values);
            } else {
                add_dynamic_schedule_point(time_type, offset_minutes, pwm_values, current_values);
            }
        }
    }
    
    // Parse moon simulation configuration
    cJSON* moon_obj = cJSON_GetObjectItem(root, "moon_simulation");
    if (cJSON_IsObject(moon_obj)) {
        MoonSimulation moon_config;
        
        // Parse enabled
        cJSON* enabled_item = cJSON_GetObjectItem(moon_obj, "enabled");
        if (cJSON_IsBool(enabled_item)) {
            moon_config.enabled = cJSON_IsTrue(enabled_item);
        }
        
        // Parse phase_scaling
        cJSON* phase_scaling_item = cJSON_GetObjectItem(moon_obj, "phase_scaling");
        if (cJSON_IsBool(phase_scaling_item)) {
            moon_config.phase_scaling = cJSON_IsTrue(phase_scaling_item);
        }
        
        // Parse base_intensity array
        cJSON* intensity_array = cJSON_GetObjectItem(moon_obj, "base_intensity");
        if (cJSON_IsArray(intensity_array)) {
            moon_config.base_intensity.clear();
            cJSON* intensity_item = NULL;
            cJSON_ArrayForEach(intensity_item, intensity_array) {
                if (cJSON_IsNumber(intensity_item)) {
                    moon_config.base_intensity.push_back(static_cast<float>(intensity_item->valuedouble));
                }
            }
        }
        
        // Apply moon simulation configuration
        set_moon_simulation(moon_config);
    }
    
    return !schedule_points_.empty();
}

void LEDScheduler::write_uint16(std::vector<uint8_t>& data, size_t& pos, uint16_t value) const {
    data.resize(pos + 2);
    data[pos++] = value & 0xFF;
    data[pos++] = (value >> 8) & 0xFF;
}

void LEDScheduler::write_float(std::vector<uint8_t>& data, size_t& pos, float value) const {
    data.resize(pos + 4);
    uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
    data[pos++] = (bits >> 0) & 0xFF;
    data[pos++] = (bits >> 8) & 0xFF;
    data[pos++] = (bits >> 16) & 0xFF;
    data[pos++] = (bits >> 24) & 0xFF;
}

uint16_t LEDScheduler::read_uint16(const std::vector<uint8_t>& data, size_t& pos) const {
    uint16_t value = data[pos] | (data[pos + 1] << 8);
    pos += 2;
    return value;
}

float LEDScheduler::read_float(const std::vector<uint8_t>& data, size_t& pos) const {
    uint32_t bits = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
    pos += 4;
    return *reinterpret_cast<const float*>(&bits);
}

// Channel configuration methods
void LEDScheduler::set_channel_config(uint8_t channel, const ChannelConfig& config) {
    if (channel < num_channels_) {
        channel_configs_[channel] = config;
    }
}

LEDScheduler::ChannelConfig LEDScheduler::get_channel_config(uint8_t channel) const {
    if (channel < num_channels_) {
        return channel_configs_[channel];
    }
    return ChannelConfig();
}

void LEDScheduler::set_channel_color(uint8_t channel, const std::string& rgb_hex) {
    if (channel < num_channels_) {
        channel_configs_[channel].rgb_hex = rgb_hex;
    }
}

void LEDScheduler::set_channel_max_current(uint8_t channel, float max_current) {
    if (channel < num_channels_) {
        // Clamp to valid range
        if (max_current < 0.1f) max_current = 0.1f;
        if (max_current > 2.0f) max_current = 2.0f;
        channel_configs_[channel].max_current = max_current;
    }
}

std::string LEDScheduler::get_channel_color(uint8_t channel) const {
    if (channel < num_channels_) {
        return channel_configs_[channel].rgb_hex;
    }
    return "#FFFFFF";
}

float LEDScheduler::get_channel_max_current(uint8_t channel) const {
    if (channel < num_channels_) {
        return channel_configs_[channel].max_current;
    }
    return 2.0f;
}
