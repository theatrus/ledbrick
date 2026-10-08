#include "led_dimming.h"

#include <algorithm>
#include <cmath>

namespace ledbrick {

namespace {
constexpr float HARDWARE_MAX_CURRENT_A = 2.0f;
// The current is commanded this far into a driver step, so the driver lands on the
// step whether it truncates or rounds the ADIM duty, with room for the PWM's rounding
constexpr float STEP_OFFSET = 0.25f;
}  // namespace

float curve_lookup(const std::vector<CurvePoint>& curve, float x) {
    if (curve.empty()) {
        return 1.0f;
    }
    if (x <= curve.front().x) {
        return curve.front().y;
    }
    if (x >= curve.back().x) {
        return curve.back().y;
    }
    for (size_t i = 1; i < curve.size(); i++) {
        if (x <= curve[i].x) {
            const CurvePoint& a = curve[i - 1];
            const CurvePoint& b = curve[i];
            float span = b.x - a.x;
            return span > 0.0f ? a.y + (b.y - a.y) * (x - a.x) / span : b.y;
        }
    }
    return curve.back().y;
}

std::vector<LedGroup> default_channel_leds(uint8_t channel, uint8_t num_channels) {
    if (num_channels != 8) {
        return {};
    }
    switch (channel) {
        case 0: return {{"luxeon_c_mint", 4}, {"luxeon_c_cyan", 4}};                                      // CyMint
        case 1: return {{"luxeon_c_white_5900k", 8}};                                                     // CW
        case 2: return {{"luxeon_c_white_3900k", 4}, {"luxeon_c_deep_red", 2}, {"luxeon_c_pc_amber", 2}};  // WW
        case 3: return {{"luxeon_c_pc_blue", 8}, {"luxeon_c_blue", 4}};                                   // PC blue
        case 4: return {{"luxeon_rubix_royal_blue", 13}};                                                 // Royal blue, centre
        case 5: return {{"luxeon_c_violet", 6}};                                                          // Violet (+2 UV)
        case 6: return {{"luxeon_c_white_5900k", 6}, {"luxeon_c_royal_blue", 4}};                        // White + blue
        case 7: return {{"luxeon_rubix_royal_blue", 13}};                                                 // Royal blue, outer
        default: return {};
    }
}

const LedModel* find_led_model(const std::string& id) {
    for (const auto& model : builtin_led_models()) {
        if (model.id == id) {
            return &model;
        }
    }
    return nullptr;
}

ChannelDimmer::ChannelDimmer(const std::vector<LedGroup>& leds, DimPriority priority, float floor_current_a,
                             float reference_temp_c)
    : priority_(priority), floor_current_a_(floor_current_a), reference_temp_c_(reference_temp_c) {
    for (const auto& group : leds) {
        const LedModel* model = find_led_model(group.model);
        if (model != nullptr && group.count > 0 && !model->output_vs_current.empty()) {
            leds_.push_back({model, static_cast<float>(group.count)});
        }
    }
}

ChannelDimmer::ChannelDimmer(const std::vector<std::pair<const LedModel*, uint16_t>>& leds, DimPriority priority,
                             float floor_current_a, float reference_temp_c)
    : priority_(priority), floor_current_a_(floor_current_a), reference_temp_c_(reference_temp_c) {
    for (const auto& led : leds) {
        if (led.first != nullptr && led.second > 0 && !led.first->output_vs_current.empty()) {
            leds_.push_back({led.first, static_cast<float>(led.second)});
        }
    }
}

float ChannelDimmer::output(float current_a, float temp_c) const {
    if (!(current_a > 0.0f) || leds_.empty()) {
        return 0.0f;
    }
    float sum = 0.0f;
    float weights = 0.0f;
    for (const auto& led : leds_) {
        const auto& curve = led.model->output_vs_current;
        // Below the first plotted current, run the curve linearly to zero
        float relative = current_a < curve.front().x ? curve.front().y * current_a / curve.front().x
                                                      : curve_lookup(curve, current_a);
        const auto& thermal = led.model->output_vs_temp;
        if (!thermal.empty()) {
            // The current curve is at a fixed junction temperature; scale it to this LED's
            // junction, which runs above the pad by its thermal resistance times its power
            float junction_c = temp_c;
            if (led.model->rth_c_per_w > 0.0f && !led.model->vf_vs_current.empty()) {
                junction_c += led.model->rth_c_per_w * curve_lookup(led.model->vf_vs_current, current_a) * current_a;
            }
            float at_curve = curve_lookup(thermal, led.model->curve_temp_c);
            if (at_curve > 0.0f) {
                relative *= curve_lookup(thermal, junction_c) / at_curve;
            }
        }
        sum += led.weight * relative;
        weights += led.weight;
    }
    return weights > 0.0f ? sum / weights : 0.0f;
}

float ChannelDimmer::characterized_current() const {
    float lowest = 0.0f;
    for (const auto& led : leds_) {
        lowest = std::max(lowest, led.model->output_vs_current.front().x);
    }
    return lowest;
}

float ChannelDimmer::max_current(const DriveLimits& limits) const {
    float limit = std::min(limits.max_current_a, HARDWARE_MAX_CURRENT_A);
    for (const auto& led : leds_) {
        if (led.model->max_current_a > 0.0f) {
            limit = std::min(limit, led.model->max_current_a);
        }
    }
    return std::max(limit, 0.0f);
}

int ChannelDimmer::max_step(const DriveLimits& limits) const {
    // The commanded current, a quarter step above the step, must stay within the limit
    return static_cast<int>(std::floor(max_current(limits) / limits.current_step_a - STEP_OFFSET + 1e-4f));
}

int ChannelDimmer::min_step(const DriveLimits& limits) const {
    // Below the lowest current the datasheets characterize, the output is unknown, so the
    // PWM takes over there however low the floor is set
    float floor_a = limits.min_current_a;
    if (priority_ == DimPriority::CURRENT_FIRST) {
        floor_a = std::max(floor_a, std::max(floor_current_a_, characterized_current()));
    }
    return static_cast<int>(std::ceil(floor_a / limits.current_step_a - 1e-4f));
}

Drive ChannelDimmer::drive_for_level(float level, const DriveLimits& limits, float temp_c) const {
    Drive drive;
    if (!valid() || !(level > 0.0f) || !(limits.current_step_a > 0.0f)) {
        return drive;
    }
    level = std::min(level, 1.0f);
    const float step = limits.current_step_a;
    const int top = max_step(limits);
    const int gate = static_cast<int>(std::ceil(limits.min_current_a / step - 1e-4f));
    if (top < 1 || top < gate) {
        return drive;  // the channel limit is below the lowest current the board will light
    }
    const int bottom = std::min(min_step(limits), top);

    const float target = level * output(top * step, reference_temp_c_);
    int k = top;
    if (priority_ == DimPriority::CURRENT_FIRST) {
        if (output(bottom * step, temp_c) >= target) {
            k = bottom;  // below the floor: hold the floor current and dim with PWM
        } else if (output(top * step, temp_c) >= target) {
            // Smallest step that reaches the target; the PWM trims the rest
            int lo = bottom;
            int hi = top;
            while (hi - lo > 1) {
                int mid = lo + (hi - lo) / 2;
                if (output(mid * step, temp_c) >= target) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            k = hi;
        }
        // Otherwise the LEDs are too hot to reach the level: full current and PWM
    }

    float out = output(k * step, temp_c);
    if (!(out > 0.0f)) {
        return drive;
    }
    float pwm = std::min(target / out, 1.0f);
    if (pwm < limits.pwm_step * 0.5f) {
        return drive;  // dimmer than one PWM step: off
    }
    drive.current_a = (k + STEP_OFFSET) * step;
    drive.pwm = pwm;
    return drive;
}

float ChannelDimmer::level_for_drive(float pwm, float current_a, const DriveLimits& limits) const {
    if (!valid() || !(pwm > 0.0f) || !(current_a > 0.0f)) {
        return 0.0f;
    }
    const int top = max_step(limits);
    float reference = output(top * limits.current_step_a, reference_temp_c_);
    if (!(reference > 0.0f)) {
        return 0.0f;
    }
    current_a = std::min(current_a, max_current(limits));
    float level = std::min(pwm, 1.0f) * output(current_a, reference_temp_c_) / reference;
    return std::max(0.0f, std::min(level, 1.0f));
}

}  // namespace ledbrick
