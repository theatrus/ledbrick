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

### ✅ Completed (Updated Implementation)
- **C++ Component**: Professional LEDBrickScheduler class with complete scheduling logic
- **Python Integration**: ESPHome codegen integration and automation actions
- **Template-Based Services**: Native ESPHome service system with individual channel parameters
- **Local Storage**: Binary format persistence to ESP32 flash memory (4KB storage)
- **Home Assistant Integration**: Complete UI helpers, scripts, and automations
- **Preset System**: Built-in presets (sunrise_sunset, full_spectrum, simple)
- **Linear Interpolation**: Smooth transitions between schedule points
- **Current Limiting**: Per-channel maximum current enforcement
- **Status Reporting**: Template sensors for monitoring scheduler state

### ✅ Working Services (Template-Based Approach)
- `set_schedule_point_8ch` - Set schedule point for all 8 channels
- `set_schedule_point_4ch` - Simplified 4-channel version for testing  
- `remove_schedule_point` - Remove schedule point at specific time
- `clear_schedule` - Remove all schedule points
- `load_preset` - Load built-in or saved presets
- `save_preset` - Save current schedule as named preset
- `set_scheduler_enabled` - Enable/disable scheduler
- `get_scheduler_status` - Log current status and values
- `export_schedule` - Export schedule as JSON to logs
- `save_to_flash` / `load_from_flash` - Manual flash storage operations

### ❌ Previous Approach (Documented as Not Working)
- ESPHome CustomAPIDevice service registration with `std::vector<float>` parameters
- Direct C++ service method binding (linking errors)
- Complex parameter type serialization (template specialization issues)

## Implementation Approaches

### ✅ Option 2: ESPHome Native Services (IMPLEMENTED)
**Status:** Successfully implemented and tested
**Approach:** Define services in YAML and connect to component methods via lambda functions
**Benefits:**
- No linking issues with complex parameter types
- Works with existing ESPHome infrastructure
- Full control over parameter validation and conversion
- Easy to extend and modify

**Example Implementation:**
```yaml
api:
  services:
    - service: set_schedule_point_8ch
      variables:
        time_minutes: int
        pwm_ch1: float
        pwm_ch2: float
        # ... all 8 channels for both PWM and current
      then:
        - lambda: |-
            std::vector<float> pwm_values = {pwm_ch1, pwm_ch2, pwm_ch3, pwm_ch4, pwm_ch5, pwm_ch6, pwm_ch7, pwm_ch8};
            std::vector<float> current_values = {current_ch1, current_ch2, current_ch3, current_ch4, current_ch5, current_ch6, current_ch7, current_ch8};
            id(main_scheduler).set_schedule_point(time_minutes, pwm_values, current_values);
```

### ❌ Option 1: ESPHome CustomAPIDevice Services (FAILED)
**Status:** Attempted but blocked by linking errors
**Issue:** ESPHome's CustomAPIDevice has incomplete template specializations for `std::vector<float>` parameters
**Conclusion:** Not viable with current ESPHome version

### ✅ Option 3: Home Assistant Template Integration (IMPLEMENTED)
**Status:** Complete with UI helpers, scripts, and automations
**Approach:** Home Assistant provides rich UI and logic, calls ESPHome services with individual parameters
**Benefits:**
- Rich Home Assistant dashboard integration
- Input helpers for easy configuration
- Automated scheduling via time-based automations
- Script shortcuts for common configurations

## Implementation Complete

### ✅ Ready for Use
The LEDBrick Scheduler is now fully implemented with:

1. **ESPHome Component**: Professional C++ scheduler with flash storage (`components/ledbrick_scheduler/`)
2. **Service Integration**: Template-based services for Home Assistant (`packages/scheduler_services.yaml`)  
3. **Home Assistant Helpers**: Complete UI and automation configuration (`homeassistant_integration.yaml`)
4. **Test Configuration**: Working example with all 8 channels (`test_scheduler_with_services.yaml`)

### Quick Start
1. **Enable in LEDBrick**: Uncomment scheduler package line in `ledbrick-plus.yaml:78`
2. **Add External Component**: Include scheduler component in configuration
3. **Import HA Config**: Copy `homeassistant_integration.yaml` to Home Assistant
4. **Configure Schedule**: Use Home Assistant UI or call services directly

### Usage Examples
```bash
# Load a preset via ESPHome service
service: esphome.ledbrickplus_load_preset
data:
  preset_name: "sunrise_sunset"

# Set custom schedule point  
service: esphome.ledbrickplus_set_schedule_point_8ch
data:
  time_minutes: 480  # 8:00 AM
  pwm_ch1: 0
  pwm_ch2: 20
  # ... etc for all channels
```

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