#include "scheduler.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdio>

// Use cJSON for lightweight JSON parsing
// cJSON is a single-file library perfect for embedded systems
extern "C" {
    #include "cJSON.h"
}

constexpr float LEDScheduler::MAX_CHANNEL_CURRENT;
constexpr float LEDScheduler::MIN_FLOOR_CURRENT;

namespace {

// Clamp to [low, high]; NaN becomes low
float clamp_value(float value, float low, float high) {
    if (!(value >= low)) return low;
    if (value > high) return high;
    return value;
}

// Round to 3 decimals for JSON output. A float widened to double prints with 17 digits
// (0.65f becomes 0.64999997615814209), which bloats the schedule saved to flash.
double json_number(float value) {
    return std::round(static_cast<double>(value) * 1000.0) / 1000.0;
}

constexpr float DEFAULT_FLOOR_CURRENT = 0.1f;

const char* dim_mode_name(ledbrick::DimMode mode) {
    return mode == ledbrick::DimMode::CURVE ? "curve" : "manual";
}

const char* dim_priority_name(ledbrick::DimPriority priority) {
    return priority == ledbrick::DimPriority::PWM_FIRST ? "pwm" : "current";
}

bool fail_with(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

// A curve as [[x,y],...], added raw: a number node for every point would take a lot of
// heap, and 4 digits keep the saved settings small
cJSON* curve_json(const std::vector<ledbrick::CurvePoint>& curve) {
    std::string text = "[";
    char point[48];
    for (size_t i = 0; i < curve.size(); i++) {
        snprintf(point, sizeof(point), "%s[%.4g,%.4g]", i > 0 ? "," : "", curve[i].x, curve[i].y);
        text += point;
    }
    text += "]";
    return cJSON_CreateRaw(text.c_str());
}

cJSON* led_model_json(const ledbrick::LedModel& model) {
    cJSON* obj = cJSON_CreateObject();
    if (!obj) {
        return nullptr;
    }
    cJSON_AddStringToObject(obj, "id", model.id.c_str());
    cJSON_AddStringToObject(obj, "name", model.name.c_str());
    if (model.test_current_a > 0.0f) {
        cJSON_AddNumberToObject(obj, "test_current", json_number(model.test_current_a));
    }
    cJSON_AddNumberToObject(obj, "max_current", json_number(model.max_current_a));
    cJSON_AddItemToObject(obj, "output_vs_current", curve_json(model.output_vs_current));
    if (!model.output_vs_temp.empty()) {
        cJSON_AddNumberToObject(obj, "curve_temp", json_number(model.curve_temp_c));
        cJSON_AddItemToObject(obj, "output_vs_temp", curve_json(model.output_vs_temp));
    }
    if (model.rth_c_per_w > 0.0f) {
        cJSON_AddNumberToObject(obj, "rth", json_number(model.rth_c_per_w));
    }
    if (!model.vf_vs_current.empty()) {
        cJSON_AddItemToObject(obj, "vf_vs_current", curve_json(model.vf_vs_current));
    }
    return obj;
}

cJSON* led_models_json(const std::vector<ledbrick::LedModel>& models) {
    cJSON* array = cJSON_CreateArray();
    if (array) {
        for (const auto& model : models) {
            cJSON* item = led_model_json(model);
            if (item) {
                cJSON_AddItemToArray(array, item);
            }
        }
    }
    return array;
}

// Unformatted JSON of root, which is deleted; "{}" when out of memory
std::string print_and_delete(cJSON* root) {
    char* printed = root ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    if (!printed) {
        return "{}";
    }
    std::string result(printed);
    cJSON_free(printed);
    return result;
}

// {"led_models":[...]}: how custom models are posted and saved
std::string custom_models_document(const std::vector<ledbrick::LedModel>& models) {
    cJSON* root = cJSON_CreateObject();
    if (root) {
        cJSON_AddItemToObject(root, "led_models", led_models_json(models));
    }
    return print_and_delete(root);
}

// [[x,y],...]; validate_led_model checks the values
bool parse_curve_json(const cJSON* item, const char* name, std::vector<ledbrick::CurvePoint>& curve,
                      std::string* error) {
    curve.clear();
    if (!cJSON_IsArray(item)) {
        return fail_with(error, std::string(name) + " must be a list of [x, y] points");
    }
    const cJSON* point = nullptr;
    cJSON_ArrayForEach(point, item) {
        if (curve.size() >= ledbrick::MAX_CURVE_POINTS) {
            return fail_with(error, std::string(name) + " has over " +
                                        std::to_string(ledbrick::MAX_CURVE_POINTS) + " points");
        }
        const cJSON* x = cJSON_GetArrayItem(point, 0);
        const cJSON* y = cJSON_GetArrayItem(point, 1);
        if (!cJSON_IsArray(point) || cJSON_GetArraySize(point) != 2 || !cJSON_IsNumber(x) || !cJSON_IsNumber(y)) {
            return fail_with(error, std::string(name) + " must be a list of [x, y] points");
        }
        curve.push_back({static_cast<float>(x->valuedouble), static_cast<float>(y->valuedouble)});
    }
    return true;
}

bool parse_led_model_json(const cJSON* item, ledbrick::LedModel& model, std::string* error) {
    if (!cJSON_IsObject(item)) {
        return fail_with(error, "each LED model must be an object");
    }
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!cJSON_IsString(id) || id->valuestring[0] == '\0' || strlen(id->valuestring) > ledbrick::MAX_LED_MODEL_ID) {
        return fail_with(error, "each LED model needs an id of 1-" + std::to_string(ledbrick::MAX_LED_MODEL_ID) +
                                    " characters");
    }
    model = ledbrick::LedModel();
    model.id = id->valuestring;
    const std::string where = "LED model " + model.id + ": ";

    const cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (name != nullptr && !cJSON_IsString(name)) {
        return fail_with(error, where + "name must be text");
    }
    model.name = cJSON_IsString(name) && name->valuestring[0] != '\0' ? name->valuestring : model.id;

    // Optional values keep their defaults when left out
    auto read_number = [&](const char* key, bool required, float& out) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(item, key);
        if (value == nullptr) {
            return required ? fail_with(error, where + key + " is required") : true;
        }
        if (!cJSON_IsNumber(value)) {
            return fail_with(error, where + key + " must be a number");
        }
        out = static_cast<float>(value->valuedouble);
        return true;
    };
    auto read_curve = [&](const char* key, bool required, std::vector<ledbrick::CurvePoint>& out) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(item, key);
        if (value == nullptr) {
            return required ? fail_with(error, where + key + " is required") : true;
        }
        std::string curve_error;
        if (!parse_curve_json(value, key, out, &curve_error)) {
            return fail_with(error, where + curve_error);
        }
        return true;
    };
    if (!read_number("test_current", false, model.test_current_a) ||
        !read_number("max_current", true, model.max_current_a) ||
        !read_curve("output_vs_current", true, model.output_vs_current) ||
        !read_curve("output_vs_temp", false, model.output_vs_temp) ||
        !read_number("curve_temp", !model.output_vs_temp.empty(), model.curve_temp_c) ||
        !read_number("rth", false, model.rth_c_per_w) ||
        !read_curve("vf_vs_current", false, model.vf_vs_current)) {
        return false;
    }
    return ledbrick::validate_led_model(model, error);
}

// At most MAX_CUSTOM_LED_MODELS valid models, each id once
bool valid_custom_models(const std::vector<ledbrick::LedModel>& models, std::string* error) {
    if (models.size() > ledbrick::MAX_CUSTOM_LED_MODELS) {
        return fail_with(error, "at most " + std::to_string(ledbrick::MAX_CUSTOM_LED_MODELS) + " custom LED models");
    }
    for (size_t i = 0; i < models.size(); i++) {
        if (!ledbrick::validate_led_model(models[i], error)) {
            return false;
        }
        for (size_t j = 0; j < i; j++) {
            if (models[j].id == models[i].id) {
                return fail_with(error, "LED model " + models[i].id + " is listed twice");
            }
        }
    }
    return true;
}

bool parse_led_models_json(const cJSON* array, std::vector<ledbrick::LedModel>& models, std::string* error) {
    models.clear();
    if (!cJSON_IsArray(array)) {
        return fail_with(error, "led_models must be a list");
    }
    if (cJSON_GetArraySize(array) > static_cast<int>(ledbrick::MAX_CUSTOM_LED_MODELS)) {
        return fail_with(error, "at most " + std::to_string(ledbrick::MAX_CUSTOM_LED_MODELS) + " custom LED models");
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        ledbrick::LedModel model;
        if (!parse_led_model_json(item, model, error)) {
            return false;
        }
        models.push_back(std::move(model));
    }
    return valid_custom_models(models, error);
}

}  // namespace

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
    
    return interpolate_values_with_astro(static_cast<float>(current_time_minutes), astro_times);
}

LEDScheduler::InterpolationResult LEDScheduler::get_values_at_seconds_with_astro(uint32_t second_of_day,
                                                                               const AstronomicalTimes& astro_times) const {
    if (second_of_day >= 1440u * 60u) {
        return InterpolationResult(); // Invalid time
    }

    return interpolate_values_with_astro(second_of_day / 60.0f, astro_times);
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
        // Clamp current values to max current
        for (size_t i = 0; i < num_channels_; i++) {
            if (result.current_values[i] > channel_configs_[i].max_current) {
                result.current_values[i] = channel_configs_[i].max_current;
            }
        }
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
            // Clamp to max current
            if (result.current_values[i] > channel_configs_[i].max_current) {
                result.current_values[i] = channel_configs_[i].max_current;
            }
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
            // Clamp to max current
            if (result.current_values[i] > channel_configs_[i].max_current) {
                result.current_values[i] = channel_configs_[i].max_current;
            }
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
            // Clamp to max current
            if (result.current_values[i] > channel_configs_[i].max_current) {
                result.current_values[i] = channel_configs_[i].max_current;
            }
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

LEDScheduler::InterpolationResult LEDScheduler::interpolate_values_with_astro(float current_time,
                                                                            const AstronomicalTimes& astro_times) const {
    // Resolve all dynamic points to actual times
    auto resolved_points = resolve_dynamic_points(astro_times);
    
    InterpolationResult result;
    result.pwm_values.resize(num_channels_, 0.0f);
    result.current_values.resize(num_channels_, 0.0f);
    
    if (resolved_points.empty()) {
        return result;
    }
    
    if (resolved_points.size() == 1) {
        // Safety check - ensure vectors have correct size
        if (resolved_points[0].pwm_values.size() != num_channels_ || 
            resolved_points[0].current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        result.pwm_values = resolved_points[0].pwm_values;
        result.current_values = resolved_points[0].current_values;
        result.valid = true;
    } else {
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
        if (!before) {
            before = &resolved_points.back();
        }
        
        // If current time is after last point, wrap to use first point as 'after'
        if (!after) {
            after = &resolved_points.front();
        }
        
        // Safety check - ensure vectors have correct size
        if (before->pwm_values.size() != num_channels_ || 
            before->current_values.size() != num_channels_ ||
            after->pwm_values.size() != num_channels_ || 
            after->current_values.size() != num_channels_) {
            return result;  // Return zeros if data is invalid
        }
        
        // An exact match on a point gives elapsed = 0, so the same path handles it
        // and still gets the current clamp and moon simulation below
        uint16_t time_span = after->time_minutes > before->time_minutes ?
            after->time_minutes - before->time_minutes :
            (1440 - before->time_minutes) + after->time_minutes; // Handle wrap-around

        float elapsed = current_time >= before->time_minutes ?
            current_time - before->time_minutes :
            (1440 - before->time_minutes) + current_time; // Handle wrap-around

        float ratio = time_span > 0 ? elapsed / time_span : 0.0f;
        
        for (size_t i = 0; i < num_channels_; i++) {
            result.pwm_values[i] = before->pwm_values[i] + ratio * (after->pwm_values[i] - before->pwm_values[i]);
            result.current_values[i] = before->current_values[i] + ratio * (after->current_values[i] - before->current_values[i]);
        }
        result.valid = true;
    }
    
    // Clamp to each channel's max current
    for (size_t i = 0; i < num_channels_ && i < channel_configs_.size(); i++) {
        if (result.current_values[i] > channel_configs_[i].max_current) {
            result.current_values[i] = channel_configs_[i].max_current;
        }
    }
    
    // Apply moon simulation if enabled
    if (moon_simulation_.enabled && astro_times.valid) {
        result = apply_moon_simulation(result, static_cast<uint16_t>(current_time), astro_times);
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
    // Resize base_intensity and base_current to match channel count
    moon_simulation_.base_intensity.resize(num_channels_, 0.0f);
    moon_simulation_.base_current.resize(num_channels_, 0.0f);
    
    // No need for special backward compatibility here since the struct constructors handle it
}

void LEDScheduler::enable_moon_simulation(bool enabled) {
    moon_simulation_.enabled = enabled;
}

void LEDScheduler::set_moon_base_intensity(const std::vector<float>& intensity) {
    // Note: "intensity" here means PWM percentage (0-100%)
    moon_simulation_.base_intensity = intensity;
    moon_simulation_.base_intensity.resize(num_channels_, 0.0f);
}

void LEDScheduler::set_moon_base_current(const std::vector<float>& current) {
    moon_simulation_.base_current = current;
    moon_simulation_.base_current.resize(num_channels_, 0.0f);
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
        // Moon not visible, return original result
        return result;
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
        // Main lights are on, moon simulation blocked
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
    
    // Apply moon simulation - PWM and current are independent
    for (size_t i = 0; i < num_channels_; i++) {
        // Apply PWM values
        if (i < moon_simulation_.base_intensity.size()) {
            float moon_intensity = moon_simulation_.base_intensity[i];
            
            // Scale by moon phase if enabled (use new field, fallback to legacy)
            if (moon_simulation_.phase_scaling_pwm) {
                moon_intensity *= moon_brightness;
            }
            
            // Apply moonlight PWM
            result.pwm_values[i] = moon_intensity;
        }
        
        // Apply current values independently - no fallback to PWM scaling
        if (i < moon_simulation_.base_current.size()) {
            float moon_current = moon_simulation_.base_current[i];
            
            // Scale by moon phase if enabled (use new field, fallback to legacy)
            if (moon_simulation_.phase_scaling_current) {
                // Apply phase scaling
                float scaled_current = moon_current * moon_brightness;
                
                // Apply minimum threshold if phase scaling is active
                if (moon_current > 0.0f && scaled_current < moon_simulation_.min_current_threshold) {
                    scaled_current = moon_simulation_.min_current_threshold;
                }
                
                result.current_values[i] = scaled_current;
            } else {
                // No phase scaling for current
                result.current_values[i] = moon_current;
            }
        }
        
        // Debug logging for channel 1 (index 0)
        if (i == 0) {
            // LOG_DEBUG("Moon sim ch1: PWM=%.1f%%, Current=%.3fA (base: PWM=%.1f%%, Current=%.3fA, phase=%.2f, brightness=%.2f)", 
            //           result.pwm_values[i], result.current_values[i],
            //           i < moon_simulation_.base_intensity.size() ? moon_simulation_.base_intensity[i] : 0.0f,
            //           i < moon_simulation_.base_current.size() ? moon_simulation_.base_current[i] : 0.0f,
            //           astro_times.moon_phase, moon_brightness);
        }
    }
    
    return result;
}

bool LEDScheduler::load_preset(const std::string& preset_name) {
    // One built-in preset - astronomical schedule
    if (preset_name == "default" || preset_name == "sunrise_sunset") {
        create_default_astronomical_preset();
        return true;
    }

    // Presets kept with save_preset()
    auto it = presets_.find(preset_name);
    if (it != presets_.end()) {
        schedule_points_ = it->second;
        return true;
    }

    return false;
}

void LEDScheduler::save_preset(const std::string& preset_name) {
    presets_[preset_name] = schedule_points_;
}

std::vector<std::string> LEDScheduler::get_preset_names() const {
    std::vector<std::string> names;
    names.push_back("default");
    return names;
}

void LEDScheduler::clear_preset(const std::string& preset_name) {
    presets_.erase(preset_name);
}


void LEDScheduler::create_default_astronomical_preset() {
    clear_schedule();
    
    // Dark before sunrise - 30 minutes before
    add_dynamic_schedule_point(DynamicTimeType::SUNRISE_RELATIVE, -30,
        std::vector<float>(num_channels_, 0.0f),   // 0% PWM - dark
        std::vector<float>(num_channels_, 0.0f));  // 0A current
    
    // Sunrise + 30 minutes - Morning ramp complete
    add_dynamic_schedule_point(DynamicTimeType::SUNRISE_RELATIVE, 30,
        std::vector<float>(num_channels_, 100.0f),  // 100% PWM
        std::vector<float>(num_channels_, 2.0f));   // 2.0A current
    
    // Solar noon - Peak intensity maintained
    add_dynamic_schedule_point(DynamicTimeType::SOLAR_NOON, 0,
        std::vector<float>(num_channels_, 100.0f),  // 100% PWM
        std::vector<float>(num_channels_, 2.0f));   // 2.0A current
    
    // Sunset - 30 minutes - Start ramping down
    add_dynamic_schedule_point(DynamicTimeType::SUNSET_RELATIVE, -30,
        std::vector<float>(num_channels_, 100.0f),  // 100% PWM
        std::vector<float>(num_channels_, 2.0f));   // 2.0A current
    
    // Sunset + 30 minutes - Dark again
    add_dynamic_schedule_point(DynamicTimeType::SUNSET_RELATIVE, 30,
        std::vector<float>(num_channels_, 0.0f),   // 0% PWM - dark
        std::vector<float>(num_channels_, 0.0f));  // 0A current
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
    
    // Resize and initialize channel configs for the new channel count
    channel_configs_.resize(num_channels_);
    const std::vector<std::string> default_colors = {
        "#FFFFFF", "#0000FF", "#00FFFF", "#00FF00",
        "#FF0000", "#FF00FF", "#FFFF00", "#FF8000"
    };
    
    for (uint8_t i = 0; i < num_channels_; i++) {
        if (channel_configs_[i].rgb_hex.empty()) {
            channel_configs_[i].rgb_hex = i < default_colors.size() ? default_colors[i] : "#FFFFFF";
            channel_configs_[i].max_current = 2.0f;
            channel_configs_[i].name = "Channel " + std::to_string(i + 1);
        }
    }
    
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
            add_channel_config_json(channels_array, i, true);
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
        char time_str[8];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", 
                 actual_time / 60, actual_time % 60);
        cJSON_AddStringToObject(point_obj, "time_formatted", time_str);
        
        // Add pwm_values array
        cJSON* pwm_array = cJSON_CreateArray();
        if (pwm_array) {
            for (float value : point.pwm_values) {
                cJSON* number = cJSON_CreateNumber(json_number(value));
                if (number) cJSON_AddItemToArray(pwm_array, number);
            }
            cJSON_AddItemToObject(point_obj, "pwm_values", pwm_array);
        }
        
        // Add current_values array
        cJSON* current_array = cJSON_CreateArray();
        if (current_array) {
            for (float value : point.current_values) {
                cJSON* number = cJSON_CreateNumber(json_number(value));
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
        cJSON_AddBoolToObject(moon_obj, "phase_scaling_pwm", moon_simulation_.phase_scaling_pwm);
        cJSON_AddBoolToObject(moon_obj, "phase_scaling_current", moon_simulation_.phase_scaling_current);
        cJSON_AddNumberToObject(moon_obj, "min_current_threshold", json_number(moon_simulation_.min_current_threshold));
        
        // Add base_intensity array
        cJSON* intensity_array = cJSON_CreateArray();
        if (intensity_array) {
            for (float intensity : moon_simulation_.base_intensity) {
                cJSON* number = cJSON_CreateNumber(json_number(intensity));
                if (number) cJSON_AddItemToArray(intensity_array, number);
            }
            cJSON_AddItemToObject(moon_obj, "base_intensity", intensity_array);
        }
        
        // Add base_current array
        cJSON* current_array = cJSON_CreateArray();
        if (current_array) {
            for (float current : moon_simulation_.base_current) {
                cJSON* number = cJSON_CreateNumber(json_number(current));
                if (number) cJSON_AddItemToArray(current_array, number);
            }
            cJSON_AddItemToObject(moon_obj, "base_current", current_array);
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
            add_channel_config_json(channels_array, i, false);
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
        char time_str[8];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", 
                 actual_time / 60, actual_time % 60);
        cJSON_AddStringToObject(point_obj, "time_formatted", time_str);
        
        // Add pwm_values array
        cJSON* pwm_array = cJSON_CreateArray();
        if (pwm_array) {
            for (float value : point.pwm_values) {
                cJSON* number = cJSON_CreateNumber(json_number(value));
                if (number) cJSON_AddItemToArray(pwm_array, number);
            }
            cJSON_AddItemToObject(point_obj, "pwm_values", pwm_array);
        }
        
        // Add current_values array
        cJSON* current_array = cJSON_CreateArray();
        if (current_array) {
            for (float value : point.current_values) {
                cJSON* number = cJSON_CreateNumber(json_number(value));
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
        cJSON_AddBoolToObject(moon_obj, "phase_scaling_pwm", moon_simulation_.phase_scaling_pwm);
        cJSON_AddBoolToObject(moon_obj, "phase_scaling_current", moon_simulation_.phase_scaling_current);
        cJSON_AddNumberToObject(moon_obj, "min_current_threshold", json_number(moon_simulation_.min_current_threshold));
        
        // Add base_intensity array
        cJSON* intensity_array = cJSON_CreateArray();
        if (intensity_array) {
            for (float intensity : moon_simulation_.base_intensity) {
                cJSON* number = cJSON_CreateNumber(json_number(intensity));
                if (number) cJSON_AddItemToArray(intensity_array, number);
            }
            cJSON_AddItemToObject(moon_obj, "base_intensity", intensity_array);
        }
        
        // Add base_current array
        cJSON* current_array = cJSON_CreateArray();
        if (current_array) {
            for (float current : moon_simulation_.base_current) {
                cJSON* number = cJSON_CreateNumber(json_number(current));
                if (number) cJSON_AddItemToArray(current_array, number);
            }
            cJSON_AddItemToObject(moon_obj, "base_current", current_array);
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

bool LEDScheduler::import_json(const std::string& json_str, std::string* error, bool allow_unknown_models) {
    // Build the new state in a copy so a failed import leaves this one untouched
    LEDScheduler staged(*this);
    if (!staged.import_json_into_(json_str, error, allow_unknown_models)) {
        return false;
    }
    *this = std::move(staged);
    return true;
}

bool LEDScheduler::import_json_into_(const std::string& json_str, std::string* error, bool allow_unknown_models) {
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
    
    if (!cJSON_IsObject(root)) {
        return false;
    }
    
    // Clear existing schedule
    clear_schedule();
    
    // Parse num_channels
    cJSON* num_channels_item = cJSON_GetObjectItem(root, "num_channels");
    if (cJSON_IsNumber(num_channels_item)) {
        if (num_channels_item->valuedouble < 1 || num_channels_item->valuedouble > 16) {
            return false;
        }
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

            if (!parse_channel_dimming_json(cJSON_GetObjectItem(channel_item, "dimming"),
                                            static_cast<uint8_t>(channel_idx), config, allow_unknown_models)) {
                return fail_with(error, "channel " + std::to_string(channel_idx + 1) +
                                            ": invalid dimming settings or unknown LED model");
            }

            // set_channel_config clamps max_current to the hardware limit
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
            if (time_type == DynamicTimeType::FIXED && strcmp(time_type_item->valuestring, "fixed") != 0) {
                return false;  // Unknown time type
            }
        }
        
        // Parse offset_minutes (for dynamic points)
        int offset_minutes = 0;
        cJSON* offset_item = cJSON_GetObjectItem(point_item, "offset_minutes");
        if (cJSON_IsNumber(offset_item)) {
            if (time_type != DynamicTimeType::FIXED &&
                (offset_item->valuedouble < -1439 || offset_item->valuedouble > 1439)) {
                return false;
            }
            offset_minutes = offset_item->valueint;
        }
        
        // Parse time_minutes
        uint16_t time_minutes = 0;
        cJSON* time_item = cJSON_GetObjectItem(point_item, "time_minutes");
        if (cJSON_IsNumber(time_item)) {
            if (time_type == DynamicTimeType::FIXED &&
                (time_item->valuedouble < 0 || time_item->valuedouble >= 1440)) {
                return false;
            }
            time_minutes = static_cast<uint16_t>(time_item->valueint);
        }
        
        // Parse pwm_values array, clamped to 0-100%
        std::vector<float> pwm_values;
        cJSON* pwm_array = cJSON_GetObjectItem(point_item, "pwm_values");
        if (cJSON_IsArray(pwm_array)) {
            cJSON* value = NULL;
            cJSON_ArrayForEach(value, pwm_array) {
                if (cJSON_IsNumber(value)) {
                    pwm_values.push_back(clamp_value(static_cast<float>(value->valuedouble), 0.0f, 100.0f));
                }
            }
        }
        
        // Parse current_values array, clamped to each channel's max current
        std::vector<float> current_values;
        cJSON* current_array = cJSON_GetObjectItem(point_item, "current_values");
        if (cJSON_IsArray(current_array)) {
            cJSON* value = NULL;
            cJSON_ArrayForEach(value, current_array) {
                if (cJSON_IsNumber(value)) {
                    float max_current = get_channel_max_current(static_cast<uint8_t>(
                        std::min<size_t>(current_values.size(), 255)));
                    current_values.push_back(clamp_value(static_cast<float>(value->valuedouble), 0.0f, max_current));
                }
            }
        }
        
        // Add the schedule point
        if (!pwm_values.empty() && !current_values.empty()) {
            if (time_type == DynamicTimeType::FIXED) {
                set_schedule_point(time_minutes, pwm_values, current_values);
            } else {
                add_dynamic_schedule_point(time_type, static_cast<int16_t>(offset_minutes), pwm_values, current_values);
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
        
        // Parse phase scaling fields
        cJSON* phase_scaling_pwm_item = cJSON_GetObjectItem(moon_obj, "phase_scaling_pwm");
        if (cJSON_IsBool(phase_scaling_pwm_item)) {
            moon_config.phase_scaling_pwm = cJSON_IsTrue(phase_scaling_pwm_item);
        }
        
        cJSON* phase_scaling_current_item = cJSON_GetObjectItem(moon_obj, "phase_scaling_current");
        if (cJSON_IsBool(phase_scaling_current_item)) {
            moon_config.phase_scaling_current = cJSON_IsTrue(phase_scaling_current_item);
        }
        
        // Parse min_current_threshold
        cJSON* min_current_item = cJSON_GetObjectItem(moon_obj, "min_current_threshold");
        if (cJSON_IsNumber(min_current_item)) {
            moon_config.min_current_threshold = clamp_value(
                static_cast<float>(min_current_item->valuedouble), 0.0f, MAX_CHANNEL_CURRENT);
        }
        
        // Parse base_intensity array, clamped to 0-100%
        cJSON* intensity_array = cJSON_GetObjectItem(moon_obj, "base_intensity");
        if (cJSON_IsArray(intensity_array)) {
            moon_config.base_intensity.clear();
            cJSON* intensity_item = NULL;
            cJSON_ArrayForEach(intensity_item, intensity_array) {
                if (cJSON_IsNumber(intensity_item)) {
                    moon_config.base_intensity.push_back(
                        clamp_value(static_cast<float>(intensity_item->valuedouble), 0.0f, 100.0f));
                }
            }
        }
        
        // Parse base_current array, clamped to each channel's max current
        cJSON* current_array = cJSON_GetObjectItem(moon_obj, "base_current");
        if (cJSON_IsArray(current_array)) {
            moon_config.base_current.clear();
            cJSON* current_item = NULL;
            cJSON_ArrayForEach(current_item, current_array) {
                if (cJSON_IsNumber(current_item)) {
                    float max_current = get_channel_max_current(static_cast<uint8_t>(
                        std::min<size_t>(moon_config.base_current.size(), 255)));
                    moon_config.base_current.push_back(
                        clamp_value(static_cast<float>(current_item->valuedouble), 0.0f, max_current));
                }
            }
        }
        
        // Apply moon simulation configuration
        set_moon_simulation(moon_config);
    }

    if (schedule_points_.empty()) {
        return fail_with(error, "schedule_points missing or invalid");
    }
    return true;
}

void LEDScheduler::write_uint16(std::vector<uint8_t>& data, size_t& pos, uint16_t value) const {
    data.resize(pos + 2);
    data[pos++] = value & 0xFF;
    data[pos++] = (value >> 8) & 0xFF;
}

void LEDScheduler::write_float(std::vector<uint8_t>& data, size_t& pos, float value) const {
    data.resize(pos + 4);
    uint32_t bits;
    memcpy(&bits, &value, sizeof(float));
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
    uint32_t bits = (data[pos] & 0xFF) | 
                    ((data[pos + 1] & 0xFF) << 8) | 
                    ((data[pos + 2] & 0xFF) << 16) | 
                    ((data[pos + 3] & 0xFF) << 24);
    pos += 4;
    float value;
    memcpy(&value, &bits, sizeof(float));
    return value;
}

// Channel configuration methods
void LEDScheduler::set_channel_config(uint8_t channel, const ChannelConfig& config) {
    if (channel < num_channels_) {
        channel_configs_[channel] = config;
        channel_configs_[channel].max_current = clamp_value(config.max_current, 0.0f, MAX_CHANNEL_CURRENT);
    }
}

bool LEDScheduler::is_curve_channel(uint8_t channel) const {
    return channel < channel_configs_.size() && channel_configs_[channel].dim_mode == ledbrick::DimMode::CURVE;
}

std::vector<ledbrick::LedGroup> LEDScheduler::channel_leds(uint8_t channel) const {
    if (channel >= channel_configs_.size()) {
        return {};
    }
    if (!channel_configs_[channel].leds.empty()) {
        return channel_configs_[channel].leds;
    }
    return ledbrick::default_channel_leds(channel, num_channels_);
}

ledbrick::ChannelDimmer LEDScheduler::channel_dimmer(uint8_t channel, float reference_temp_c) const {
    if (channel >= channel_configs_.size()) {
        return ledbrick::ChannelDimmer();
    }
    const ChannelConfig& config = channel_configs_[channel];
    return ledbrick::ChannelDimmer(channel_leds(channel), custom_led_models_, config.dim_priority,
                                   config.floor_current, reference_temp_c);
}

const ledbrick::LedModel* LEDScheduler::find_led_model(const std::string& id) const {
    return ledbrick::find_led_model(id, custom_led_models_);
}

bool LEDScheduler::valid_led_groups(const std::vector<ledbrick::LedGroup>& leds, std::string* error,
                                    bool allow_unknown) const {
    if (leds.size() > 8) {
        return fail_with(error, "at most 8 LED models per channel");
    }
    for (const auto& group : leds) {
        if (group.model.empty() || group.model.size() > ledbrick::MAX_LED_MODEL_ID) {
            return fail_with(error, "LED model ids are 1-" + std::to_string(ledbrick::MAX_LED_MODEL_ID) +
                                        " characters");
        }
        if (!allow_unknown && find_led_model(group.model) == nullptr) {
            return fail_with(error, "unknown LED model " + group.model);
        }
        if (group.count < 1 || group.count > 100) {
            return fail_with(error, "LED count must be 1-100");
        }
    }
    return true;
}

bool LEDScheduler::channel_has_models(uint8_t channel, std::string* error) const {
    const std::string where = "channel " + std::to_string(channel + 1) + ": ";
    std::string led_error;
    if (!valid_led_groups(channel_configs_[channel].leds, &led_error)) {
        return fail_with(error, where + led_error);
    }
    if (is_curve_channel(channel) && !channel_dimmer(channel).valid()) {
        return fail_with(error, where + "curve mode needs LEDs with curves");
    }
    return true;
}

bool LEDScheduler::channels_have_models(std::string* error) const {
    for (uint8_t channel = 0; channel < channel_configs_.size(); channel++) {
        if (!channel_has_models(channel, error)) {
            return false;
        }
    }
    return true;
}

bool LEDScheduler::set_custom_led_models(const std::vector<ledbrick::LedModel>& models, std::string* error) {
    if (!valid_custom_models(models, error)) {
        return false;
    }
    // Refuse to break a channel. One already naming a missing model (its model was lost)
    // does not block the change; posting that model back is how it recovers.
    std::vector<bool> had_models(channel_configs_.size());
    for (uint8_t channel = 0; channel < channel_configs_.size(); channel++) {
        had_models[channel] = channel_has_models(channel, nullptr);
    }
    std::vector<ledbrick::LedModel> previous = std::move(custom_led_models_);
    custom_led_models_ = models;
    for (uint8_t channel = 0; channel < channel_configs_.size(); channel++) {
        if (had_models[channel] && !channel_has_models(channel, error)) {
            custom_led_models_ = std::move(previous);
            return false;
        }
    }
    return true;
}

std::string LEDScheduler::export_led_models_json(bool custom_only) const {
    if (custom_only) {
        return custom_models_document(custom_led_models_);
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* array = root ? cJSON_AddArrayToObject(root, "models") : nullptr;
    // Custom models first, then the built-ins they do not replace
    for (int pass = 0; array && pass < 2; pass++) {
        const bool custom = pass == 0;
        for (const auto& model : custom ? custom_led_models_ : ledbrick::builtin_led_models()) {
            if (!custom && find_led_model(model.id) != &model) {
                continue;
            }
            cJSON* item = led_model_json(model);
            if (item) {
                cJSON_AddBoolToObject(item, "custom", custom);
                // The floor current never goes below this
                cJSON_AddNumberToObject(item, "characterized_from", json_number(model.output_vs_current.front().x));
                cJSON_AddItemToArray(array, item);
            }
        }
    }
    return print_and_delete(root);
}

bool LEDScheduler::import_led_models_json(const std::string& json_str, std::string* error, size_t max_saved_size) {
    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        return fail_with(error, "invalid JSON");
    }
    std::vector<ledbrick::LedModel> models;
    bool ok = parse_led_models_json(cJSON_GetObjectItemCaseSensitive(root, "led_models"), models, error);
    cJSON_Delete(root);
    if (!ok) {
        return false;
    }
    if (max_saved_size > 0) {
        size_t size = custom_models_document(models).size();
        if (size > max_saved_size) {
            return fail_with(error, "LED models take " + std::to_string(size) + " bytes to save; the limit is " +
                                        std::to_string(max_saved_size));
        }
    }
    return set_custom_led_models(models, error);
}

bool LEDScheduler::set_channel_dimming(uint8_t channel, ledbrick::DimMode mode, ledbrick::DimPriority priority,
                                       float floor_current, const std::vector<ledbrick::LedGroup>& leds,
                                       std::string* error) {
    auto fail = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (channel >= num_channels_) {
        return fail("invalid channel");
    }
    if (!(floor_current >= MIN_FLOOR_CURRENT && floor_current <= MAX_CHANNEL_CURRENT)) {
        return fail("floor_current must be 0.05-2 A");
    }
    std::string led_error;
    if (!valid_led_groups(leds, &led_error)) {
        return fail(led_error);
    }

    // Back to manual, the level becomes a PWM at the channel's maximum current, the way
    // manual schedules are usually written, rather than the current-first split
    const ledbrick::ChannelDimmer old_dimmer(channel_leds(channel), custom_led_models_,
                                             ledbrick::DimPriority::PWM_FIRST, channel_configs_[channel].floor_current);
    const std::vector<ledbrick::LedGroup> new_leds =
        leds.empty() ? ledbrick::default_channel_leds(channel, num_channels_) : leds;
    const ledbrick::ChannelDimmer new_dimmer(new_leds, custom_led_models_, priority, floor_current);
    if (mode == ledbrick::DimMode::CURVE && !new_dimmer.valid()) {
        return fail("curve mode needs the channel's LEDs");
    }

    // Convert the channel's values so the schedule gives the same light in the new mode
    ChannelConfig& config = channel_configs_[channel];
    if (config.dim_mode != mode) {
        ledbrick::DriveLimits limits;
        limits.max_current_a = config.max_current;
        auto convert = [&](float& pwm, float& current) {
            if (mode == ledbrick::DimMode::CURVE) {
                pwm = new_dimmer.level_for_drive(pwm / 100.0f, current, limits) * 100.0f;
                current = 0.0f;
            } else if (old_dimmer.valid()) {
                ledbrick::Drive drive = old_dimmer.drive_for_level(pwm / 100.0f, limits, 25.0f);
                pwm = drive.pwm * 100.0f;
                current = drive.current_a;
            } else {
                current = config.max_current;  // no curve to go by: keep the level as the PWM
            }
        };
        for (auto& point : schedule_points_) {
            if (channel < point.pwm_values.size() && channel < point.current_values.size()) {
                convert(point.pwm_values[channel], point.current_values[channel]);
            }
        }
        if (channel < moon_simulation_.base_intensity.size()) {
            float current = channel < moon_simulation_.base_current.size() ? moon_simulation_.base_current[channel] : 0.0f;
            convert(moon_simulation_.base_intensity[channel], current);
            if (channel < moon_simulation_.base_current.size()) {
                moon_simulation_.base_current[channel] = current;
            }
        }
    }

    config.dim_mode = mode;
    config.dim_priority = priority;
    config.floor_current = floor_current;
    config.leds = leds;
    return true;
}

void LEDScheduler::add_channel_config_json(cJSON* channels_array, uint8_t channel, bool full) const {
    const ChannelConfig& config = channel_configs_[channel];
    cJSON* channel_obj = cJSON_CreateObject();
    if (!channel_obj) {
        return;
    }
    cJSON_AddStringToObject(channel_obj, "rgb_hex", config.rgb_hex.c_str());
    cJSON_AddNumberToObject(channel_obj, "max_current", json_number(config.max_current));
    cJSON_AddStringToObject(channel_obj, "name", config.name.c_str());

    // The full export always has the dimming settings, with the LEDs in use. The saved
    // copy only has settings that differ from the defaults, to fit the flash slot.
    bool customized = config.dim_mode != ledbrick::DimMode::MANUAL ||
                      config.dim_priority != ledbrick::DimPriority::CURRENT_FIRST ||
                      std::fabs(config.floor_current - DEFAULT_FLOOR_CURRENT) > 1e-4f || !config.leds.empty();
    if (full || customized) {
        cJSON* dimming = cJSON_CreateObject();
        if (dimming) {
            cJSON_AddStringToObject(dimming, "mode", dim_mode_name(config.dim_mode));
            cJSON_AddStringToObject(dimming, "priority", dim_priority_name(config.dim_priority));
            cJSON_AddNumberToObject(dimming, "floor_current", json_number(config.floor_current));
            if (full || !config.leds.empty()) {
                cJSON* leds = cJSON_CreateArray();
                if (leds) {
                    for (const auto& group : full ? channel_leds(channel) : config.leds) {
                        cJSON* item = cJSON_CreateObject();
                        if (item) {
                            cJSON_AddStringToObject(item, "model", group.model.c_str());
                            cJSON_AddNumberToObject(item, "count", group.count);
                            cJSON_AddItemToArray(leds, item);
                        }
                    }
                    cJSON_AddItemToObject(dimming, "leds", leds);
                }
                if (full) {
                    cJSON_AddBoolToObject(dimming, "leds_default", config.leds.empty());
                }
            }
            cJSON_AddItemToObject(channel_obj, "dimming", dimming);
        }
    }
    cJSON_AddItemToArray(channels_array, channel_obj);
}

bool LEDScheduler::parse_channel_dimming_json(const cJSON* item, uint8_t channel, ChannelConfig& config,
                                              bool allow_unknown_models) const {
    if (item == nullptr) {
        // Clients that predate dimming leave it out: keep the channel's settings
        if (channel < channel_configs_.size()) {
            const ChannelConfig& existing = channel_configs_[channel];
            config.dim_mode = existing.dim_mode;
            config.dim_priority = existing.dim_priority;
            config.floor_current = existing.floor_current;
            config.leds = existing.leds;
        }
        return true;
    }
    if (!cJSON_IsObject(item)) {
        return false;
    }

    config.dim_mode = ledbrick::DimMode::MANUAL;
    const cJSON* mode = cJSON_GetObjectItemCaseSensitive(item, "mode");
    if (mode != nullptr) {
        if (!cJSON_IsString(mode)) return false;
        if (strcmp(mode->valuestring, "curve") == 0) {
            config.dim_mode = ledbrick::DimMode::CURVE;
        } else if (strcmp(mode->valuestring, "manual") != 0) {
            return false;
        }
    }

    config.dim_priority = ledbrick::DimPriority::CURRENT_FIRST;
    const cJSON* priority = cJSON_GetObjectItemCaseSensitive(item, "priority");
    if (priority != nullptr) {
        if (!cJSON_IsString(priority)) return false;
        if (strcmp(priority->valuestring, "pwm") == 0) {
            config.dim_priority = ledbrick::DimPriority::PWM_FIRST;
        } else if (strcmp(priority->valuestring, "current") != 0) {
            return false;
        }
    }

    config.floor_current = DEFAULT_FLOOR_CURRENT;
    const cJSON* floor_current = cJSON_GetObjectItemCaseSensitive(item, "floor_current");
    if (floor_current != nullptr) {
        if (!cJSON_IsNumber(floor_current) || !(floor_current->valuedouble >= MIN_FLOOR_CURRENT) ||
            floor_current->valuedouble > MAX_CHANNEL_CURRENT) {
            return false;
        }
        config.floor_current = static_cast<float>(floor_current->valuedouble);
    }

    // The full export marks the emitter's LEDs as defaults, so they stay defaults
    config.leds.clear();
    const cJSON* leds_default = cJSON_GetObjectItemCaseSensitive(item, "leds_default");
    const cJSON* leds = cJSON_GetObjectItemCaseSensitive(item, "leds");
    if (!cJSON_IsTrue(leds_default) && leds != nullptr) {
        if (!cJSON_IsArray(leds)) return false;
        const cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, leds) {
            const cJSON* model = cJSON_GetObjectItemCaseSensitive(entry, "model");
            const cJSON* count = cJSON_GetObjectItemCaseSensitive(entry, "count");
            if (!cJSON_IsString(model) || !cJSON_IsNumber(count) ||
                count->valuedouble != std::floor(count->valuedouble) || count->valuedouble < 1 ||
                count->valuedouble > 100) {
                return false;
            }
            config.leds.push_back({model->valuestring, static_cast<uint16_t>(count->valuedouble)});
        }
        if (!valid_led_groups(config.leds, nullptr, allow_unknown_models)) return false;
    }

    // Curve mode needs LEDs to go by. In the saved copy, a model that was lost uses the
    // standard LED until it is back, rather than fail the whole schedule.
    if (config.dim_mode == ledbrick::DimMode::CURVE) {
        std::vector<ledbrick::LedGroup> in_use =
            config.leds.empty() ? ledbrick::default_channel_leds(channel, num_channels_) : config.leds;
        if (!ledbrick::ChannelDimmer(in_use, custom_led_models_, config.dim_priority, config.floor_current).valid()) {
            return false;
        }
    }
    return true;
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
        channel_configs_[channel].max_current = clamp_value(max_current, 0.0f, MAX_CHANNEL_CURRENT);
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
