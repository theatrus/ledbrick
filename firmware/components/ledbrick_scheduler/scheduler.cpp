#include "scheduler.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

LEDScheduler::LEDScheduler(uint8_t num_channels) 
    : num_channels_(num_channels) {
}

void LEDScheduler::set_num_channels(uint8_t channels) {
    if (channels < 1 || channels > 16) {
        return; // Invalid channel count
    }
    num_channels_ = channels;
    
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
        result.pwm_values = before->pwm_values;
        result.current_values = before->current_values;
        result.valid = true;
        return result;
    }
    
    // Interpolate
    if (before && after) {
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
    
    for (float current : point.current_values) {
        if (current < 0.0f || current > 5.0f) {
            return false;
        }
    }
    
    return true;
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
        
        new_points.push_back(point);
    }
    
    // Success - update state
    num_channels_ = data.num_channels;
    schedule_points_ = std::move(new_points);
    sort_schedule_points();
    return true;
}

std::string LEDScheduler::export_json() const {
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    
    json << "{\n";
    json << "  \"num_channels\": " << static_cast<int>(num_channels_) << ",\n";
    json << "  \"schedule_points\": [\n";
    
    for (size_t i = 0; i < schedule_points_.size(); i++) {
        const auto& point = schedule_points_[i];
        
        json << "    {\n";
        
        // Add time type information
        json << "      \"time_type\": \"" << dynamic_time_type_to_string(point.time_type) << "\",\n";
        
        if (point.time_type != DynamicTimeType::FIXED) {
            json << "      \"offset_minutes\": " << point.offset_minutes << ",\n";
        }
        
        json << "      \"time_minutes\": " << point.time_minutes << ",\n";
        json << "      \"time_formatted\": \"" << std::setfill('0') << std::setw(2) 
             << (point.time_minutes / 60) << ":" << std::setfill('0') << std::setw(2) 
             << (point.time_minutes % 60) << "\",\n";
        
        json << "      \"pwm_values\": [";
        for (size_t j = 0; j < point.pwm_values.size(); j++) {
            if (j > 0) json << ", ";
            json << point.pwm_values[j];
        }
        json << "],\n";
        
        json << "      \"current_values\": [";
        for (size_t j = 0; j < point.current_values.size(); j++) {
            if (j > 0) json << ", ";
            json << point.current_values[j];
        }
        json << "]\n";
        
        json << "    }";
        if (i < schedule_points_.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    return json.str();
}

bool LEDScheduler::import_json(const std::string& json_str) {
    // Simple JSON parser for our specific format
    // This is a basic implementation - in production you'd use a proper JSON library
    
    // For now, return false as we'd need a proper JSON parser
    // This is a placeholder for the full implementation
    return false;
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