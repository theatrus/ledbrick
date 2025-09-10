#pragma once

#include <cstdint>
#include <algorithm>

namespace ledbrick {

// Standalone PID Controller implementation
class PIDController {
public:
    // Constructor with default output limits
    PIDController(float kp = 1.0f, float ki = 0.1f, float kd = 0.01f, 
                  float min_output = 0.0f, float max_output = 100.0f);
    
    // Set the target (setpoint) value
    void set_target(float target);
    
    // Get the current target value
    float get_target() const { return target_; }
    
    // Set PID tuning parameters
    void set_tunings(float kp, float ki, float kd);
    
    // Get PID tuning parameters
    void get_tunings(float& kp, float& ki, float& kd) const {
        kp = kp_;
        ki = ki_;
        kd = kd_;
    }
    
    // Set output limits for anti-windup
    void set_output_limits(float min_output, float max_output);
    
    // Get output limits
    void get_output_limits(float& min_output, float& max_output) const {
        min_output = min_output_;
        max_output = max_output_;
    }
    
    // Reset the controller state
    void reset();
    
    // Compute the control output
    float compute(float input, uint32_t dt_ms);
    
    // Get the last computed values for debugging
    float get_error() const { return error_; }
    float get_integral() const { return integral_; }
    float get_derivative() const { return derivative_; }
    float get_output() const { return output_; }
    
private:
    // PID tuning parameters
    float kp_;          // Proportional gain
    float ki_;          // Integral gain
    float kd_;          // Derivative gain
    
    // Controller state
    float target_;      // Setpoint
    float integral_;    // Integral accumulator
    float last_input_;  // Previous input for derivative calculation
    float error_;       // Last error
    float derivative_;  // Last derivative
    float output_;      // Last output
    
    // Output limits
    float min_output_;
    float max_output_;
    
    bool first_run_;    // Flag for first computation
};

} // namespace ledbrick