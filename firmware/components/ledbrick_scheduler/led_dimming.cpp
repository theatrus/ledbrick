#include "led_dimming.h"

#include <algorithm>
#include <cmath>

namespace ledbrick {

namespace {
constexpr float HARDWARE_MAX_CURRENT_A = 2.0f;
// The current is commanded this far into a driver step. The driver rounds the ADIM duty
// down (measured), so mid-step leaves the most room either side for the duty's rounding.
constexpr float STEP_OFFSET = 0.5f;
}  // namespace

float effective_pwm(float pwm, float pulse_loss, float tail) {
    if (!(pwm > 0.0f)) {
        return 0.0f;
    }
    if (pwm >= 1.0f) {
        return 1.0f;
    }
    if (!(pulse_loss > 0.0f) || !(tail > 0.0f)) {
        return pwm;
    }
    // The tail is never narrower than the loss, so the share rises with the duty
    tail = std::max(tail, pulse_loss);
    return pwm - pulse_loss * (1.0f - std::exp(-pwm / tail));
}

float pwm_for_effective(float effective, float pulse_loss, float tail) {
    if (!(effective > 0.0f)) {
        return 0.0f;
    }
    if (!(pulse_loss > 0.0f) || !(tail > 0.0f)) {
        return std::min(effective, 1.0f);
    }
    // The formula's limit just below 100%
    if (effective >= effective_pwm(1.0f - 1e-6f, pulse_loss, tail)) {
        return 1.0f;
    }
    // effective_pwm rises with d, so bisect; 30 halvings resolve far below one LEDC step
    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 30; i++) {
        float mid = 0.5f * (lo + hi);
        if (effective_pwm(mid, pulse_loss, tail) < effective) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return hi;
}

float DriveLimits::pulse_tail(float current_a) const {
    constexpr float LOW_A = 0.17f;
    constexpr float HIGH_A = 0.3f;
    if (current_a <= LOW_A) {
        return pwm_tail_low_current;
    }
    if (current_a >= HIGH_A) {
        return pwm_tail_high_current;
    }
    float t = (current_a - LOW_A) / (HIGH_A - LOW_A);
    return pwm_tail_low_current + t * (pwm_tail_high_current - pwm_tail_low_current);
}

float DriveLimits::effective_pwm(float pwm, float current_a) const {
    return ledbrick::effective_pwm(pwm, pwm_pulse_loss, pulse_tail(current_a));
}

float DriveLimits::pwm_for_effective(float effective, float current_a) const {
    return ledbrick::pwm_for_effective(effective, pwm_pulse_loss, pulse_tail(current_a));
}

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

const LedModel* find_led_model(const std::string& id) {
    for (const auto& model : builtin_led_models()) {
        if (model.id == id) {
            return &model;
        }
    }
    return nullptr;
}

const LedModel* find_led_model(const std::string& id, const std::vector<LedModel>& custom) {
    for (const auto& model : custom) {
        if (model.id == id) {
            return &model;
        }
    }
    return find_led_model(id);
}

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

// 2 to MAX_CURVE_POINTS points, x increasing within [x_low, x_high], y within [y_low, y_high]
bool valid_curve(const std::vector<CurvePoint>& curve, const char* name, float x_low, float x_high, float y_low,
                 float y_high, std::string* error) {
    if (curve.size() < 2 || curve.size() > MAX_CURVE_POINTS) {
        return fail(error, std::string(name) + " needs 2-" + std::to_string(MAX_CURVE_POINTS) + " points");
    }
    for (size_t i = 0; i < curve.size(); i++) {
        const CurvePoint& p = curve[i];
        if (!(p.x >= x_low && p.x <= x_high) || !(p.y >= y_low && p.y <= y_high)) {
            return fail(error, std::string(name) + " has a point out of range");
        }
        if (i > 0 && !(p.x > curve[i - 1].x)) {
            return fail(error, std::string(name) + " must have increasing x");
        }
    }
    return true;
}

}  // namespace

bool validate_led_model(const LedModel& model, std::string* error) {
    constexpr float MAX_CURRENT_A = 3.0f;
    if (model.id.empty() || model.id.size() > MAX_LED_MODEL_ID) {
        return fail(error, "LED model id must be 1-" + std::to_string(MAX_LED_MODEL_ID) + " characters");
    }
    for (char c : model.id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return fail(error, "LED model id may only use a-z, 0-9 and _");
        }
    }
    const std::string where = "LED model " + model.id + ": ";
    if (model.name.size() > MAX_LED_MODEL_NAME) {
        return fail(error, where + "name is over " + std::to_string(MAX_LED_MODEL_NAME) + " characters");
    }
    if (!(model.max_current_a > 0.0f && model.max_current_a <= MAX_CURRENT_A)) {
        return fail(error, where + "max_current must be over 0 and at most 3 A");
    }
    if (!(model.test_current_a >= 0.0f && model.test_current_a <= MAX_CURRENT_A)) {
        return fail(error, where + "test_current must be 0-3 A");
    }

    std::string curve_error;
    const auto& current = model.output_vs_current;
    // The curve runs linearly to zero below its first current, so that must be above zero
    if (!valid_curve(current, "output_vs_current", 1e-3f, MAX_CURRENT_A, 0.0f, 1000.0f, &curve_error)) {
        return fail(error, where + curve_error);
    }
    for (size_t i = 1; i < current.size(); i++) {
        if (current[i].y < current[i - 1].y) {
            return fail(error, where + "output_vs_current must not fall as the current rises");
        }
    }
    if (!(current.back().y > 0.0f)) {
        return fail(error, where + "output_vs_current must end above zero");
    }
    // Past its last point the curve is flat, which would understate the output
    if (current.back().x < model.max_current_a - 1e-4f) {
        return fail(error, where + "output_vs_current must reach max_current");
    }

    if (!model.output_vs_temp.empty()) {
        if (!valid_curve(model.output_vs_temp, "output_vs_temp", -40.0f, 200.0f, 1e-3f, 1000.0f, &curve_error)) {
            return fail(error, where + curve_error);
        }
        if (!(model.curve_temp_c >= -40.0f && model.curve_temp_c <= 200.0f)) {
            return fail(error, where + "curve_temp must be -40 to 200 C");
        }
    }
    if (!(model.rth_c_per_w >= 0.0f && model.rth_c_per_w <= 100.0f)) {
        return fail(error, where + "rth must be 0-100 C/W");
    }
    if (!model.vf_vs_current.empty() &&
        !valid_curve(model.vf_vs_current, "vf_vs_current", 1e-3f, MAX_CURRENT_A, 0.1f, 10.0f, &curve_error)) {
        return fail(error, where + curve_error);
    }
    return true;
}

ChannelDimmer::ChannelDimmer(const std::vector<LedGroup>& leds, DimPriority priority, float floor_current_a,
                             float reference_temp_c)
    : ChannelDimmer(leds, std::vector<LedModel>(), priority, floor_current_a, reference_temp_c) {}

ChannelDimmer::ChannelDimmer(const std::vector<LedGroup>& leds, const std::vector<LedModel>& custom,
                             DimPriority priority, float floor_current_a, float reference_temp_c)
    : priority_(priority), floor_current_a_(floor_current_a), reference_temp_c_(reference_temp_c) {
    for (const auto& group : leds) {
        const LedModel* model = find_led_model(group.model, custom);
        if (model == nullptr) {
            model = find_led_model(STANDARD_LED_MODEL, custom);
        }
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
    // Pulsing loses light, so the highest pulsed duty gives `pulsed` of a step's output;
    // only 100% (no pulses) gives all of it
    // (the tail hardly matters at the highest pulsed duty)
    const float pulsed = limits.effective_pwm(limits.max_pulsed_pwm, top * step);
    int k = top;
    if (priority_ == DimPriority::CURRENT_FIRST) {
        if (output(bottom * step, temp_c) * pulsed >= target) {
            k = bottom;  // below the floor: hold the floor current and dim with PWM
        } else if (output(top * step, temp_c) * pulsed >= target) {
            // Smallest step whose pulsed output reaches the target; the PWM trims the rest
            int lo = bottom;
            int hi = top;
            while (hi - lo > 1) {
                int mid = lo + (hi - lo) / 2;
                if (output(mid * step, temp_c) * pulsed >= target) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            k = hi;
        }
        // Otherwise only the top step at 100% comes near the target (or the LEDs are too
        // hot to reach it): the top step, trimmed below
    }

    float out = output(k * step, temp_c);
    if (!(out > 0.0f)) {
        return drive;
    }
    float share = target / out;
    float pwm;
    if (share <= pulsed) {
        pwm = limits.pwm_for_effective(share, k * step);  // the driver gives step k
    } else if (share >= 1.0f) {
        pwm = 1.0f;  // all the top step gives; hot LEDs may fall short of the level
    } else {
        // No pulsed duty gives this much of the top step's light, and 100% gives more.
        // Choosing between those two jumped 4.6% near the top of every ramp. Steps at 100%
        // are under 1% of the light apart up here, so run the step whose full output is
        // nearest the target. In PWM-first mode the current drops a few percent at most.
        int lo = bottom - 1;  // output below the target, or below the range
        int hi = k;           // output at or above the target
        while (hi - lo > 1) {
            int mid = lo + (hi - lo) / 2;
            if (output(mid * step, temp_c) >= target) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        const float above = output(hi * step, temp_c);
        const float below_full = lo >= bottom ? output(lo * step, temp_c) : 0.0f;
        const float below_pulsed = out * pulsed;
        if (above - target <= target - std::max(below_full, below_pulsed)) {
            k = hi;
            pwm = 1.0f;
        } else if (below_full > below_pulsed) {
            k = lo;
            pwm = 1.0f;
        } else {
            pwm = limits.max_pulsed_pwm;
        }
    }
    if (pwm < limits.pwm_step * 0.5f) {
        return drive;  // dimmer than one PWM step: off
    }
    drive.current_a = (k + STEP_OFFSET) * step;
    drive.pwm = pwm;
    return drive;
}

float ChannelDimmer::level_for_drive(float pwm, float current_a, const DriveLimits& limits) const {
    if (!valid() || !(pwm > 0.0f) || !(current_a > 0.0f) || !(limits.current_step_a > 0.0f)) {
        return 0.0f;
    }
    // The board holds EN/PWM off below the gate, so the channel is dark
    if (current_a < limits.min_current_a) {
        return 0.0f;
    }
    const float step = limits.current_step_a;
    const int top = max_step(limits);
    float reference = output(top * step, reference_temp_c_);
    if (!(reference > 0.0f)) {
        return 0.0f;
    }
    // The driver rounds the current down to its step: convert by the light it gives
    current_a = std::min(current_a, max_current(limits));
    const float landed = std::floor(current_a / step + 1e-3f) * step;
    float level = limits.effective_pwm(pwm, landed) * output(landed, reference_temp_c_) / reference;
    return std::max(0.0f, std::min(level, 1.0f));
}

}  // namespace ledbrick
