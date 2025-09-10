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
  phase_scaling: boolean;
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
  solar_noon_time?: string;
  channels?: Channel[];
  voltage?: number;
  total_current?: number;
  fan_speed?: number;
  fan_on?: boolean;
  temperatures?: TemperatureSensor[];
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