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
    m.curve_temp_c = 25.0f;
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
    m.curve_temp_c = 25.0f;
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

void test_junction_heating(TestRunner& runner) {
    runner.start_suite("Junction Heating Tests");
    // Same part, now with a thermal resistance and Vf: the junction runs above the pad by
    // Rth x Vf x I, so output drops faster with current
    LedModel plain = make_blue();
    LedModel heated = make_blue();
    heated.rth_c_per_w = 10.0f;
    heated.vf_vs_current = {{0.0f, 3.0f}, {1.0f, 3.0f}};
    ChannelDimmer a({{&plain, 1}}, DimPriority::CURRENT_FIRST, 0.1f);
    ChannelDimmer b({{&heated, 1}}, DimPriority::CURRENT_FIRST, 0.1f);
    // At 1 A: junction 30 C above the pad, output 0.9 + 0.1 * (1 - 30/60) per the curve
    float expected = a.output(1.0f, 55.0f);
    runner.assert_equals(expected, b.output(1.0f, 25.0f), 1e-4f, "1 A heats the junction by 30 C");
    runner.assert_true(b.output(0.1f, 25.0f) > 0.99f * a.output(0.1f, 25.0f), "Little heating at 0.1 A");

    // The dimmer still delivers the exact level with self-heating
    DriveLimits l = limits(1.0f);
    Drive full = b.drive_for_level(1.0f, l, 25.0f);
    float reference = delivered(b, full, l, 25.0f);
    Drive half = b.drive_for_level(0.5f, l, 25.0f);
    runner.assert_equals(0.5f * reference, delivered(b, half, l, 25.0f), 1e-4f * reference, "Exact level with self-heating");
}

void test_characterized_floor(TestRunner& runner) {
    runner.start_suite("Characterized Floor Tests");
    // The curve starts at 0.2 A: below that the output is unknown, so the floor rises to it
    LedModel late = make_blue();
    late.output_vs_current = {{0.2f, 0.6f}, {0.35f, 1.0f}, {1.0f, 2.5f}};
    ChannelDimmer dimmer({{&late, 4}}, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals(0.2f, dimmer.characterized_current(), 1e-6f, "Characterized from 0.2 A");
    Drive low = dimmer.drive_for_level(0.01f, limits(1.0f), 25.0f);
    runner.assert_true(low.current_a >= 0.2f, "Floor raised to the characterized current");

    // Mixed string: the highest of the parts' starting currents
    LedModel early = make_blue();
    ChannelDimmer mixed({{&late, 1}, {&early, 1}}, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals(0.2f, mixed.characterized_current(), 1e-6f, "Mixed string uses the highest start");
}

void test_builtin_models(TestRunner& runner) {
    runner.start_suite("Built-in Model Tests");
    // Every emitter channel's default LEDs have curves, normalized at their test current
    for (uint8_t ch = 0; ch < 8; ch++) {
        auto groups = default_channel_leds(ch, 8);
        runner.assert_true(!groups.empty(), "Channel " + std::to_string(ch + 1) + " has default LEDs");
        for (const auto& g : groups) {
            const LedModel* m = find_led_model(g.model);
            runner.assert_true(m != nullptr, "Model known: " + g.model);
            if (m) {
                runner.assert_equals(1.0f, curve_lookup(m->output_vs_current, m->test_current_a), 0.003f,
                                     g.model + " is 1.0 at its test current");
                runner.assert_equals(1.0f, curve_lookup(m->output_vs_temp, 85.0f), 0.003f,
                                     g.model + " is 1.0 at 85 C");
            }
        }
        ChannelDimmer dimmer(groups, DimPriority::CURRENT_FIRST, 0.1f);
        runner.assert_true(dimmer.valid(), "Channel " + std::to_string(ch + 1) + " dimmer is valid");
    }
    runner.assert_true(default_channel_leds(0, 4).empty(), "No defaults for other channel counts");

    // WW is 3900K whites and PC ambers, no deep reds
    auto ww_leds = default_channel_leds(2, 8);
    runner.assert_true(ww_leds.size() == 2 && ww_leds[0].model == "luxeon_c_white_3900k" && ww_leds[0].count == 4 &&
                       ww_leds[1].model == "luxeon_c_pc_amber" && ww_leds[1].count == 4,
                       "WW is 4 LUXEON C 3900K whites and 4 PC ambers");
    ChannelDimmer ww(ww_leds, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals(1.0f, ww.max_current(limits(1.0f)), 1e-6f, "WW is not held to a deep red's 700 mA");

    // A string with deep reds (rated 700 mA) cannot go above 700 mA, whatever the channel limit
    ChannelDimmer with_red(std::vector<LedGroup>{{"luxeon_c_white_3900k", 4}, {"luxeon_c_deep_red", 2}},
                           DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals(0.7f, with_red.max_current(limits(1.0f)), 1e-6f, "Deep red caps a string at 700 mA");

    // Real curves give a smooth, exact ramp too. Level 1 is the output at 25 C, so at 45 C
    // the top of the range saturates at the maximum current.
    ChannelDimmer rubix(default_channel_leds(4, 8), DimPriority::CURRENT_FIRST, 0.1f);
    DriveLimits l = limits(1.2f);
    float reference = delivered(rubix, rubix.drive_for_level(1.0f, l, 25.0f), l, 25.0f);
    float achievable = delivered(rubix, rubix.drive_for_level(1.0f, l, 45.0f), l, 45.0f);
    runner.assert_true(achievable < reference, "Less light available at 45 C");
    bool exact = true;
    bool monotonic = true;
    float previous = -1.0f;
    for (int i = 1; i <= 2000; i++) {
        float level = i / 2000.0f;
        float light = delivered(rubix, rubix.drive_for_level(level, l, 45.0f), l, 45.0f);
        if (std::fabs(light - std::min(level * reference, achievable)) > 1e-4f * reference) exact = false;
        if (light < previous) monotonic = false;
        previous = light;
    }
    runner.assert_true(exact, "Rubix at 45 C holds each level until the maximum current");
    runner.assert_true(monotonic, "Rubix ramp never steps backwards");
    runner.assert_true(rubix.characterized_current() > 0.15f, "Rubix floor at its characterized current");
}

void test_custom_models(TestRunner& runner) {
    runner.start_suite("Custom Model Tests");

    // The built-in models pass the checks custom ones get
    for (const auto& model : builtin_led_models()) {
        std::string error;
        runner.assert_true(validate_led_model(model, &error), model.id + " passes validation " + error);
    }

    LedModel blue = make_blue();
    std::string error;
    runner.assert_true(validate_led_model(blue, &error), "A complete model is valid");
    LedModel minimal;
    minimal.id = "minimal";
    minimal.max_current_a = 0.5f;
    minimal.output_vs_current = {{0.05f, 0.1f}, {0.5f, 1.0f}};
    runner.assert_true(validate_led_model(minimal, &error), "Only the current curve is required");

    auto rejects = [&](void (*change)(LedModel&), const std::string& what) {
        LedModel m = make_blue();
        change(m);
        std::string why;
        bool valid = validate_led_model(m, &why);
        runner.assert_true(!valid && !why.empty(), what + " rejected: " + why);
    };
    rejects([](LedModel& m) { m.id = ""; }, "Empty id");
    rejects([](LedModel& m) { m.id = "Blue LED"; }, "Id with capitals and a space");
    rejects([](LedModel& m) { m.id = std::string(33, 'a'); }, "Id over 32 characters");
    rejects([](LedModel& m) { m.name = std::string(49, 'a'); }, "Name over 48 characters");
    rejects([](LedModel& m) { m.max_current_a = 0.0f; }, "Zero max current");
    rejects([](LedModel& m) { m.max_current_a = 3.5f; }, "Max current over 3 A");
    rejects([](LedModel& m) { m.max_current_a = NAN; }, "NaN max current");
    rejects([](LedModel& m) { m.test_current_a = -0.1f; }, "Negative test current");
    rejects([](LedModel& m) { m.output_vs_current.resize(1); }, "One-point current curve");
    rejects([](LedModel& m) { m.output_vs_current.assign(33, {0.5f, 1.0f}); }, "33-point current curve");
    rejects([](LedModel& m) { m.output_vs_current[0].x = 0.0f; }, "Current curve starting at 0 A");
    rejects([](LedModel& m) { std::swap(m.output_vs_current[1], m.output_vs_current[2]); }, "Currents out of order");
    rejects([](LedModel& m) { m.output_vs_current[1].x = m.output_vs_current[0].x; }, "Repeated current");
    rejects([](LedModel& m) { m.output_vs_current[3].y = 0.5f; }, "Output falling as current rises");
    rejects([](LedModel& m) { m.output_vs_current.pop_back(); }, "Current curve short of max current");
    rejects([](LedModel& m) { m.output_vs_current[2].y = INFINITY; }, "Infinite output");
    rejects([](LedModel& m) { m.output_vs_temp = {{25.0f, 1.0f}}; }, "One-point temperature curve");
    rejects([](LedModel& m) { m.output_vs_temp = {{25.0f, 1.0f}, {85.0f, 0.0f}}; }, "Zero output at temperature");
    rejects([](LedModel& m) { m.output_vs_temp = {{-50.0f, 1.0f}, {85.0f, 0.9f}}; }, "Temperature below -40 C");
    rejects([](LedModel& m) { m.curve_temp_c = 250.0f; }, "Curve temperature over 200 C");
    rejects([](LedModel& m) { m.rth_c_per_w = -1.0f; }, "Negative thermal resistance");
    rejects([](LedModel& m) { m.vf_vs_current = {{0.1f, 2.8f}, {0.5f, 12.0f}}; }, "Vf over 10 V");

    // Custom models are found first, so one can replace a built-in
    const std::string builtin_id = builtin_led_models()[0].id;
    LedModel replacement = make_blue();
    replacement.id = builtin_id;
    std::vector<LedModel> custom = {replacement, make_red()};
    runner.assert_true(find_led_model(builtin_id, custom) == &custom[0], "Custom model replaces the built-in");
    runner.assert_true(find_led_model("test_red", custom) == &custom[1], "Custom-only model found");
    runner.assert_true(find_led_model("luxeon_c_blue", custom) == find_led_model("luxeon_c_blue"), "Other built-ins still found");
    runner.assert_true(find_led_model("test_red") == nullptr, "Built-in lookup ignores custom models");

    ChannelDimmer dimmer({{"test_red", 4}}, custom, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_true(dimmer.valid(), "Dimmer built from a custom model");
    runner.assert_equals(1.0f, dimmer.output(0.35f, 25.0f), 1e-4f, "Dimmer uses the custom curve");
    ChannelDimmer replaced({{builtin_id, 4}}, custom, DimPriority::CURRENT_FIRST, 0.1f);
    runner.assert_equals(1.0f, replaced.output(0.35f, 25.0f), 1e-4f, "Dimmer uses the replacement curve");
    runner.assert_false(ChannelDimmer({{"test_red", 4}}, DimPriority::CURRENT_FIRST, 0.1f).valid(),
                        "Without the custom list the model is unknown");
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

    test_junction_heating(runner);
    results.add_suite_results(runner);

    test_characterized_floor(runner);
    results.add_suite_results(runner);

    test_builtin_models(runner);
    results.add_suite_results(runner);

    test_custom_models(runner);
    results.add_suite_results(runner);

    results.print_final_summary("LED Dimming");

    return results.get_exit_code();
}
