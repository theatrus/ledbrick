#include "components/ledbrick_scheduler/led_dimming.h"
#include "test_framework.h"
#include <cmath>
#include <vector>

using namespace ledbrick;

namespace {

// A blue-like part: sublinear (droop), 1.0 at 0.35 A, rated to 1 A, -10% from 25 to 85 C
LedModel make_blue() {
    LedModel m;
    m.id = "test_blue";
    m.name = "Test blue";
    m.test_current_a = 0.35f;
    m.max_current_a = 1.0f;
    m.output_vs_current = {{0.05f, 0.16f}, {0.10f, 0.31f}, {0.35f, 1.0f}, {0.70f, 1.85f}, {1.0f, 2.5f}};
    m.output_vs_temp = {{25.0f, 1.0f}, {85.0f, 0.9f}};
    return m;
}

// A red-like part: steeper thermal drop, rated to 0.7 A
LedModel make_red() {
    LedModel m;
    m.id = "test_red";
    m.name = "Test red";
    m.test_current_a = 0.35f;
    m.max_current_a = 0.7f;
    m.output_vs_current = {{0.05f, 0.13f}, {0.35f, 1.0f}, {0.70f, 1.9f}};
    m.output_vs_temp = {{25.0f, 1.0f}, {85.0f, 0.7f}};
    return m;
}

DriveLimits limits(float max_current) {
    DriveLimits l;
    l.max_current_a = max_current;
    return l;
}

// Light the driver gives for a drive: current lands on the step below the command
float delivered(const ChannelDimmer& dimmer, const Drive& d, const DriveLimits& l, float temp) {
    float step_current = std::floor(d.current_a / l.current_step_a) * l.current_step_a;
    return dimmer.output(step_current, temp) * d.pwm;
}

}  // namespace

void test_curve_lookup(TestRunner& runner) {
    runner.start_suite("Curve Lookup Tests");
    std::vector<CurvePoint> curve = {{0.0f, 0.0f}, {1.0f, 2.0f}, {2.0f, 3.0f}};
    runner.assert_equals(1.0f, curve_lookup(curve, 0.5f), 1e-6f, "Linear between points");
    runner.assert_equals(2.5f, curve_lookup(curve, 1.5f), 1e-6f, "Second segment");
    runner.assert_equals(0.0f, curve_lookup(curve, -1.0f), 1e-6f, "Flat before the first point");
    runner.assert_equals(3.0f, curve_lookup(curve, 5.0f), 1e-6f, "Flat after the last point");
    runner.assert_equals(1.0f, curve_lookup(std::vector<CurvePoint>(), 3.0f), 1e-6f, "Empty curve is 1");
}

void test_channel_output(TestRunner& runner) {
    runner.start_suite("Channel Output Tests");
    LedModel blue = make_blue();
    LedModel red = make_red();

    ChannelDimmer single({{&blue, 13}}, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals(1.0f, single.output(0.35f, 25.0f), 1e-5f, "1.0 at the test current");
    runner.assert_equals(0.08f, single.output(0.025f, 25.0f), 1e-5f, "Runs linearly to zero below the curve");
    runner.assert_equals(0.0f, single.output(0.0f, 25.0f), 1e-6f, "Zero current gives zero");
    runner.assert_equals(0.9f, single.output(0.35f, 85.0f), 1e-5f, "Thermal drop applied");

    // Mixed string: count-weighted
    ChannelDimmer mixed({{&blue, 3}, {&red, 1}}, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals((3 * 1.85f + 1.9f) / 4.0f, mixed.output(0.7f, 25.0f), 1e-5f, "Count-weighted mix");
    runner.assert_equals(0.7f, mixed.max_current(limits(1.5f)), 1e-6f, "Lowest LED rating caps the channel");
    runner.assert_equals(0.5f, mixed.max_current(limits(0.5f)), 1e-6f, "Channel limit caps the channel");

    ChannelDimmer empty(std::vector<LedGroup>{{"no_such_led", 4}}, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_false(empty.valid(), "Unknown model gives an invalid dimmer");
    runner.assert_equals(0.0f, empty.drive_for_level(0.5f, limits(1.0f), 25.0f).pwm, 1e-6f, "Invalid dimmer stays off");
}

void test_current_first(TestRunner& runner) {
    runner.start_suite("Current-First Dimming Tests");
    LedModel blue = make_blue();
    ChannelDimmer dimmer({{&blue, 13}}, DimPriority::CURRENT_FIRST, 0.1f);
    DriveLimits l = limits(1.0f);

    Drive off = dimmer.drive_for_level(0.0f, l, 25.0f);
    runner.assert_equals(0.0f, off.pwm, 1e-6f, "Level 0: PWM off");
    runner.assert_equals(0.0f, off.current_a, 1e-6f, "Level 0: no current");

    Drive full = dimmer.drive_for_level(1.0f, l, 25.0f);
    runner.assert_equals(1.0f, full.pwm, 1e-6f, "Level 1: full PWM");
    runner.assert_true(full.current_a <= 1.0f, "Level 1: within the channel limit");
    runner.assert_true(full.current_a > 1.0f - 2 * l.current_step_a, "Level 1: at the top step");

    // Mid level: current carries it, PWM only trims
    Drive mid = dimmer.drive_for_level(0.5f, l, 25.0f);
    runner.assert_true(mid.pwm > 0.95f, "Mid level: PWM near full");
    runner.assert_true(mid.current_a > 0.3f && mid.current_a < 0.7f, "Mid level: current lowered");

    // Below the floor: floor current, PWM does the dimming
    Drive low = dimmer.drive_for_level(0.01f, l, 25.0f);
    runner.assert_true(low.current_a >= 0.1f && low.current_a < 0.1f + l.current_step_a, "Low level: floor current");
    runner.assert_true(low.pwm < 0.5f, "Low level: dimmed with PWM");

    // Every level from 0 to 1 gives exactly its share of the light, so ramps are smooth
    float reference = delivered(dimmer, full, l, 25.0f);
    bool exact = true;
    bool monotonic = true;
    bool safe = true;
    float previous = -1.0f;
    for (int i = 1; i <= 20000; i++) {
        float level = i / 20000.0f;
        Drive d = dimmer.drive_for_level(level, l, 25.0f);
        float light = delivered(dimmer, d, l, 25.0f);
        if (std::fabs(light - level * reference) > 1e-4f * reference) exact = false;
        if (light < previous) monotonic = false;
        previous = light;
        if (d.current_a > 1.0f || (d.pwm > 0.0f && d.current_a < l.min_current_a)) safe = false;
    }
    runner.assert_true(exact, "Light output is linear in level (within 0.01%)");
    runner.assert_true(monotonic, "Light output never steps backwards");
    runner.assert_true(safe, "Current within the limit and above the gate whenever lit");

    // The floor never drops below the board's 50 mA gate
    ChannelDimmer low_floor({{&blue, 13}}, DimPriority::CURRENT_FIRST, 0.0f);
    runner.assert_true(low_floor.drive_for_level(0.001f, l, 25.0f).current_a >= l.min_current_a, "Floor at least the gate");
}

void test_pwm_first(TestRunner& runner) {
    runner.start_suite("PWM-First Dimming Tests");
    LedModel blue = make_blue();
    ChannelDimmer dimmer({{&blue, 13}}, DimPriority::PWM_FIRST, 0.1f);
    DriveLimits l = limits(1.0f);

    Drive full = dimmer.drive_for_level(1.0f, l, 25.0f);
    Drive half = dimmer.drive_for_level(0.5f, l, 25.0f);
    Drive tenth = dimmer.drive_for_level(0.1f, l, 25.0f);
    runner.assert_equals(full.current_a, half.current_a, 1e-6f, "Current held at half level");
    runner.assert_equals(full.current_a, tenth.current_a, 1e-6f, "Current held at a tenth");
    runner.assert_equals(0.5f, half.pwm, 1e-5f, "PWM is the level");
    runner.assert_equals(0.1f, tenth.pwm, 1e-5f, "PWM is the level at a tenth");
    runner.assert_equals(0.0f, dimmer.drive_for_level(1e-6f, l, 25.0f).pwm, 1e-9f, "Below one PWM step: off");
}

void test_thermal_compensation(TestRunner& runner) {
    runner.start_suite("Thermal Compensation Tests");
    LedModel blue = make_blue();
    ChannelDimmer dimmer({{&blue, 13}}, DimPriority::CURRENT_FIRST, 0.1f);
    DriveLimits l = limits(1.0f);
    float reference = delivered(dimmer, dimmer.drive_for_level(1.0f, l, 25.0f), l, 25.0f);

    // Hot LEDs give less light per amp, so a mid level takes more current for the same light
    Drive cool = dimmer.drive_for_level(0.4f, l, 25.0f);
    Drive hot = dimmer.drive_for_level(0.4f, l, 85.0f);
    runner.assert_true(hot.current_a > cool.current_a, "More current when hot");
    runner.assert_equals(0.4f * reference, delivered(dimmer, hot, l, 85.0f), 1e-4f * reference, "Same light when hot");

    // Full level cannot be held when hot: saturate at the limit
    Drive full_hot = dimmer.drive_for_level(1.0f, l, 85.0f);
    runner.assert_equals(1.0f, full_hot.pwm, 1e-6f, "Saturates at full PWM");
    runner.assert_true(full_hot.current_a <= 1.0f, "Never above the limit when hot");

    // Cold LEDs are brighter: less current
    Drive cold = dimmer.drive_for_level(0.4f, l, 5.0f);
    runner.assert_true(cold.current_a <= cool.current_a, "Less or equal current when cold");

    // PWM-first compensates with PWM
    ChannelDimmer pwm_first({{&blue, 13}}, DimPriority::PWM_FIRST, 0.1f);
    float pwm_cool = pwm_first.drive_for_level(0.5f, l, 25.0f).pwm;
    float pwm_hot = pwm_first.drive_for_level(0.5f, l, 85.0f).pwm;
    runner.assert_equals(pwm_cool / 0.9f, pwm_hot, 1e-4f, "PWM-first raises PWM when hot");
}

void test_level_conversion(TestRunner& runner) {
    runner.start_suite("Manual To Level Conversion Tests");
    LedModel blue = make_blue();
    ChannelDimmer dimmer({{&blue, 13}}, DimPriority::CURRENT_FIRST, 0.1f);
    DriveLimits l = limits(1.0f);

    // A manual setting of full PWM at the top step is level 1
    Drive full = dimmer.drive_for_level(1.0f, l, 25.0f);
    runner.assert_equals(1.0f, dimmer.level_for_drive(1.0f, full.current_a, l), 0.01f, "Full is level 1");

    // Half PWM at half the test current: output ratio from the curve
    float level = dimmer.level_for_drive(0.5f, 0.35f, l);
    float expected = 0.5f * dimmer.output(0.35f, 25.0f) / dimmer.output(126 * l.current_step_a, 25.0f);
    runner.assert_equals(expected, level, 0.01f, "Half PWM at the test current");

    // Converting back gives the same light
    Drive back = dimmer.drive_for_level(level, l, 25.0f);
    float reference = delivered(dimmer, full, l, 25.0f);
    runner.assert_equals(0.5f * dimmer.output(0.35f, 25.0f), delivered(dimmer, back, l, 25.0f), 0.01f * reference,
                         "Round trip keeps the light");
    runner.assert_equals(0.0f, dimmer.level_for_drive(0.0f, 0.5f, l), 1e-6f, "PWM 0 is level 0");
}

int main() {
    TestResults results;
    TestRunner runner;

    std::cout << "=== LED DIMMING UNIT TESTS ===" << std::endl;

    test_curve_lookup(runner);
    results.add_suite_results(runner);

    test_channel_output(runner);
    results.add_suite_results(runner);

    test_current_first(runner);
    results.add_suite_results(runner);

    test_pwm_first(runner);
    results.add_suite_results(runner);

    test_thermal_compensation(runner);
    results.add_suite_results(runner);

    test_level_conversion(runner);
    results.add_suite_results(runner);

    results.print_final_summary("LED Dimming");

    return results.get_exit_code();
}
