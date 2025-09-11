# LEDBrick Plus Home Assistant Integration

Simple, focused integration for LEDBrick Plus with Home Assistant. Supports both single unit and multi-unit tank setups.

## Features

- **Direct ESPHome Integration**: Uses native ESPHome entities and services
- **Multi-Unit Support**: Control multiple units as tank groups
- **Clean Dashboards**: Modern UI without complex automations
- **Essential Controls**: Schedule management, brightness, presets
- **Real-time Monitoring**: Power, temperature, and system status

## Quick Start

### 1. Install the Simple Package

Copy `ledbrick_simple_package.yaml` to your Home Assistant packages directory:

```bash
cp ledbrick_simple_package.yaml /config/packages/
```

Enable packages in `configuration.yaml`:
```yaml
homeassistant:
  packages: !include_dir_named packages
```

### 2. Configure Your Units

Edit the `tank_groups` helper in the package:
- **Single unit**: `{"main": ["ledbrickplus"]}`
- **Multiple units**: `{"main": ["ledbrick1", "ledbrick2"], "frag": ["ledbrick3"]}`

### 3. Add a Dashboard

Choose one of the provided dashboards:
- `ledbrick_plus_dashboard.yaml` - Single unit control
- `ledbrick_multi_unit_dashboard.yaml` - Multi-unit with tank groups

## Available Dashboards

### Single Unit Dashboard
Clean interface for controlling one LEDBrick Plus:
- Real-time channel status with colors
- System monitoring (power, temperature, fan)
- Schedule presets and controls
- Temperature control status

### Multi-Unit Dashboard  
Tank-based grouping for multiple units:
- Tank selector with unit overview
- Synchronized controls across units
- Individual unit status cards
- Group actions (brightness, scheduler, presets)

## Key Entities

Each LEDBrick Plus exposes these entities via ESPHome:

### Controls
- `switch.{device}_scheduler_enabled` - Enable/disable scheduler
- `number.{device}_pwm_scale` - Global brightness (0-100%)
- `light.{device}_ch{1-8}_pwm` - Individual channel PWM controls
- `number.{device}_ch{1-8}_current` - Channel current limits
- `number.{device}_ch{1-8}_max_current` - Maximum current settings

### Sensors
- `sensor.{device}_scheduler_pwm_channel_{1-8}` - Current PWM values
- `sensor.{device}_scheduler_current_channel_{1-8}` - Current draw
- `sensor.{device}_ina228_current` - Total current draw
- `sensor.{device}_ina228_bus_voltage` - Input voltage
- `sensor.{device}_dallas_temperature` - Heatsink temperature
- `sensor.{device}_fan_speed` - Fan RPM
- `sensor.{device}_moon_phase` - Current moon phase
- `text_sensor.{device}_ch{1-8}_color` - Channel color configuration

### Services
- `esphome.{device}_load_preset` - Load schedule preset
- `esphome.{device}_clear_schedule` - Clear all schedule points
- `esphome.{device}_set_schedule_point_8ch` - Add schedule point
- `esphome.{device}_get_schedule_json` - Export schedule
- `esphome.{device}_import_schedule_json` - Import schedule

## Simple Package Contents

The `ledbrick_simple_package.yaml` includes:

### Helpers
- `input_text.tank_groups` - JSON tank/unit mapping
- `input_select.active_tank` - Current tank selection
- `input_number.tank_brightness` - Global brightness control
- `input_boolean.sync_units` - Synchronize units toggle
- `input_boolean.feeding_mode` - Simple feeding timer

### Template Sensors
- `sensor.active_tank_units` - Unit count and list
- `sensor.tank_total_power` - Combined power usage
- `sensor.tank_average_temperature` - Average temperature
- `sensor.tank_schedule_status` - Schedule sync status

### Scripts
- `script.ledbrick_load_preset` - Load preset to all units
- `script.ledbrick_clear_schedule` - Clear schedules
- `script.ledbrick_toggle_scheduler` - Toggle schedulers
- `script.ledbrick_set_brightness` - Set brightness
- `script.ledbrick_emergency_off` - Emergency shutdown

### Automations
- Tank brightness synchronization
- Simple 10-minute feeding mode

## Installation Steps

1. **Add ESPHome Device**
   ```yaml
   esphome:
     name: ledbrickplus
     friendly_name: "LEDBrick Plus"
   ```

2. **Install Package**
   - Copy `ledbrick_simple_package.yaml` to `/config/packages/`
   - Restart Home Assistant

3. **Configure Units**
   - Go to Settings → Devices & Services → Helpers
   - Edit "Tank Group Configuration"
   - Add your unit names in JSON format

4. **Add Dashboard**
   - Go to your Lovelace dashboard
   - Add a new card
   - Paste contents from chosen dashboard YAML

## Custom Card Requirements

Install via HACS for full dashboard functionality:
- `custom:button-card`
- `custom:auto-entities` 
- `custom:multiple-entity-row`
- `custom:layout-card`
- `card-mod`

## Tips

- Use the web UI at `http://ledbrickplus.local` for advanced configuration
- Schedule presets: `dynamic_sunrise_sunset`, `full_spectrum`, `simple`
- Moon simulation automatically follows moon phases
- Temperature control maintains safe operating temperatures
- Fan speed adjusts based on temperature

## Troubleshooting

- **Units not syncing**: Check `input_boolean.sync_units` is enabled
- **No data**: Verify ESPHome device names match configuration
- **Missing entities**: Ensure ESPHome firmware is up to date
- **Dashboard errors**: Install required custom cards via HACS

## Support

- [GitHub Issues](https://github.com/theatrus/ledbrick/issues)
- [LEDBrick Forum Thread](https://www.reef2reef.com/threads/ledbrick-diy-led-pendant-with-pucks.243746/)