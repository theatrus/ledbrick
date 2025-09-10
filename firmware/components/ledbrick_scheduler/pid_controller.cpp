#include "pid_controller.h"
#include <cmath>

namespace ledbrick {

PIDController::PIDController(float kp, float ki, float kd, float min_output, float max_output)
    : kp_(kp), ki_(ki), kd_(kd), target_(0.0f), integral_(0.0f), last_input_(0.0f),
      error_(0.0f), derivative_(0.0f), output_(0.0f), 
      min_output_(min_output), max_output_(max_output), first_run_(true) {
}

void PIDController::set_target(float target) {
    target_ = target;
}

void PIDController::set_tunings(float kp, float ki, float kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
}

void PIDController::set_output_limits(float min_output, float max_output) {
    min_output_ = min_output;
    max_output_ = max_output;
    
    // Clamp current output to new limits
    if (output_ > max_output_) output_ = max_output_;
    if (output_ < min_output_) output_ = min_output_;
    
    // Clamp integral to prevent windup
    if (ki_ > 0.0f) {
        float max_integral = (max_output_ - min_output_) / ki_;
        if (integral_ > max_integral) integral_ = max_integral;
        if (integral_ < -max_integral) integral_ = -max_integral;
    }
}

void PIDController::reset() {
    integral_ = 0.0f;
    last_input_ = 0.0f;
    error_ = 0.0f;
    derivative_ = 0.0f;
    output_ = 0.0f;
    first_run_ = true;
}

float PIDController::compute(float input, uint32_t dt_ms) {
    if (dt_ms == 0) {
        return output_; // No time passed, return last output
    }
    
    float dt_sec = static_cast<float>(dt_ms) / 1000.0f;
    
    // Calculate error
    error_ = target_ - input;
    
    // Integral term with windup protection
    integral_ += error_ * dt_sec;
    if (ki_ > 0.0f) {
        float max_integral = (max_output_ - min_output_) / ki_;
        integral_ = std::max(-max_integral, std::min(max_integral, integral_));
    }
    
    // Derivative term (derivative on measurement to avoid spikes on setpoint changes)
    if (first_run_) {
        derivative_ = 0.0f;
        first_run_ = false;
    } else {
        // Use derivative on measurement instead of error to avoid derivative kick
        derivative_ = -(input - last_input_) / dt_sec;
    }
    last_input_ = input;
    
    // Calculate PID output
    float p_term = kp_ * error_;
    float i_term = ki_ * integral_;
    float d_term = kd_ * derivative_;
    
    output_ = p_term + i_term + d_term;
    
    // Apply output limits
    output_ = std::max(min_output_, std::min(max_output_, output_));
    
    return output_;
}

} // namespace ledbrick