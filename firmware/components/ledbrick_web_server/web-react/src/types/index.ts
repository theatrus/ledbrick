export interface SchedulePoint {
  time_minutes: number;
  pwm_values: number[];
  current_values: number[];
  time_type?: string;
  offset_minutes?: number;
}

export interface ChannelConfig {
  rgb_hex: string;
  max_current: number;
  name?: string;
}

export interface MoonSimulation {
  enabled: boolean;
  phase_scaling: boolean;  // Legacy field
  phase_scaling_pwm?: boolean;
  phase_scaling_current?: boolean;
  min_current_threshold?: number;
  base_intensity: number[];
  base_current: number[];
}

export interface Schedule {
  version: number;
  num_channels: number;
  schedule_points: SchedulePoint[];
  channel_configs: ChannelConfig[];
  moon_simulation?: MoonSimulation;
  timezone?: string;
  timezone_offset_hours?: number;
  current_time_minutes?: number;
  enabled?: boolean;
  latitude?: number;
  longitude?: number;
  astronomical_projection?: boolean;
  time_shift_hours?: number;
  time_shift_minutes?: number;
}

export interface Channel {
  id: number;
  pwm: number;
  current: number;
}

export interface TemperatureSensor {
  name?: string;
  value: number;
}

export interface Status {
  enabled: boolean;
  time_formatted: string;
  time_minutes: number;
  schedule_points: number;
  pwm_scale: number;
  latitude: number;
  longitude: number;
  astronomical_projection: boolean;
  time_shift_hours: number;
  time_shift_minutes: number;
  moon_phase?: number;
  moon_simulation?: MoonSimulation;
  sunrise_time?: string;
  sunset_time?: string;
  moonrise_time?: string;
  moonset_time?: string;
  solar_noon_time?: string;
  channels?: Channel[];
  voltage?: number;
  total_current?: number;
  fan_speed?: number;
  fan_on?: boolean;
  temperatures?: TemperatureSensor[];
  thermal_emergency?: boolean;
  temperature_control?: {
    enabled: boolean;
    current_temp: number;
    target_temp: number;
    fan_pwm: number;
    pid_error: number;
  };
}

export interface InterpolationResult {
  valid: boolean;
  pwm_values: number[];
  current_values: number[];
}

export type DynamicTimeType = 
  | 'fixed'
  | 'sunrise_relative'
  | 'sunset_relative'
  | 'solar_noon'
  | 'civil_dawn_relative'
  | 'civil_dusk_relative'
  | 'nautical_dawn_relative'
  | 'nautical_dusk_relative';

export interface TemperatureConfig {
  target_temp_c: number;
  kp: number;
  ki: number;
  kd: number;
  min_fan_pwm: number;
  max_fan_pwm: number;
  fan_update_interval_ms: number;
  emergency_temp_c: number;
  recovery_temp_c: number;
  emergency_delay_ms: number;
  sensor_timeout_ms: number;
  temp_filter_alpha: number;
}

export interface TemperatureStatus {
  enabled: boolean;
  thermal_emergency: boolean;
  fan_enabled: boolean;
  current_temp_c: number;
  target_temp_c: number;
  fan_pwm_percent: number;
  fan_rpm: number;
  pid_error: number;
  pid_output: number;
  sensors_valid_count: number;
  sensors_total_count: number;
}

export interface FanCurvePoint {
  temperature: number;
  fan_pwm: number;
}

export interface FanCurve {
  points: FanCurvePoint[];
}