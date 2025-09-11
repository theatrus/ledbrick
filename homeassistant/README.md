# LEDBrick Home Assistant Integration

This directory contains a comprehensive Home Assistant configuration demonstrating all LEDBrick features.

## Directory Structure

```
homeassistant/
├── README.md                      # This file
├── ledbrick_package.yaml          # Complete package for tank groups
├── dashboards/
│   ├── tank_group_dashboard.yaml  # Main dashboard for tank groups
│   └── simple_card.yaml           # Simple card for quick setup
└── automations/
    └── multi_unit_examples.yaml   # Examples for coordinating multiple units
```

## Tank Groups

This package uses a tank group approach, perfect for:
- **Large tanks** with multiple LEDBrick fixtures that need synchronized control
- **Multiple tanks** each with their own group of lights
- **Mixed setups** with both large multi-fixture tanks and smaller single-fixture tanks

Examples:
- 6-foot reef tank with 3 LEDBrick units (left, center, right)
- 4-foot planted tank with 2 LEDBrick units
- 2-foot frag tank with 1 LEDBrick unit
- Quarantine tank with 1 LEDBrick unit

## Features Demonstrated

### 1. **Dynamic Scheduling**
- Sunrise/sunset relative scheduling
- Solar noon optimization
- Civil/nautical/astronomical twilight support
- Seasonal adjustments

### 2. **Moon Simulation**
- Automatic moonlight when lights are off
- Moon phase-based intensity
- Moonrise/moonset awareness
- Tank-specific configurations

### 3. **PWM Scaling**
- Global brightness control
- Feeding mode dimming
- Photography mode
- Energy saving modes
- LED aging compensation

### 4. **Channel Control**
- Individual PWM control (0-100%)
- Current limiting (0-5A)
- Multi-channel coordination
- Channel grouping

### 5. **Automation Examples**
- Time-based schedules
- Sensor-based adjustments
- Emergency responses
- Maintenance modes

### 6. **Monitoring**
- Real-time channel status
- Power consumption tracking
- Temperature monitoring
- Schedule visualization

## Installation

1. Copy `ledbrick_package.yaml` to your Home Assistant `packages` directory

2. Edit the `tank_groups` configuration in the package file:
   ```yaml
   tank_groups:
     initial: >
       {
         "display_tank": ["ledbrick_left", "ledbrick_center", "ledbrick_right"],
         "frag_tank": ["ledbrick_frag"],
         "quarantine_tank": ["ledbrick_qt"]
       }
   ```

3. Ensure your `configuration.yaml` includes packages:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

4. Restart Home Assistant

5. Add the `tank_group_dashboard.yaml` to your Lovelace UI

## Configuration Examples

### Single Unit Setup
```json
{
  "main_tank": ["ledbrickplus"]
}
```

### Multiple Units on One Tank
```json
{
  "display_tank": ["ledbrick_1", "ledbrick_2", "ledbrick_3"],
  "sump": ["ledbrick_sump"]
}
```

### Multiple Tanks
```json
{
  "display_tank": ["ledbrick_main_left", "ledbrick_main_right"],
  "frag_tank": ["ledbrick_frag"],
  "quarantine": ["ledbrick_qt"]
}
```

### Alternative: Manual Installation

1. Copy individual configurations from the `automations`, `scripts`, and `dashboards` folders
2. Add them to your existing Home Assistant configuration
3. Restart Home Assistant

## Channel Mapping Example

For a typical reef tank:
- Channel 1: Blue (450nm)
- Channel 2: Royal Blue (470nm)
- Channel 3: UV (420nm)
- Channel 4: Cool White (6500K)
- Channel 5: Warm White (3000K)
- Channel 6: Red (660nm)
- Channel 7: Green (520nm)
- Channel 8: Far Red (730nm)

## Quick Start

1. Load the default dynamic sunrise/sunset preset:
   ```yaml
   service: esphome.ledbrickplus_load_preset
   data:
     preset_name: "dynamic_sunrise_sunset"
   ```

2. Enable moon simulation:
   ```yaml
   service: switch.turn_on
   target:
     entity_id: switch.ledbrickplus_moon_simulation_enabled
   ```

3. Set PWM scale for normal operation:
   ```yaml
   service: number.set_value
   target:
     entity_id: number.ledbrickplus_pwm_scale
   data:
     value: 100
   ```

## Advanced Features

### Custom Dynamic Points
Create schedule points that adjust with sunrise/sunset:
```yaml
service: esphome.ledbrickplus_set_dynamic_schedule_point_8ch
data:
  time_type: "sunrise_relative"
  offset_minutes: -30  # 30 minutes before sunrise
  pwm_ch1: 10          # Start ramping up blue
  # ... other channels
```

### JSON Import/Export
Export current schedule:
```yaml
service: esphome.ledbrickplus_get_schedule_json
```

Import a saved schedule:
```yaml
service: esphome.ledbrickplus_import_schedule_json
data:
  json_data: '{"num_channels":8,"schedule_points":[...]}'
```