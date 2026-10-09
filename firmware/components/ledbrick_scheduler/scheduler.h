#pragma once

#include <vector>
#include <string>
#include <map>
#include <cstdint>

#include "led_dimming.h"

struct cJSON;

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
        float moon_phase = 0.0f;            // Position in the lunar cycle (0=new, 0.5=full, 1=new)
        bool valid = false;  // Whether times have been calculated
    };

    // Channel configuration
    struct ChannelConfig {
        std::string rgb_hex = "#FFFFFF";  // RGB color in hex format
        float max_current = 2.0f;          // Maximum current in amps (0-MAX_CHANNEL_CURRENT)
        std::string name;                  // Optional channel name
        
        // How the channel is dimmed (led_dimming.h). In CURVE mode a schedule point's
        // pwm_values entry is the channel's level (0-100% of its light output at max_current)
        // and its current_values entry is unused (0).
        ledbrick::DimMode dim_mode = ledbrick::DimMode::MANUAL;
        ledbrick::DimPriority dim_priority = ledbrick::DimPriority::CURRENT_FIRST;
        float floor_current = 0.1f;            // Current-first: lowest current before PWM takes over (A)
        std::vector<ledbrick::LedGroup> leds;  // Empty: the emitter's LEDs for this channel
        
        ChannelConfig() = default;
        ChannelConfig(const std::string& color, float current, const std::string& n = "")
            : rgb_hex(color), max_current(current), name(n) {}
    };

    // Moon simulation configuration
    struct MoonSimulation {
        bool enabled = false;
        std::vector<float> base_intensity;  // Base moonlight PWM per channel (0-100%)
        std::vector<float> base_current;    // Base moonlight current per channel (0-max_current)
        bool phase_scaling_pwm = true;      // Scale PWM by the share of the moon that is lit
        bool phase_scaling_current = true;  // Scale current by the share of the moon that is lit
        float min_current_threshold = 0.0f; // Minimum current when phase scaling (0-2A)
        
        MoonSimulation() = default;
        MoonSimulation(bool enable, std::vector<float> intensity, bool scale = true)
            : enabled(enable), base_intensity(std::move(intensity)), 
              phase_scaling_pwm(scale), phase_scaling_current(scale) {}
        MoonSimulation(bool enable, std::vector<float> intensity, std::vector<float> current, bool scale = true)
            : enabled(enable), base_intensity(std::move(intensity)), base_current(std::move(current)), 
              phase_scaling_pwm(scale), phase_scaling_current(scale) {}
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
    // Same, to the second, so ramps move smoothly instead of in one-minute steps
    InterpolationResult get_values_at_seconds_with_astro(uint32_t second_of_day,
                                                         const AstronomicalTimes& astro_times) const;

    // Moon simulation
    void set_moon_simulation(const MoonSimulation& config);
    MoonSimulation get_moon_simulation() const { return moon_simulation_; }
    void enable_moon_simulation(bool enabled);
    void set_moon_base_intensity(const std::vector<float>& intensity);
    void set_moon_base_current(const std::vector<float>& current);
    
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
    // Returns false, leaving the schedule unchanged, for an unknown preset
    bool load_preset(const std::string& preset_name);
    void save_preset(const std::string& preset_name);
    std::vector<std::string> get_preset_names() const;
    void clear_preset(const std::string& preset_name);
    
    // Serialization
    SerializedData serialize() const;
    bool deserialize(const SerializedData& data);
    
    // Highest per-channel current the hardware accepts, in amps
    static constexpr float MAX_CHANNEL_CURRENT = 2.0f;

    // JSON export/import
    std::string export_json() const;
    std::string export_json_minified() const;  // Compact JSON without formatting
    // Replaces the schedule, channel configs and moon settings. On failure nothing changes.
    // PWM and current values are clamped to their valid ranges; bad times or types fail.
    // error, when given, may say why an import failed. Custom LED models are not part of
    // the schedule (see import_led_models_json). A channel naming a model that is not
    // known fails the import, unless allow_unknown_models is set: then the channel keeps the
    // name and uses the standard LED for it until the model is back (for the saved copy, so
    // a lost model cannot lose the whole schedule).
    bool import_json(const std::string& json_str, std::string* error = nullptr, bool allow_unknown_models = false);
    
    // Built-in preset (only one default)
    void create_default_astronomical_preset();
    
    // Channel configuration
    void set_channel_config(uint8_t channel, const ChannelConfig& config);
    ChannelConfig get_channel_config(uint8_t channel) const;
    std::vector<ChannelConfig> get_all_channel_configs() const { return channel_configs_; }
    
    // Dimming
    static constexpr float MIN_FLOOR_CURRENT = 0.05f;  // the board keeps EN/PWM off below 50 mA
    bool is_curve_channel(uint8_t channel) const;
    // The channel's LEDs: as configured, or the emitter's defaults
    std::vector<ledbrick::LedGroup> channel_leds(uint8_t channel) const;
    ledbrick::ChannelDimmer channel_dimmer(uint8_t channel, float reference_temp_c = 25.0f) const;
    // Changes how a channel is dimmed. Switching between manual and curve mode converts the
    // channel's schedule points and moonlight, so the light they give stays the same.
    // Returns false and changes nothing for an unknown LED model, a bad count or floor
    // current, or curve mode without LEDs.
    bool set_channel_dimming(uint8_t channel, ledbrick::DimMode mode, ledbrick::DimPriority priority,
                             float floor_current, const std::vector<ledbrick::LedGroup>& leds,
                             std::string* error = nullptr);

    // LED models the user added, kept apart from the schedule. One with a built-in's id
    // replaces the built-in, for every channel that uses it.
    const std::vector<ledbrick::LedModel>& get_custom_led_models() const { return custom_led_models_; }
    // Replaces the custom models. Returns false and changes nothing when a model is invalid,
    // ids repeat, or a channel uses a model the new set drops.
    bool set_custom_led_models(const std::vector<ledbrick::LedModel>& models, std::string* error = nullptr);
    // A model by id: custom first, then built-in
    const ledbrick::LedModel* find_led_model(const std::string& id) const;
    // {"models":[...]}: every model in effect, with its curves; custom ones have "custom": true.
    // custom_only: {"led_models":[...]} with just the custom models, as import takes them.
    std::string export_led_models_json(bool custom_only = false) const;
    // Replaces the custom models from {"led_models":[...]}; see set_custom_led_models.
    // With max_saved_size, a set whose export_led_models_json(true) is larger is refused.
    bool import_led_models_json(const std::string& json_str, std::string* error = nullptr,
                                size_t max_saved_size = 0);
    // Every channel's LEDs are known, and curve channels have curves; error names the first
    // channel that is not
    bool channels_have_models(std::string* error = nullptr) const;

    void set_channel_color(uint8_t channel, const std::string& rgb_hex);
    void set_channel_max_current(uint8_t channel, float max_current);
    std::string get_channel_color(uint8_t channel) const;
    float get_channel_max_current(uint8_t channel) const;
    

private:
    uint8_t num_channels_;
    std::vector<SchedulePoint> schedule_points_;
    std::map<std::string, std::vector<SchedulePoint>> presets_;
    AstronomicalTimes astronomical_times_;
    MoonSimulation moon_simulation_;
    std::vector<ChannelConfig> channel_configs_;
    std::vector<ledbrick::LedModel> custom_led_models_;

    // Internal methods
    InterpolationResult interpolate_values(uint16_t current_time) const;
    InterpolationResult interpolate_values_with_astro(float current_time, const AstronomicalTimes& astro_times) const;
    InterpolationResult apply_moon_simulation(const InterpolationResult& base_result, uint16_t current_time, 
                                             const AstronomicalTimes& astro_times) const;
    bool is_moon_visible(uint16_t current_time, const AstronomicalTimes& astro_times) const;
    void sort_schedule_points();
    void sort_schedule_points_with_astro(const AstronomicalTimes& astro_times);
    bool validate_point(const SchedulePoint& point) const;
    bool import_json_into_(const std::string& json_str, std::string* error, bool allow_unknown_models);
    // Known models (any name with allow_unknown), 1-100 of each, at most 8 kinds in a string
    bool valid_led_groups(const std::vector<ledbrick::LedGroup>& leds, std::string* error,
                          bool allow_unknown = false) const;
    // The channel's LEDs are known, and it has curves if in curve mode
    bool channel_has_models(uint8_t channel, std::string* error) const;
    // JSON for one channel config; full adds the dimming defaults the saved copy leaves out
    void add_channel_config_json(struct cJSON* channels_array, uint8_t channel, bool full) const;
    // Dimming settings from a channel config's "dimming" object; false when invalid.
    // Without one, the channel keeps its current settings.
    bool parse_channel_dimming_json(const struct cJSON* item, uint8_t channel, ChannelConfig& config,
                                    bool allow_unknown_models) const;
    std::vector<SchedulePoint> resolve_dynamic_points(const AstronomicalTimes& astro_times) const;
    
    // Serialization helpers
    void write_uint16(std::vector<uint8_t>& data, size_t& pos, uint16_t value) const;
    void write_float(std::vector<uint8_t>& data, size_t& pos, float value) const;
    uint16_t read_uint16(const std::vector<uint8_t>& data, size_t& pos) const;
    float read_float(const std::vector<uint8_t>& data, size_t& pos) const;
};