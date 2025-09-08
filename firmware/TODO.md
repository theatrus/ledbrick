# LEDBrick Scheduler - Home Assistant Services Implementation

## Overview
The LEDBrick Scheduler C++ component includes a planned Home Assistant services architecture that was partially implemented but needs completion due to ESPHome API service registration complexity.

## Planned Services Architecture

### 1. Schedule Management Services

#### `ledbrick_scheduler.set_schedule_point`
**Purpose:** Add or update a schedule point for all channels
**Parameters:**
- `time_minutes` (int, 0-1439): Time in minutes from midnight (e.g., 480 = 8:00 AM)
- `pwm_values` (float array): PWM percentages (0-100) for each channel
- `current_values` (float array): Current values (0-2A) for each channel

**Example Usage:**
```yaml
# Home Assistant automation
service: ledbrick_scheduler.set_schedule_point
data:
  time_minutes: 480  # 8:00 AM
  pwm_values: [0, 20, 60, 80, 60, 40, 20, 0]  # 8 channels
  current_values: [0, 0.3, 1.0, 1.5, 1.0, 0.6, 0.3, 0]
```

#### `ledbrick_scheduler.remove_schedule_point`
**Purpose:** Remove a schedule point at specific time
**Parameters:**
- `time_minutes` (int): Time point to remove

#### `ledbrick_scheduler.clear_schedule`
**Purpose:** Remove all schedule points
**Parameters:** None

### 2. Preset Management Services

#### `ledbrick_scheduler.load_preset`
**Purpose:** Load a built-in or saved preset schedule
**Parameters:**
- `preset_name` (string): Name of preset to load

**Built-in Presets:**
- `sunrise_sunset`: Natural lighting cycle (4 points: dawn, noon, dusk, night)
- `full_spectrum`: High-intensity full spectrum (4 points: morning, midday, afternoon, evening)
- `simple`: Basic on/off schedule (2 points: 8AM on, 8PM off)

**Example Usage:**
```yaml
service: ledbrick_scheduler.load_preset
data:
  preset_name: "sunrise_sunset"
```

#### `ledbrick_scheduler.save_preset`
**Purpose:** Save current schedule as named preset
**Parameters:**
- `preset_name` (string): Name for the new preset

### 3. Control Services

#### `ledbrick_scheduler.set_enabled`
**Purpose:** Enable or disable the scheduler
**Parameters:**
- `enabled` (boolean): True to enable, false to disable

#### `ledbrick_scheduler.get_status`
**Purpose:** Get current scheduler status and interpolated values
**Parameters:** None
**Returns:** Fires Home Assistant event with current status

### 4. Advanced Services (Future)

#### `ledbrick_scheduler.set_interpolation_curve`
**Purpose:** Change interpolation method between schedule points
**Parameters:**
- `curve_type` (string): "linear", "smooth", "bezier"

#### `ledbrick_scheduler.sync_sunrise_sunset`
**Purpose:** Automatically sync schedule points to sunrise/sunset times
**Parameters:**
- `location` (object): Latitude/longitude for sun calculations

## Current Implementation Status

### ✅ Completed
- C++ component structure with service handler methods
- Python codegen integration for ESPHome
- Basic automation actions (SetSchedulePointAction, LoadPresetAction, SetEnabledAction)
- Service method implementations in C++:
  - `on_set_schedule_point()`
  - `on_load_preset()`
  - `on_set_enabled()`
  - `on_get_status()`

### ❌ Blocked/Incomplete
- Service registration causes linking errors with ESPHome API
- ESPHome CustomAPIDevice service registration requires complex template specializations
- `std::vector<float>` parameter types not fully supported in ESPHome service system

## Implementation Approaches

### Option 1: Fix ESPHome Service Registration (Preferred)
**Status:** Attempted but blocked by linking errors
**Issue:** ESPHome's CustomAPIDevice service registration has incomplete template specializations for `std::vector<float>` types
**Solution:** 
- Contribute fixes to ESPHome codebase for vector parameter support
- Or implement custom service parameter serialization

### Option 2: Use ESPHome Native Services (Alternative)
**Approach:** Define services in YAML and connect to component methods
```yaml
api:
  services:
    - service: set_schedule_point
      variables:
        time_minutes: int
        pwm_ch1: float
        pwm_ch2: float
        # ... repeat for all channels
        current_ch1: float
        current_ch2: float
        # ... repeat for all channels
      then:
        - lambda: |-
            std::vector<float> pwm_values = {pwm_ch1, pwm_ch2, ...};
            std::vector<float> current_values = {current_ch1, current_ch2, ...};
            id(main_scheduler).set_schedule_point(time_minutes, pwm_values, current_values);
```

### Option 3: Home Assistant Template Integration
**Approach:** Use Home Assistant's template system to call simpler services
```yaml
# In Home Assistant
script:
  set_full_spectrum_schedule:
    sequence:
      - service: esphome.ledbrickplus_set_schedule_point
        data_template:
          time_minutes: 480
          pwm_values: "{{ [0, 20, 60, 80, 60, 40, 20, 0] | join(',') }}"
```

## Recommended Next Steps

1. **Immediate:** Use Option 2 (ESPHome Native Services) for basic functionality
2. **Short-term:** Implement preset loading via simple string parameter services
3. **Long-term:** Contribute ESPHome fixes for complex parameter types (Option 1)

## Service Interface Design Goals

- **Simple:** Easy to call from Home Assistant automations
- **Flexible:** Support both individual channel control and preset loading  
- **Efficient:** Minimal Home Assistant entity overhead
- **Extensible:** Easy to add advanced features like curve interpolation
- **Robust:** Proper error handling and validation

## Example Home Assistant Integration

```yaml
# automation.yaml
automation:
  - alias: "Morning Lighting Schedule"
    trigger:
      - platform: time
        at: "06:00:00"
    action:
      - service: ledbrick_scheduler.load_preset
        data:
          preset_name: "sunrise_sunset"
      - service: ledbrick_scheduler.set_enabled
        data:
          enabled: true

  - alias: "Custom Evening Lighting"  
    trigger:
      - platform: time
        at: "20:00:00"
    action:
      - service: ledbrick_scheduler.set_schedule_point
        data:
          time_minutes: 1200  # 8 PM
          pwm_values: [10, 20, 30, 40, 30, 20, 10, 5]
          current_values: [0.2, 0.4, 0.6, 0.8, 0.6, 0.4, 0.2, 0.1]
```

This architecture provides a clean, professional interface for managing complex lighting schedules while maintaining the performance benefits of the C++ implementation.