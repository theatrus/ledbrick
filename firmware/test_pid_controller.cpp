#include "components/ledbrick_scheduler/pid_controller.h"
#include "test_framework.h"
#include <cmath>

using namespace ledbrick;

void test_initialization(TestRunner& runner) {
    runner.start_suite("PID Initialization Tests");
    
    PIDController pid(2.0f, 0.5f, 0.1f, -100.0f, 100.0f);
    
    float kp, ki, kd;
    pid.get_tunings(kp, ki, kd);
    runner.assert_equals(2.0f, kp, 0.001f, "Initial Kp");
    runner.assert_equals(0.5f, ki, 0.001f, "Initial Ki");
    runner.assert_equals(0.1f, kd, 0.001f, "Initial Kd");
    
    float min_out, max_out;
    pid.get_output_limits(min_out, max_out);
    runner.assert_equals(-100.0f, min_out, 0.001f, "Initial min output");
    runner.assert_equals(100.0f, max_out, 0.001f, "Initial max output");
    
    runner.assert_equals(0.0f, pid.get_target(), 0.001f, "Initial target");
    runner.assert_equals(0.0f, pid.get_output(), 0.001f, "Initial output");
}

void test_proportional_only(TestRunner& runner) {
    runner.start_suite("P-Only Controller Tests");
    
    PIDController pid(2.0f, 0.0f, 0.0f, -100.0f, 100.0f);
    pid.set_target(50.0f);
    
    // Test proportional response
    float output = pid.compute(40.0f, 1000); // 10 degree error
    runner.assert_equals(20.0f, output, 0.001f, "P-only output (error=10)");
    
    output = pid.compute(45.0f, 1000); // 5 degree error
    runner.assert_equals(10.0f, output, 0.001f, "P-only output (error=5)");
    
    output = pid.compute(55.0f, 1000); // -5 degree error
    runner.assert_equals(-10.0f, output, 0.001f, "P-only output (error=-5)");
}

void test_integral_action(TestRunner& runner) {
    runner.start_suite("I-Controller Tests");
    
    PIDController pid(0.0f, 1.0f, 0.0f, -100.0f, 100.0f);
    pid.set_target(50.0f);
    
    // Accumulate integral over time
    float output1 = pid.compute(40.0f, 1000); // 10 degree error for 1 second
    runner.assert_equals(10.0f, output1, 0.001f, "I-only output after 1s");
    
    float output2 = pid.compute(40.0f, 1000); // Another 1 second
    runner.assert_equals(20.0f, output2, 0.001f, "I-only output after 2s");
    
    float output3 = pid.compute(40.0f, 500); // 0.5 seconds
    runner.assert_equals(25.0f, output3, 0.001f, "I-only output after 2.5s");
    
    // Test integral decay when error changes sign
    float output4 = pid.compute(60.0f, 1000); // -10 degree error for 1 second
    runner.assert_equals(15.0f, output4, 0.001f, "I-only output with opposite error");
}

void test_derivative_action(TestRunner& runner) {
    runner.start_suite("D-Controller Tests");
    
    PIDController pid(0.0f, 0.0f, 1.0f, -100.0f, 100.0f);
    pid.set_target(50.0f);
    
    // First computation - no derivative yet
    float output1 = pid.compute(40.0f, 1000);
    runner.assert_equals(0.0f, output1, 0.001f, "D-only first output (no history)");
    
    // Temperature rising (input increasing)
    float output2 = pid.compute(45.0f, 1000); // +5 degrees/second
    runner.assert_equals(-5.0f, output2, 0.001f, "D-only output (rising temp)");
    
    // Temperature falling (input decreasing)
    float output3 = pid.compute(42.0f, 1000); // -3 degrees/second
    runner.assert_equals(3.0f, output3, 0.001f, "D-only output (falling temp)");
    
    // No change
    float output4 = pid.compute(42.0f, 1000);
    runner.assert_equals(0.0f, output4, 0.001f, "D-only output (no change)");
}

void test_full_pid(TestRunner& runner) {
    runner.start_suite("Full PID Controller Tests");
    
    PIDController pid(2.0f, 0.5f, 1.0f, 0.0f, 100.0f);
    pid.set_target(50.0f);
    
    // Initial state: temp = 40°C, error = 10°C
    float output1 = pid.compute(40.0f, 1000);
    // P = 2 * 10 = 20
    // I = 0.5 * 10 * 1 = 5
    // D = 0 (first run)
    runner.assert_equals(25.0f, output1, 0.001f, "Full PID first output");
    
    // Temp rises to 45°C
    float output2 = pid.compute(45.0f, 1000);
    // P = 2 * 5 = 10
    // I = 5 + 0.5 * 5 * 1 = 7.5
    // D = 1 * (-5) = -5 (derivative on measurement)
    runner.assert_equals(12.5f, output2, 0.001f, "Full PID second output");
}

void test_output_limits(TestRunner& runner) {
    runner.start_suite("Output Limiting Tests");
    
    PIDController pid(10.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    pid.set_target(50.0f);
    
    // Test upper limit
    float output1 = pid.compute(30.0f, 1000); // Would be 200 without limit
    runner.assert_equals(100.0f, output1, 0.001f, "Output clamped to max");
    
    // Test lower limit
    float output2 = pid.compute(70.0f, 1000); // Would be -200 without limit
    runner.assert_equals(0.0f, output2, 0.001f, "Output clamped to min");
    
    // Test within limits
    float output3 = pid.compute(45.0f, 1000); // Should be 50
    runner.assert_equals(50.0f, output3, 0.001f, "Output within limits");
}

void test_integral_windup_prevention(TestRunner& runner) {
    runner.start_suite("Integral Windup Prevention Tests");
    
    PIDController pid(1.0f, 1.0f, 0.0f, 0.0f, 100.0f);
    pid.set_target(50.0f);
    
    // Create large sustained error
    for (int i = 0; i < 20; i++) {
        pid.compute(20.0f, 1000); // 30 degree error
    }
    
    // Integral should be limited
    // Max output = 100, min output = 0, ki = 1.0
    // Max integral = (100 - 0) / 1.0 = 100
    runner.assert_equals(100.0f, pid.get_integral(), 0.001f, "Integral windup prevented");
    runner.assert_equals(100.0f, pid.get_output(), 0.001f, "Output at maximum");
    
    // When temperature goes above target, output should respond immediately
    float output = pid.compute(55.0f, 1000); // -5 degree error (above target)
    // Error = -5, so integral will decrease
    // P = 1 * (-5) = -5, I will decrease from 100
    // Integral = 100 + (-5 * 1) = 95
    // Total = -5 + 95 = 90
    
    runner.assert_true(output < 100.0f, "Output responds despite windup");
}

void test_reset(TestRunner& runner) {
    runner.start_suite("Reset Tests");
    
    PIDController pid(2.0f, 0.5f, 1.0f, 0.0f, 100.0f);
    pid.set_target(50.0f);
    
    // Build up some state
    pid.compute(40.0f, 1000);
    pid.compute(45.0f, 1000);
    
    // Verify state exists
    runner.assert_true(pid.get_integral() > 0.0f, "Integral accumulated");
    runner.assert_true(pid.get_output() > 0.0f, "Output non-zero");
    
    // Reset
    pid.reset();
    
    // Verify reset
    runner.assert_equals(0.0f, pid.get_integral(), 0.001f, "Integral reset");
    runner.assert_equals(0.0f, pid.get_error(), 0.001f, "Error reset");
    runner.assert_equals(0.0f, pid.get_derivative(), 0.001f, "Derivative reset");
    runner.assert_equals(0.0f, pid.get_output(), 0.001f, "Output reset");
    
    // First computation after reset should have no derivative
    pid.compute(40.0f, 1000);
    runner.assert_equals(0.0f, pid.get_derivative(), 0.001f, "No derivative after reset");
}

void test_setpoint_change(TestRunner& runner) {
    runner.start_suite("Setpoint Change Tests");
    
    PIDController pid(2.0f, 0.5f, 1.0f, 0.0f, 100.0f);
    
    // Initial setpoint
    pid.set_target(50.0f);
    float output1 = pid.compute(50.0f, 1000); // At target
    runner.assert_equals(0.0f, output1, 0.001f, "Output at target");
    
    // Change setpoint
    pid.set_target(60.0f);
    float output2 = pid.compute(50.0f, 1000); // Now 10 below target
    
    // P = 2 * 10 = 20
    // I = 0.5 * 10 * 1 = 5
    // D = 0 (derivative on measurement, input didn't change)
    runner.assert_equals(25.0f, output2, 0.001f, "Output after setpoint change");
    runner.assert_equals(0.0f, pid.get_derivative(), 0.001f, "No derivative kick");
}

void test_derivative_on_measurement(TestRunner& runner) {
    runner.start_suite("Derivative on Measurement Tests");
    
    PIDController pid(0.0f, 0.0f, 10.0f, -100.0f, 100.0f);
    
    // Test that derivative is based on input change, not error change
    pid.set_target(50.0f);
    pid.compute(40.0f, 1000); // Initialize
    
    // Change setpoint - should not cause derivative spike
    pid.set_target(60.0f);
    float output1 = pid.compute(40.0f, 1000); // Input unchanged
    runner.assert_equals(0.0f, output1, 0.001f, "No derivative on setpoint change");
    
    // Change input - should cause derivative response
    float output2 = pid.compute(42.0f, 1000); // Input increased by 2
    runner.assert_equals(-20.0f, output2, 0.001f, "Derivative on input change");
}

void test_zero_time_delta(TestRunner& runner) {
    runner.start_suite("Zero Time Delta Tests");
    
    PIDController pid(2.0f, 0.5f, 1.0f, 0.0f, 100.0f);
    pid.set_target(50.0f);
    
    float output1 = pid.compute(40.0f, 1000);
    float output2 = pid.compute(45.0f, 0); // Zero time delta
    
    runner.assert_equals(output1, output2, 0.001f, "Output unchanged with zero dt");
}

void test_tuning_changes(TestRunner& runner) {
    runner.start_suite("Tuning Change Tests");
    
    PIDController pid(1.0f, 0.1f, 0.01f, 0.0f, 100.0f);
    pid.set_target(50.0f);
    
    float output1 = pid.compute(40.0f, 1000);
    
    // Change tuning
    pid.set_tunings(2.0f, 0.2f, 0.02f);
    
    float output2 = pid.compute(40.0f, 1000);
    
    // New output should reflect new tuning
    // Old: P=10, I=1, D=0, Total=11
    // New: P=20, I=2+2=4, D=0, Total=24
    runner.assert_true(output2 > output1 * 2.0f, "Output increased with higher gains");
}

void test_steady_state_error(TestRunner& runner) {
    runner.start_suite("Steady State Error Tests");
    
    // P-only controller should have steady state error
    PIDController p_controller(2.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    p_controller.set_target(50.0f);
    
    // Simulate reaching steady state
    float temp = 40.0f;
    for (int i = 0; i < 100; i++) {
        float output = p_controller.compute(temp, 100);
        temp += output * 0.01f; // Simple simulation
    }
    
    runner.assert_true(std::abs(temp - 50.0f) > 0.1f, "P-only has steady state error");
    
    // PI controller should eliminate steady state error
    // Use conservative gains for stable convergence
    PIDController pi_controller(0.5f, 0.01f, 0.0f, -100.0f, 100.0f);
    pi_controller.set_target(50.0f);
    
    temp = 40.0f;
    // Run longer simulation with small time steps
    for (int i = 0; i < 5000; i++) {
        float output = pi_controller.compute(temp, 100); // 0.1 second time step
        // Simulate a simple linear process
        temp += output * 0.001f;
        
        // Clamp temperature to realistic range
        if (temp > 100.0f) temp = 100.0f;
        if (temp < 0.0f) temp = 0.0f;
    }
    
    // Accept within 2 degrees as the simple euler simulation is not perfect
    runner.assert_true(std::abs(temp - 50.0f) < 2.0f, "PI reduces steady state error");
}

// Main test runner
int main() {
    TestResults results;
    TestRunner runner;
    
    std::cout << "=== PID CONTROLLER UNIT TESTS ===" << std::endl;
    
    test_initialization(runner);
    results.add_suite_results(runner);
    
    test_proportional_only(runner);
    results.add_suite_results(runner);
    
    test_integral_action(runner);
    results.add_suite_results(runner);
    
    test_derivative_action(runner);
    results.add_suite_results(runner);
    
    test_full_pid(runner);
    results.add_suite_results(runner);
    
    test_output_limits(runner);
    results.add_suite_results(runner);
    
    test_integral_windup_prevention(runner);
    results.add_suite_results(runner);
    
    test_reset(runner);
    results.add_suite_results(runner);
    
    test_setpoint_change(runner);
    results.add_suite_results(runner);
    
    test_derivative_on_measurement(runner);
    results.add_suite_results(runner);
    
    test_zero_time_delta(runner);
    results.add_suite_results(runner);
    
    test_tuning_changes(runner);
    results.add_suite_results(runner);
    
    test_steady_state_error(runner);
    results.add_suite_results(runner);
    
    results.print_final_summary("PID Controller");
    
    return results.get_exit_code();
}