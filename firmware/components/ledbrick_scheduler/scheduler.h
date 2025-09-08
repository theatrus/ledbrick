#pragma once

#include <vector>
#include <string>
#include <map>
#include <cstdint>

/**
 * Standalone LED Scheduler
 * Pure C++ implementation with no external dependencies
 * Handles schedule points, interpolation, presets, and serialization
 */
class LEDScheduler {
public:
    // Dynamic time reference types
    enum class DynamicTimeType {
        FIXED = 0,           // Fixed time (default, backward compatible)
        SUNRISE_RELATIVE,    // Relative to sunrise (+/- offset)
        SUNSET_RELATIVE,     // Relative to sunset (+/- offset)
        SOLAR_NOON,          // Solar noon (sun at highest point)
        CIVIL_DAWN,          // Sun 6° below horizon (morning)
        CIVIL_DUSK,          // Sun 6° below horizon (evening)
        NAUTICAL_DAWN,       // Sun 12° below horizon (morning)
        NAUTICAL_DUSK,       // Sun 12° below horizon (evening)
        ASTRONOMICAL_DAWN,   // Sun 18° below horizon (morning)
        ASTRONOMICAL_DUSK    // Sun 18° below horizon (evening)
    };

    struct SchedulePoint {
        uint16_t time_minutes;  // 0-1439 (minutes from midnight) - for FIXED type or calculated value
        std::vector<float> pwm_values;     // PWM percentages (0-100) per channel
        std::vector<float> current_values; // Current values (0-5A) per channel
        
        // Dynamic time fields
        DynamicTimeType time_type = DynamicTimeType::FIXED;
        int16_t offset_minutes = 0;  // Offset from dynamic reference (-1439 to +1439)
        
        SchedulePoint() : time_minutes(0) {}
        SchedulePoint(uint16_t time, std::vector<float> pwm, std::vector<float> current) 
            : time_minutes(time), pwm_values(std::move(pwm)), current_values(std::move(current)) {}
        SchedulePoint(DynamicTimeType type, int16_t offset, std::vector<float> pwm, std::vector<float> current) 
            : time_minutes(0), pwm_values(std::move(pwm)), current_values(std::move(current)), 
              time_type(type), offset_minutes(offset) {}
    };

    struct InterpolationResult {
        std::vector<float> pwm_values;
        std::vector<float> current_values;
        bool valid = false;
        
        InterpolationResult() = default;
        InterpolationResult(std::vector<float> pwm, std::vector<float> current) 
            : pwm_values(std::move(pwm)), current_values(std::move(current)), valid(true) {}
    };

    struct SerializedData {
        uint16_t num_points;
        uint8_t num_channels;
        std::vector<uint8_t> data;
        
        SerializedData() : num_points(0), num_channels(0) {}
    };

    // Structure to hold astronomical times for dynamic calculations
    struct AstronomicalTimes {
        uint16_t sunrise_minutes = 420;     // Default 7:00 AM
        uint16_t sunset_minutes = 1080;     // Default 6:00 PM
        uint16_t solar_noon_minutes = 750;  // Default 12:30 PM
        uint16_t civil_dawn_minutes = 390;  // Default 6:30 AM
        uint16_t civil_dusk_minutes = 1110; // Default 6:30 PM
        uint16_t nautical_dawn_minutes = 360;  // Default 6:00 AM
        uint16_t nautical_dusk_minutes = 1140; // Default 7:00 PM
        uint16_t astronomical_dawn_minutes = 330;  // Default 5:30 AM
        uint16_t astronomical_dusk_minutes = 1170; // Default 7:30 PM
        uint16_t moonrise_minutes = 0;      // Moon rise time (0-1439)
        uint16_t moonset_minutes = 0;       // Moon set time (0-1439)
        float moon_phase = 0.0f;            // Moon phase (0=new, 0.5=full, 1=new)
        bool valid = false;  // Whether times have been calculated
    };

    // Moon simulation configuration
    struct MoonSimulation {
        bool enabled = false;
        std::vector<float> base_intensity;  // Base moonlight intensity per channel (0-100%)
        bool phase_scaling = true;         // Scale intensity by moon phase
        
        MoonSimulation() = default;
        MoonSimulation(bool enable, std::vector<float> intensity, bool scale = true)
            : enabled(enable), base_intensity(std::move(intensity)), phase_scaling(scale) {}
    };

    // Constructor
    LEDScheduler(uint8_t num_channels = 8);
    
    // Configuration
    void set_num_channels(uint8_t channels);
    uint8_t get_num_channels() const { return num_channels_; }
    
    // Schedule management
    void add_schedule_point(const SchedulePoint& point);
    void set_schedule_point(uint16_t time_minutes, const std::vector<float>& pwm_values, 
                           const std::vector<float>& current_values);
    void add_dynamic_schedule_point(DynamicTimeType type, int16_t offset_minutes,
                                   const std::vector<float>& pwm_values,
                                   const std::vector<float>& current_values);
    void remove_schedule_point(uint16_t time_minutes);
    void remove_dynamic_schedule_point(DynamicTimeType type, int16_t offset_minutes);
    void clear_schedule();
    
    // Interpolation and current state
    InterpolationResult get_values_at_time(uint16_t current_time_minutes) const;
    InterpolationResult get_values_at_time_with_astro(uint16_t current_time_minutes, 
                                                      const AstronomicalTimes& astro_times) const;
    
    // Moon simulation
    void set_moon_simulation(const MoonSimulation& config);
    MoonSimulation get_moon_simulation() const { return moon_simulation_; }
    void enable_moon_simulation(bool enabled);
    void set_moon_base_intensity(const std::vector<float>& intensity);
    
    // Astronomical time management
    void set_astronomical_times(const AstronomicalTimes& times);
    AstronomicalTimes get_astronomical_times() const { return astronomical_times_; }
    
    // Calculate actual time for dynamic points
    uint16_t calculate_dynamic_time(const SchedulePoint& point, const AstronomicalTimes& astro_times) const;
    
    // Helper to convert string to DynamicTimeType
    static DynamicTimeType string_to_dynamic_time_type(const std::string& type_str);
    static std::string dynamic_time_type_to_string(DynamicTimeType type);
    
    // Schedule inspection
    std::vector<SchedulePoint> get_schedule_points() const { return schedule_points_; }
    size_t get_schedule_size() const { return schedule_points_.size(); }
    bool is_schedule_empty() const { return schedule_points_.empty(); }
    
    // Preset management
    void load_preset(const std::string& preset_name);
    void save_preset(const std::string& preset_name);
    std::vector<std::string> get_preset_names() const;
    void clear_preset(const std::string& preset_name);
    
    // Serialization
    SerializedData serialize() const;
    bool deserialize(const SerializedData& data);
    
    // JSON export/import
    std::string export_json() const;
    bool import_json(const std::string& json_str);
    
    // Built-in presets
    void create_sunrise_sunset_preset(uint16_t sunrise_minutes = 420, uint16_t sunset_minutes = 1020);
    void create_full_spectrum_preset();
    void create_simple_preset();
    void create_dynamic_sunrise_sunset_preset();

private:
    uint8_t num_channels_;
    std::vector<SchedulePoint> schedule_points_;
    std::map<std::string, std::vector<SchedulePoint>> presets_;
    AstronomicalTimes astronomical_times_;
    MoonSimulation moon_simulation_;
    
    // Internal methods
    InterpolationResult interpolate_values(uint16_t current_time) const;
    InterpolationResult interpolate_values_with_astro(uint16_t current_time, const AstronomicalTimes& astro_times) const;
    InterpolationResult apply_moon_simulation(const InterpolationResult& base_result, uint16_t current_time, 
                                             const AstronomicalTimes& astro_times) const;
    bool is_moon_visible(uint16_t current_time, const AstronomicalTimes& astro_times) const;
    void sort_schedule_points();
    void sort_schedule_points_with_astro(const AstronomicalTimes& astro_times);
    bool validate_point(const SchedulePoint& point) const;
    std::vector<SchedulePoint> resolve_dynamic_points(const AstronomicalTimes& astro_times) const;
    
    // Serialization helpers
    void write_uint16(std::vector<uint8_t>& data, size_t& pos, uint16_t value) const;
    void write_float(std::vector<uint8_t>& data, size_t& pos, float value) const;
    uint16_t read_uint16(const std::vector<uint8_t>& data, size_t& pos) const;
    float read_float(const std::vector<uint8_t>& data, size_t& pos) const;
};