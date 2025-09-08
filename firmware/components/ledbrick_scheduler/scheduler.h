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
    struct SchedulePoint {
        uint16_t time_minutes;  // 0-1439 (minutes from midnight)
        std::vector<float> pwm_values;     // PWM percentages (0-100) per channel
        std::vector<float> current_values; // Current values (0-5A) per channel
        
        SchedulePoint() : time_minutes(0) {}
        SchedulePoint(uint16_t time, std::vector<float> pwm, std::vector<float> current) 
            : time_minutes(time), pwm_values(std::move(pwm)), current_values(std::move(current)) {}
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

    // Constructor
    LEDScheduler(uint8_t num_channels = 8);
    
    // Configuration
    void set_num_channels(uint8_t channels);
    uint8_t get_num_channels() const { return num_channels_; }
    
    // Schedule management
    void add_schedule_point(const SchedulePoint& point);
    void set_schedule_point(uint16_t time_minutes, const std::vector<float>& pwm_values, 
                           const std::vector<float>& current_values);
    void remove_schedule_point(uint16_t time_minutes);
    void clear_schedule();
    
    // Interpolation and current state
    InterpolationResult get_values_at_time(uint16_t current_time_minutes) const;
    
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

private:
    uint8_t num_channels_;
    std::vector<SchedulePoint> schedule_points_;
    std::map<std::string, std::vector<SchedulePoint>> presets_;
    
    // Internal methods
    InterpolationResult interpolate_values(uint16_t current_time) const;
    void sort_schedule_points();
    bool validate_point(const SchedulePoint& point) const;
    
    // Serialization helpers
    void write_uint16(std::vector<uint8_t>& data, size_t& pos, uint16_t value) const;
    void write_float(std::vector<uint8_t>& data, size_t& pos, float value) const;
    uint16_t read_uint16(const std::vector<uint8_t>& data, size_t& pos) const;
    float read_float(const std::vector<uint8_t>& data, size_t& pos) const;
};