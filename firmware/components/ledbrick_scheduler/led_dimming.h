#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ledbrick {

// (x, y) points with x increasing. Linear between points, flat past the ends.
struct CurvePoint {
    float x;
    float y;
};
float curve_lookup(const std::vector<CurvePoint>& curve, float x);

// One LED part's published behaviour, from its datasheet
struct LedModel {
    std::string id;                             // e.g. "luxeon_c_royal_blue"
    std::string name;
    float test_current_a = 0.0f;                // datasheet test current
    float max_current_a = 0.0f;                 // datasheet maximum DC current
    std::vector<CurvePoint> output_vs_current;  // x = A, y = output relative to the test current,
                                                // at junction temperature curve_temp_c
    float curve_temp_c = 85.0f;                 // junction temperature of output_vs_current
    std::vector<CurvePoint> output_vs_temp;     // x = junction deg C, y = relative output (any scale)
    float rth_c_per_w = 0.0f;                   // junction to solder pad thermal resistance
    std::vector<CurvePoint> vf_vs_current;      // x = A, y = forward voltage, for self-heating
};

// Models for the LEDs on LEDBrick emitters (led_models.cpp)
const std::vector<LedModel>& builtin_led_models();
const LedModel* find_led_model(const std::string& id);

struct LedGroup {
    std::string model;  // LedModel id
    uint16_t count;     // no default, so {"id", n} works as a C++11 aggregate
};

// The LEDs in each channel's string on the LEDBrick Plus emitter (8 channels), from its
// schematic. Empty for other channel counts. The two VIOSYS UV LEDs in channel 6's violet
// string have no Lumileds curve and are left out; they carry the same current.
std::vector<LedGroup> default_channel_leds(uint8_t channel, uint8_t num_channels);

// How a channel is driven. MANUAL: the schedule sets PWM and current directly.
// CURVE: the schedule sets one level (share of the channel's light output) and
// ChannelDimmer picks the current and PWM from the LEDs' curves.
enum class DimMode : uint8_t { MANUAL = 0, CURVE };

// In CURVE mode: lower the current first and use PWM below a floor current,
// or hold the current at the channel maximum and dim with PWM only
enum class DimPriority : uint8_t { CURRENT_FIRST = 0, PWM_FIRST };

// What the hardware can do
struct DriveLimits {
    float max_current_a = 1.0f;            // channel limit; also capped by the LEDs' maximum
    float current_step_a = 2.0f / 256.0f;  // TPS922053 resolves ADIM to 8 bits of 2 A
    float min_current_a = 0.05f;           // channel.yaml holds EN/PWM off below this
    float pwm_step = 1.0f / 16384.0f;      // one LEDC step at 14 bits
};

struct Drive {
    float current_a = 0.0f;  // current to command
    float pwm = 0.0f;        // EN/PWM duty, 0-1
};

class ChannelDimmer {
public:
    ChannelDimmer() = default;
    // Unknown model ids are skipped; valid() is false when none are known
    ChannelDimmer(const std::vector<LedGroup>& leds, DimPriority priority, float floor_current_a,
                  float reference_temp_c = 25.0f);
    // For tests and custom parts: models given directly
    ChannelDimmer(const std::vector<std::pair<const LedModel*, uint16_t>>& leds, DimPriority priority,
                  float floor_current_a, float reference_temp_c = 25.0f);

    bool valid() const { return !leds_.empty(); }
    DimPriority priority() const { return priority_; }

    // Count-weighted output of the channel's LEDs, relative to their test currents, at a
    // driver current and solder-pad (board) temperature. Each LED's junction runs hotter
    // than its pad by its thermal resistance times its electrical power.
    float output(float current_a, float temp_c) const;

    // Lowest current every LED in the channel is characterized at (the start of its curve)
    float characterized_current() const;

    // Highest current the channel may use: its limit, the LEDs' datasheet maximum and 2 A
    float max_current(const DriveLimits& limits) const;

    // Current and PWM for a level (0-1). Level 1 is the output at the channel's
    // maximum current at the reference temperature, so a level holds its output as the
    // LEDs heat, until the current reaches the maximum. The current is commanded a
    // quarter step above the driver step it should land on, so the driver gives that
    // step whether it truncates or rounds; the PWM then trims the output to the level.
    Drive drive_for_level(float level, const DriveLimits& limits, float temp_c) const;

    // Level that a manual PWM (0-1) and current give at the reference temperature
    float level_for_drive(float pwm, float current_a, const DriveLimits& limits) const;

private:
    struct Entry {
        const LedModel* model;
        float weight;
    };
    std::vector<Entry> leds_;
    DimPriority priority_ = DimPriority::CURRENT_FIRST;
    float floor_current_a_ = 0.1f;
    float reference_temp_c_ = 25.0f;

    // Driver steps: highest usable, lowest allowed when lit
    int max_step(const DriveLimits& limits) const;
    int min_step(const DriveLimits& limits) const;
};

}  // namespace ledbrick
