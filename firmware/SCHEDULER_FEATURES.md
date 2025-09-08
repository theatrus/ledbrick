# LEDBrick Scheduler - Features and Usage

## ✅ Recent Updates

### Timezone Support
- **Home Assistant Time Source**: Scheduler now uses `homeassistant_time` as primary time source for automatic timezone detection
- **Fallback SNTP**: Uses SNTP with `America/Los_Angeles` timezone if Home Assistant unavailable
- **Configuration**: Set timezone in `ledbrick_scheduler` config: `timezone: America/Los_Angeles`
- **Display**: All times shown in configured local timezone (not UTC)

### JSON Export Service  
- **New Service**: `get_schedule_json` exports complete scheduler configuration as JSON
- **Rich Data**: Includes current time, timezone, current interpolated values, and all schedule points
- **Time Display**: Human-readable time format (HH:MM) alongside raw minutes
- **Logging**: JSON output appears in ESPHome logs for easy access

## 🎛️ Available Services

### Schedule Management
- `set_schedule_point_8ch` - Set schedule point for all 8 channels
- `set_schedule_point_4ch` - Simplified 4-channel version  
- `remove_schedule_point` - Remove schedule point at specific time
- `clear_schedule` - Clear all schedule points

### Preset Management  
- `load_preset` - Load built-in presets (sunrise_sunset, full_spectrum, simple)
- `save_preset` - Save current schedule as named preset

### Control & Status
- `set_scheduler_enabled` - Enable/disable scheduler
- `get_scheduler_status` - Log current status and interpolated values
- `get_schedule_json` - **NEW** Export complete configuration as JSON

### Storage Management
- `save_to_flash` - Force save to ESP32 flash memory
- `load_from_flash` - Force reload from flash memory
- `export_schedule` - Log schedule data (basic format)

## 📄 JSON Export Example

```json
{
  "timezone": "America/Los_Angeles",
  "current_time_minutes": 720,
  "current_time_display": "12:00",
  "enabled": true,
  "channels": 8,
  "current_pwm_values": [60, 80, 100, 90, 80, 70, 60, 40],
  "current_current_values": [1.0, 1.5, 2.0, 1.8, 1.5, 1.2, 1.0, 0.6],
  "schedule_points": [
    {
      "time_minutes": 360,
      "time_display": "06:00", 
      "pwm_values": [0, 0, 0, 20, 40, 60, 80, 0],
      "current_values": [0, 0, 0, 0.3, 0.6, 1.0, 1.2, 0]
    }
  ]
}
```

## 🏠 Home Assistant Integration

### Scripts Available
- `ledbrick_export_json` - Trigger JSON export
- `ledbrick_morning_gentle` - Set gentle morning lighting  
- `ledbrick_midday_full` - Set full spectrum midday
- `ledbrick_evening_warm` - Set warm evening lighting

### Usage Example
```yaml
# Call the JSON export service
- service: script.ledbrick_export_json

# Check ESPHome logs for JSON output:
# "Schedule JSON Export: {timezone:..., current_time_minutes:720, ...}"
```

## ⚙️ Configuration

### ESPHome Configuration
```yaml
# Time sources with timezone support
time:
  - platform: homeassistant
    id: homeassistant_time  # Automatically uses HA timezone
  - platform: sntp
    id: sntp_time
    timezone: America/Los_Angeles  # Fallback timezone

# Scheduler with timezone
ledbrick_scheduler:
  id: main_scheduler
  time_source: homeassistant_time
  timezone: America/Los_Angeles
  channels: 8
  update_interval: 30s
```

## 🔧 Local Storage
- **Automatic**: Schedule changes are automatically saved to ESP32 flash
- **Persistent**: Survives power cycles and reboots
- **Fallback**: Loads default "sunrise_sunset" preset if no saved data
- **Capacity**: ~4KB storage allows approximately 50 schedule points

## 📅 Built-in Presets

### sunrise_sunset
- **6 AM**: Gradual dawn lighting
- **12 PM**: Full spectrum midday  
- **6 PM**: Warm dusk lighting
- **12 AM**: Lights off

### full_spectrum
- **8 AM**: Morning high-intensity
- **12 PM**: Maximum full spectrum
- **4 PM**: Afternoon lighting
- **8 PM**: Evening warmth

### simple
- **8 AM**: All channels 70% / 1.2A
- **8 PM**: All channels off

All schedule points use smooth linear interpolation between set times.