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

// The standard LED: the median of the LUXEON C curves. It stands in for any LED without a
// model, so a channel always has a curve: LEDs with no published curve, boards without an
// LED map, and a model that is missing (lost from flash, say). A custom model with this id
// replaces it.
constexpr const char* STANDARD_LED_MODEL = "standard_led";
const LedModel* find_led_model(const std::string& id);
// Looks in custom first, so a custom model with a built-in's id replaces it
const LedModel* find_led_model(const std::string& id, const std::vector<LedModel>& custom);

// Limits for user-supplied models
constexpr size_t MAX_CUSTOM_LED_MODELS = 8;
constexpr size_t MAX_CURVE_POINTS = 32;
constexpr size_t MAX_LED_MODEL_ID = 32;
constexpr size_t MAX_LED_MODEL_NAME = 48;

// Checks a user-supplied model. The id is 1-32 of a-z, 0-9 and _. Currents are 0-3 A.
// output_vs_current is required: 2-32 points, currents increasing, output never falling,
// reaching max_current. output_vs_temp (with curve_temp) and vf_vs_current are optional.
// On failure, error says why.
bool validate_led_model(const LedModel& model, std::string* error);

struct LedGroup {
    std::string model;  // LedModel id
    uint16_t count;     // no default, so {"id", n} works as a C++11 aggregate
};

// The LEDs in each channel's string on the LEDBrick Plus emitter (8 channels), as built:
// boards/ledbrick-plus/channels.json, generated into led_models.cpp. Other channel counts get
// one standard LED.
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
    float current_step_a = 2.0f / 256.0f;  // TPS922053 resolves ADIM to 8 bits of 2 A, rounding down
    float min_current_a = 0.05f;           // channel.yaml holds EN/PWM off below this
    float pwm_step = 1.0f / 16384.0f;      // one LEDC step at 14 bits
    // Light lost per EN/PWM pulse, as a share of the PWM period. Each pulse of 8% or more
    // loses about 19 us of light: 1.9% of the 1 kHz period. Shorter pulses lose less; the
    // loss sets in over the tail width, which is narrower at higher currents (1.9% at
    // 300 mA and above, 4% at 170 mA and below). Measured on a LEDBrick Plus from supply
    // current against duty (boards/ledbrick-plus/FINDINGS.md).
    float pwm_pulse_loss = 0.019f;
    float pwm_tail_low_current = 0.04f;   // tail width at and below 170 mA
    float pwm_tail_high_current = 0.019f;  // tail width at and above 300 mA
    // Highest duty used below 100%. Shorter off-times than 2.5% of the period (25 us) were
    // not measured, and the driver may not see a very short low pulse at all.
    float max_pulsed_pwm = 0.975f;

    // Share of full light a duty gives at a current, and the duty for a share
    float effective_pwm(float pwm, float current_a) const;
    float pwm_for_effective(float effective, float current_a) const;
    float pulse_tail(float current_a) const;
};

// Share of full light a PWM duty gives. 100% has no edges, so no loss. Below that each pulse
// loses pulse_loss of the period, setting in over the tail width:
// d - L (1 - e^(-d/tail)). Rises with d, so pwm_for_effective inverts it.
float effective_pwm(float pwm, float pulse_loss, float tail);
// The duty below 100% that gives an effective share; 1 for shares no pulsed duty reaches
float pwm_for_effective(float effective, float pulse_loss, float tail);

struct Drive {
    float current_a = 0.0f;  // current to command
    float pwm = 0.0f;        // EN/PWM duty, 0-1
};

class ChannelDimmer {
public:
    ChannelDimmer() = default;
    // An unknown model id uses the standard LED. valid() is false only without LEDs.
    ChannelDimmer(const std::vector<LedGroup>& leds, DimPriority priority, float floor_current_a,
                  float reference_temp_c = 25.0f);
    // Looks the models up in custom first, then the built-in table, then uses the standard
    // LED. The dimmer points into custom, so it must not outlive a change to it.
    ChannelDimmer(const std::vector<LedGroup>& leds, const std::vector<LedModel>& custom, DimPriority priority,
                  float floor_current_a, float reference_temp_c = 25.0f);
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
    // LEDs heat, until the current reaches the maximum. The current is commanded half a
    // step above the driver step it should land on, since the driver rounds down; the PWM
    // then trims the output to the level, allowing for the light each pulse loses.
    Drive drive_for_level(float level, const DriveLimits& limits, float temp_c) const;

    // Level that a manual PWM (0-1) and current give at the reference temperature,
    // allowing for the light each pulse loses
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
