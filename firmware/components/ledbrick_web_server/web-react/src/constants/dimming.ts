// Limits for per-channel dimming, matching the firmware's checks
// (POST /api/channel/dimming)
export const MIN_FLOOR_CURRENT = 0.05;  // A; the board keeps EN/PWM off below 50 mA
export const MAX_FLOOR_CURRENT = 2.0;   // A; the driver's limit
export const DEFAULT_FLOOR_CURRENT = 0.1;
export const MAX_LED_KINDS = 8;         // LED models per channel
export const MIN_LED_COUNT = 1;
export const MAX_LED_COUNT = 100;

export const DIM_MODE_LABELS = {
  manual: 'Manual',
  curve: 'LED curve',
} as const;

export const DIM_PRIORITY_LABELS = {
  current: 'Current first',
  pwm: 'PWM first',
} as const;
